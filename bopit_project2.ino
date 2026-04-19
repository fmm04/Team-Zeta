//For Microphone (DF Player):
#include "Arduino.h"
#include "DFRobotDFPlayerMini.h"
//For score display: 
  // Also for text display
#include <M5UNIT_DIGI_CLOCK.h>
//For text display:
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h>


/*
However, the following pins are fixed by hardware peripheral constraints and must not be reassigned:
Pin	Function	Reason
PC0 (SCL)	OLED I2C clock	Hardware I2C fixed pin
PC1 (SDA)	OLED I2C data	Hardware I2C fixed pin
PD0 (RXD0)	Debug UART RX	Hardware UART0 fixed pin
PD1 (TXD0)	Debug UART TX	Hardware UART0 fixed pin
PD2 (RXD1)	DFPlayer TX → MCU	Hardware UART1 fixed pin
PD3 (TXD1)	MCU TX → DFPlayer RX	Hardware UART1 fixed pin
PA0 (ADC0)	Microphone analog input	ADC input pin
All remaining pin assignments (LEDs, PTT button, rotary encoder, TM1637 CLK/DIO) are to be confirmed after hardware layout is finalized.
*/
//Restart Pin
#define RESTART_PIN 5   // PB5

//Possibly need to hard code pins and not do digital pins 
//Blow detection test
#define MIC_PIN A0
int blow_threshold_detect = 1000;

//PTT Button: 
#define PTT_PIN PIN_PD4


//For Score Display:
#define ADDR 0x30

M5UNIT_DIGI_CLOCK Digiclock;

const int scoreA = 0; 

//Text Display:
#define OLED_ADDR 0x3C
Adafruit_SH1106G txtDisplay = Adafruit_SH1106G(128, 64, &Wire, -1);


//Mic/DF Player
#define PIN_BUSY 15
#define blow_prompt 1 //0001
#define talk_prompt 2 //0002
#define radio_prompt 3 //0003
#define success_sound 4 //0004
#define fail_sound 5 //0005
#define gameSuccess_sound 6 //0006
#define radio_tone 7 //0007
DFRobotDFPlayerMini myDFPlayer;


// the setup function runs once when you press reset or power the board
void setup() {
  // initialize pins
  //For LEDs:
  DDRB |= (1 << PB2) | (1 << PB3) | (1 << PB4);
  resetLEDs();  

  //Restart Button
  pinMode(RESTART_PIN, INPUT_PULLUP);

  //Round Randomize 
  randomSeed(analogRead(A1));

  //For PTT Button:
  pinMode(PTT_PIN, INPUT_PULLUP);  // Enable internal pull-up

  //For score display:
  Wire.begin();
  Digiclock.begin(&Wire);
  Digiclock.setBrightness(0x0f);  // 0x00–0x0f
  Digiclock.setSegments(0, 0, 0, 0, false);
  updateScoreDisplay(0);


  //Text Display
  delay(100);
  txtDisplay.begin(OLED_ADDR, true);
  txtDisplay.clearDisplay();


  //DF Player (Mic)
  Serial1.begin(9600);  //.begin() initializes serual communication between Board and other devices (and sets baud rate)

  pinMode(PIN_BUSY, INPUT);


  delay(2000);

  myDFPlayer.begin(Serial1);
  myDFPlayer.volume(20);
  myDFPlayer.play(1);


  //Encoder (Dial)
  pinMode(5, INPUT_PULLUP);  // PD5
  pinMode(6, INPUT_PULLUP);  // PD6

  Serial.begin(9600);

}

void loop() 
{

  if (checkRestartButton()) 
  {
    restartGame();
  }

  displayStart();
  delay(5000);
  int score = 0;
  bool fail = false;
  const int totalRounds = 3;   
  const int testChoice = 3;    

  for(int round = 1; round <= totalRounds; round++)
  {
    resetLEDs();  
    displayRoundNumber(round);
    delay(3000);
    //In cases or actual functions themselves, have a countdown that gives user enough time to complete task
    //Give warning on time? or no? Maybe display how much time there is left
    int order[testChoice] = {1, 2, 3};

    //Shuffle the array
    for(int i = testChoice - 1; i > 0; i--)
    {
      int j = random(0, i + 1);
      int temp = order[i];
      order[i] = order[j];
      order[j] = temp;
    }

    for(int i = 0; i < testChoice; i++)
    {
      int testChoice = order[i];
      switch (testChoice) 
      {
        case 1:
          //Trigger LED pin 2 and corresponding noise
          playAudio(3);
          LEDTwo();
          displayRadioRound();
          delay(5000);
          //Cool idea if we have time.. have radio freq noise in background while this 
          if(!radio())
          {
            fail = true;
            goto END_GAME;
          }
          playAudio(4);
          displayTestSuccess();
            score++;
            updateScoreDisplay(score);
            delay(3000);
          break;
        case 2:
          //Trigger LED pin 3 and corresponding noise 
          playAudio(1);
          LEDThree();
          displayTempRound();
          delay(5000);
          if(!tempDetect())
          {
            fail = true;
            goto END_GAME;
          }
          playAudio(4);
            displayTestSuccess();
            score++;
            updateScoreDisplay(score);
            delay(3000);
          break;
        case 3:
          //Trigger LED pin 4 and corresponding noise 
          playAudio(2);
          LEDFour();
          displayTalkRound();
          //Delay; if time runs out, jump to else 
          if(!button(3000))
          {
              fail = true;
              goto END_GAME;
          }
          playAudio(4);
              displayTestSuccess();
              score++;
              updateScoreDisplay(score);
              delay(3000);
          break; 
      }
    }
   
    
  }  
   END_GAME:
    resetLEDs(); 
      if(fail)
      {
        playAudio(5);
        displayGameOver();
        while(true) 
        {
          if(checkRestartButton())
          {
            restartGame();
          }
        }
      }

      playAudio(6);
      displayGameWon();
      while(true) 
      {
        if(checkRestartButton())
        {
          restartGame();
        }
      }

}



//Temperature 
/*The blow detection input will come from the microphone module as an analog signal into the MCU ADC pin.
The software side will need to sample the ADC input and determine a threshold for recognizing a valid blow action.
A simple threshold on one ADC sample may not be enough, so it is recommended to evaluate the signal over a short time window and use amplitude / peak-to-peak variation as the detection criterion.
The exact ADC threshold value can remain adjustable until testing is done.
Note: The microphone is used exclusively for blow detection. The PTT action requires only a button press and does not involve microphone input.
*/
bool tempDetect() {
  const unsigned long sampleWindow = 200;
  unsigned long startTime = millis();
  int peak = 0; 

  while (millis() - startTime < sampleWindow)
  {
    int val = analogRead(MIC_PIN);
    if(val > peak)
    {
      peak = val;
    }
  }

  if(peak > blow_threshold_detect)
  {
    return true;
  }
  else
  {
    return false;
  }
}



//LED Functions -- An LED corresponds to each action and lights up when that action is requested from the game 
void LEDTwo()
{
  PORTB |= (1 << PB2);
  PORTB &= ~((1 << PB3) | (1 << PB4));
  delay(500);
}

void LEDThree()
{
  PORTB |= (1 << PB3);
  PORTB &= ~((1 << PB2) | (1 << PB4));
  delay(500);
}

void LEDFour()
{
  PORTB |= (1 << PB4);
  PORTB &= ~((1 << PB2) | (1 << PB3));
  delay(500);
}

void resetLEDs() {
    PORTB &= ~((1 << PB2) | (1 << PB3) | (1 << PB4));
}


//Button Press 
bool button(unsigned long timeoutMs) 
{
  unsigned long start = millis();

  while (millis() - start < timeoutMs) 
  {
    if (digitalRead(PTT_PIN) == LOW) 
    {
      unsigned long holdStart = millis();
      
      // require a stable hold
      while (digitalRead(PTT_PIN) == LOW) 
      {
        if (millis() - holdStart >= 100) 
        {
          return true;
        }
      }
    }

  }
    return false;
}




/*Text Display
The OLED text display will be controlled by the MCU over I2C.
Software will need to define display functions for all game states. The following states and display strings should be defined:
*/
//Or put start and countdown in same function? 


void displayStart()
{
  txtDisplay.setTextSize(2);
  txtDisplay.setTextColor(SH110X_WHITE);  //On monochrome OLEDs, SH110X_WHITE means turn pixels on 
  txtDisplay.setCursor(10, 10);
  txtDisplay.println("HELLO");
  txtDisplay.display();
}


void displayTempRound() 
{
  txtDisplay.clearDisplay();
  txtDisplay.setTextSize(2);
  txtDisplay.setCursor(10, 10);
  txtDisplay.print("BLOW INTO SENSOR");
  txtDisplay.display();
}

void displayTalkRound() 
{
  txtDisplay.clearDisplay();
  txtDisplay.setTextSize(2);
  txtDisplay.setCursor(10, 10);
  txtDisplay.print("PRESS BUTTON TO TALK");
  txtDisplay.display();
}

void displayRadioRound() 
{
  txtDisplay.clearDisplay();
  txtDisplay.setTextSize(2);
  txtDisplay.setCursor(10, 10);
  txtDisplay.print("TURN RADIO DIAL");
  txtDisplay.display();
}

void displayTestSuccess() {
  txtDisplay.clearDisplay();
  txtDisplay.setTextSize(2);
  txtDisplay.setCursor(10, 10);
  txtDisplay.print("TEST COMPLETE");
  txtDisplay.display();
}

void displayTestFail() 
{
  txtDisplay.clearDisplay();
  txtDisplay.setTextSize(1);
  txtDisplay.setCursor(10, 10);
  txtDisplay.print("FAILED TEST");
  txtDisplay.display();
}

void displayGameOver() 
{
  txtDisplay.clearDisplay();
  txtDisplay.setTextSize(2);
  txtDisplay.setCursor(10, 20);
  txtDisplay.print("GAME OVER");
  txtDisplay.display();
}

void displayRoundNumber(int i) 
{
  txtDisplay.clearDisplay();
  txtDisplay.setTextSize(2);
  txtDisplay.setCursor(10, 20);
  if(i == 1)
  {
    txtDisplay.print("Round 1");
    txtDisplay.display();
  }
  else if(i == 2)
  {
    txtDisplay.print("Round 2");
    txtDisplay.display();
  }
  else if(i == 3)
  {
    txtDisplay.print("Round 3");
    txtDisplay.display();
  }
}

void displayGameWon() 
{
  txtDisplay.clearDisplay();
  txtDisplay.setTextSize(2);
  txtDisplay.setCursor(10, 20);
  txtDisplay.print("YOU WIN!");
  txtDisplay.display();
}


/*Score Display
The MCU software will need to send commands to update the current score or final score.
Score should be displayed on the rightmost two digits of the  4-digit display, formatted as a 2-digit number (00–99). 
The colon segment should remain off at all times to avoid visual confusion.
*/
void updateScoreDisplay(int score)
{
  Digiclock.setString(score);
}



//Sound effects
void playAudio(uint16_t fileNumber) 
{
  myDFPlayer.play(fileNumber);
}


//Encoder-- Radio Freq dial check 
bool radio() 
{
  playAudio(7);
  bool pd5_low = !(PIND & (1 << PIND5));
  bool pd6_low = !(PIND & (1 << PIND6));
  if (pd5_low || pd6_low) 
  {
    PORTB |= (1 << PORTB2);
    delay(100);
    return true; 
  } 
  else 
  {
    PORTB &= ~(1 << PORTB2);
    return false;
  }
}

//Debounce for restart button
bool checkRestartButton() 
{
  static bool lastState = HIGH;
  static unsigned long lastTime = 0;
  const unsigned long debounceDelay = 50;

  bool reading = digitalRead(RESTART_PIN);

  if (reading != lastState) 
  {
    lastTime = millis();   // reset debounce timer
  }

  if ((millis() - lastTime) > debounceDelay) 
  {
    if (reading == LOW && lastState == HIGH) 
    {
      lastState = reading;
      return true;       // HIGH to LOW transition detected
    }
  }

  lastState = reading;
  return false;
}


//Actual Reset
void restartGame() 
{
    // Reset game variables
    //Add more possibly 

    updateScoreDisplay(0);
    txtDisplay.clearDisplay();
    txtDisplay.setCursor(10, 20);
    txtDisplay.print("RESTARTING...");
    txtDisplay.display();

    delay(1000);

    // Restart the entire program cleanly
    asm volatile ("jmp 0");
}
