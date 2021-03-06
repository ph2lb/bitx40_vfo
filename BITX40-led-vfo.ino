/*  
 * ------------------------------------------------------------------------
 * "PH2LB LICENSE" (Revision 1) : (based on "THE BEER-WARE LICENSE" Rev 42) 
 * <lex@ph2lb.nl> wrote this file. As long as you retain this notice
 * you can do modify it as you please. It's Free for non commercial usage 
 * and education and if we meet some day, and you think this stuff is 
 * worth it, you can buy me a beer in return
 * Lex Bolkesteijn 
 * ------------------------------------------------------------------------ 
 * Filename : BITX40-led-v1.ino  
 * Version  : 0.1 (DRAFT)
 * ------------------------------------------------------------------------
 * Description : A Arduino NANO DDS based VFO for the BITX40 with LED display
 * ------------------------------------------------------------------------
 * Revision : 
 *  - 2017-jan-08 0.1 initial version 
 *  - 2017-jan-12 0.2 added A/B (split in next version??) 2 pins used
 *                    to select VFOA or VFOB, pull down to select. When
 *                    both low VFOA wins. when both high VFOB wins. Why
 *                    2 pins? because in next release maybe split 
 *                    (VFOA = RX, VFOB = TX) will be added. 
 *  - 2017-jan-17 0.3 better support for AB (remember step)
 *                    added FreqOffset (see comment below)
 * ------------------------------------------------------------------------
 * Hardware used : 
 *  - Arduino NANO  
 *  - MAX7219 
 *  - AD9833
 *  - Simpel rotary encoder with switch
 * ------------------------------------------------------------------------
 * Software used :  
 *  - LedControl library from Eberhard Fahle
 * ------------------------------------------------------------------------ 
 * TODO LIST : 
 *  - add more sourcode comment 
 *  - add freqoffset in eeprom
 * ------------------------------------------------------------------------ 
 */
 
  
#include "AD9833.h"    
#include "LedControl.h"  

#define debugSerial Serial

#define debugPrintLn(...) { if (debugSerial) debugSerial.println(__VA_ARGS__); }
#define debugPrint(...) { if (debugSerial) debugSerial.print(__VA_ARGS__); }

 
AD9833 ad9833= AD9833(11, 13, A1); // SPI_MOSI, SPI_CLK, SPI_CS 
LedControl lc=LedControl(11,13,A2,1);  // SPI_MOSI, SPI_CLK, SPI_CS, NR OF DEVICES  NOTE TO MY SELF>> CS is now A1 instead of 12 (12=MISO on UNO) 
 
#define buttonAnalogInput A0   // A0 on the LCD shield (save some pins)

// BITX40 VFO limits (4.8Mhz - 5Mhz)
uint32_t vfofreqlowerlimit = 48e5;
uint32_t vfofrequpperlimit = 5e6;

// for a little debounce (needs to be better then now)
int_fast32_t prevtimepassed = millis(); // int to hold the arduino miilis since startup

// define the band
uint32_t FreqLowerLimit = (uint32_t)7000e3; // lower limit acording to band plan
uint32_t FreqUpperLimit = (uint32_t)7200e3; // upper limit according to bandplan
uint32_t FreqBase = (uint32_t)7000e3; // the base frequency  
uint32_t FreqVFOA = (uint32_t)7100e3 ; // the A frequency (set with default) 
uint32_t FreqVFOB = (uint32_t)7150e3 ; // the B frequency (set with default)  
uint32_t Freq = FreqVFOA;  // default
long FreqOffset =  -1500;   // You have to set Freq off set in Hz. because no BitX has the same crystals.

// define the stepstruct
typedef struct 
{
  char * Text;
  uint32_t Step;
} 
StepStruct;

// define the step enum
typedef enum 
{
  STEPMIN = 0,
  S10 = 0,
  S50 = 1,
  S100 = 2,
  S500 = 3,
  S1KHZ = 4,
  S5KHZ = 5,
  S10KHZ = 6,
  STEPMAX = 6
} 
StepEnum;

// define the bandstruct array
StepStruct  Steps [] =
{
  { (char *)"10Hz", 10 }, 
  { (char *)"50Hz", 50 }, 
  { (char *)"100Hz", 100 }, 
  { (char *)"500Hz", 500 }, 
  { (char *)"1KHz", 1000 }, 
  { (char *)"5KHz", 5000 }, 
  { (char *)"10KHz", 10000 }
};

// Switching band stuff
boolean switchBand = false; 
int FreqStepIndex = (int)S100; // default 100Hz.
int FreqStepIndexA = FreqStepIndex;
int FreqStepIndexB = FreqStepIndex; 

// Encoder stuff
int encoder0PinALast = LOW; 
#define encoderPin1 2
#define encoderPin2 3
// #define pttSW   4  SPLIT SUPPORT NOT USED YET.
#define vfoASW 5
#define vfoBSW 6 
#define encoderSW 7

volatile int lastEncoded = 0;
volatile long encoderValue = 0;
long lastencoderValue = 0;
int lastMSB = 0;
int lastLSB = 0;

int nrOfSteps = 0;

// LCD stuff
boolean updatedisplayfreq = false;
boolean updatedisplaystep = false;
 
boolean useVFOA = false;
boolean useVFOB = false;

// Playtime
void setup() 
{  
  debugSerial.begin(57600); 
  debugPrintLn(F("setup"));  
  
  SPI.begin();  
  SPI.setDataMode(SPI_MODE2);   
  debugPrintLn(F("spi done"));  
  lc.init();
  /*
  The MAX72XX is in power-saving mode on startup,
  we have to do a wakeup call
  */
  lc.shutdown(0,false);
  /* Set the brightness to a medium values */
  lc.setIntensity(0,6);
  /* and clear the display */
  lc.clearDisplay(0);   
  debugPrintLn(F("led done"));   
  writeTextToLed("DDS"); 
  ad9833.init();
  ad9833.reset(); 
  writeTextToLed("initdone"); 
  updatedisplayfreq = true;
  updatedisplaystep = false;
  
  updateDisplays();
  setFreq();
   
  debugPrintLn(F("start loop")); 
 
  pinMode(encoderPin1, INPUT); 
  pinMode(encoderPin2, INPUT);
  pinMode(encoderSW, INPUT);
  // pinMode(pttSW, INPUT); NOT USED YET.
  pinMode(vfoASW, INPUT);
  pinMode(vfoBSW, INPUT); 

  digitalWrite(encoderPin1, HIGH); //turn pullup resistor on
  digitalWrite(encoderPin2, HIGH); //turn pullup resistor on
  digitalWrite(encoderSW, HIGH); //turn pullup resistor on
  // digitalWrite(pttSW, HIGH); //turn pullup resistor on NOT USED YET.
  digitalWrite(vfoASW, HIGH); //turn pullup resistor on
  digitalWrite(vfoBSW, HIGH); //turn pullup resistor on
  
  //call updateEncoder() when any high/low changed seen
  //on interrupt 0 (pin 2), or interrupt 1 (pin 3) 
  attachInterrupt(0, updateEncoder, CHANGE); 
  attachInterrupt(1, updateEncoder, CHANGE);
} 

// INT0 and INT1 interrupt handler routine (used for encoder)
void updateEncoder()
{
  int MSB = digitalRead(encoderPin1); //MSB = most significant bit
  int LSB = digitalRead(encoderPin2); //LSB = least significant bit

  int encoded = (MSB << 1) |LSB; //converting the 2 pin value to single number
  int sum  = (lastEncoded << 2) | encoded; //adding it to the previous encoded value

  if(sum == 0b1101 || sum == 0b0100 || sum == 0b0010 || sum == 0b1011) encoderValue ++;
  if(sum == 0b1110 || sum == 0b0111 || sum == 0b0001 || sum == 0b1000) encoderValue --;

  lastEncoded = encoded; //store this value for next time
}

// function to set the AD9833 to the VFO frequency depending on bandplan.
void setFreq()
{
  // now we know what we want, so scale it back to the 4.8Mhz - 5Mhz freq
  uint32_t frequency = Freq - FreqBase;
  uint32_t ft301freq = (vfofrequpperlimit - vfofreqlowerlimit) - frequency + vfofreqlowerlimit;
  
  ad9833.setFrequency((long)ft301freq + FreqOffset); 
}
 
void writeTextToLed(char *p)
{ 
    lc.clearDisplay(0);
    lc.setChar(0, 0, p[7], false);
    lc.setChar(0, 1, p[6], false);
    lc.setChar(0, 2, p[5], false);
    lc.setChar(0, 3, p[4], false);   
    lc.setChar(0, 4, p[3], false);  
    lc.setChar(0, 5, p[2], false);  
    lc.setChar(0, 6, p[1], false);  
    lc.setChar(0, 7, p[0], false);   
    delay ( 00 ); 
}

void freqToLed( long v)
{ 
     lc.clearDisplay(0);
     // MAX 7.200.00  (no 1Hz) 
     // Variable value digit 
     int digito1;
     int digito2;
     int digito3;
     int digito4;
     int digito5;
     int digito6;
  
    // Calculate the value of each digit 
    digito1 = (v / 10 )% 10 ;
    digito2 = (v / 100 )% 10 ;  
    digito3 = (v / 1000 )% 10 ;
    digito4 = (v / 10000 )% 10 ;
    digito5 = (v / 100000 )% 10 ;
    digito6 = (v / 1000000 )% 10 ;
    
    // Display the value of each digit in the display   byte = point, A, B, C, D, E, G
    if (useVFOA)
      lc.setChar(0, 7, 'A', false); // or use A when you like
    else if (useVFOB)
      lc.setChar(0, 7, 'B', false); // or use B when you like
    else  
      lc.setChar(0, 7, 'S', false);

    lc.setDigit ( 0 , 5 , (byte) digito6, false);
    lc.setDigit ( 0 , 4 , (byte) digito5, false);
    lc.setDigit ( 0 , 3 , (byte) digito4, false);
    lc.setDigit ( 0 , 2 , (byte) digito3, true);
    lc.setDigit ( 0 , 1 , (byte) digito2, false);
    lc.setDigit ( 0 , 0 , (byte) digito1, false);
    delay ( 00 ); 
} 

void stepToLed( long s)
{ 
    lc.clearDisplay(0);
    lc.setChar(0, 7, 's', false);
    lc.setChar(0, 6, 't', false);
    lc.setChar(0, 5, 'e', false);
    lc.setChar(0, 4, 'p', true);  
     
    // Variable value digit 
    int digito1;
    int digito2;
    int digito3;
    int digito4; 
    boolean is10Kplus = false;
  
    if (s >= 1000)
    {
      s = s / 100;  
      is10Kplus = true;
    }
  
    // Calculate the value of each digit 
    digito1 = s %  10 ;
    digito2 = (s / 10 )% 10 ;
    digito3 = (s / 100 )% 10 ;  
    digito4 = (s / 1000 )% 10 ; 
    
    // Display the value of each digit in the display 
    if (s >= 1000)
    {
      lc.setDigit ( 0 , 3 , (byte) digito4, false);
    }
    if (s >= 100)
    {
      lc.setDigit ( 0 , 2 , (byte) digito3, false);
    }
    // there isn't anything smaller then 10
    lc.setDigit ( 0 , 1 , (byte) digito2, is10Kplus);
    lc.setDigit ( 0 , 0 , (byte) digito1, false); 
} 
 

// function to update the display
void updateDisplays()
{
  if (updatedisplayfreq)
  {
    freqToLed(Freq); 
  }

  if (updatedisplaystep)
  { 
    stepToLed(Steps[FreqStepIndex].Step); 
  } 
} 


boolean ccw = false;
boolean cw = false;
boolean changeStep = false;
boolean prevSw = false;
boolean prevVFOASW = false;
boolean prevVFOBSW = false;

// the main loop
void loop() 
{  
  boolean updatefreq = false; 
  updatedisplayfreq = false;  
  updatedisplaystep = false;
      
  if ((int)encoderValue / 4 !=0 )
  {
    nrOfSteps = (int)encoderValue / 4;
    encoderValue = 0; 
  } 

  // read VFOA switch
  useVFOA = digitalRead(vfoASW) == 0;
  if (useVFOA != prevVFOASW)
  {
    prevVFOASW = useVFOA;
    updatedisplayfreq = true;
    updatefreq = true;
  }
  // read VFOB switch
  useVFOB = digitalRead(vfoBSW) == 0; 
  if (useVFOB != prevVFOBSW)
  {
    prevVFOBSW = useVFOB;
    updatedisplayfreq = true; 
    updatefreq = true;
  }

  // load the Freq variable.
  Freq = FreqVFOA;  // default load VFO A
  if (useVFOB)  // but when VFO B is used load FreqB
    Freq = FreqVFOB;
  // load the FreqStepIndex variable.  
  FreqStepIndex = FreqStepIndexA;  // default load VFO A
  if (useVFOB)  // but when VFO B is used load FreqB
    FreqStepIndex = FreqStepIndexB;
        
  // read encoder switch 
  boolean sw = digitalRead(encoderSW) == 0;
  if (sw != prevSw)
  {
     prevSw = sw;
     if (sw)
     {
       changeStep = changeStep == 0; // toggle  
       if (changeStep)
         updatedisplaystep = true;
       else
         updatedisplayfreq = true;
       delay(250);  // software debounce
     }
  }
    
  int_fast32_t timepassed = millis(); // int to hold the arduino milis since startup
  if ( prevtimepassed + 10 < timepassed  || nrOfSteps != 0)
  {
    prevtimepassed = timepassed; 
    
    if (changeStep)
    {
      int nrOfStepsTmp = nrOfSteps; 
      nrOfSteps = 0; 
      
      if (nrOfStepsTmp > 0 && FreqStepIndex < STEPMAX)
      {
        FreqStepIndex = (StepEnum)FreqStepIndex + 1;
        updatedisplaystep = true;
      } 
      else if (nrOfStepsTmp < 0 && FreqStepIndex > STEPMIN)
      {
        FreqStepIndex = (StepEnum)FreqStepIndex - 1;
        updatedisplaystep = true;
      } 

      if (useVFOA || (useVFOA == 0 && useVFOB == 0))   // when VFO A selected
        FreqStepIndexA = FreqStepIndex; // store current FreqStepIndex in FreqStepIndexA
      else if (useVFOB) // when VFO B selected
        FreqStepIndexB = FreqStepIndex; // store current FreqStepIndex in FreqStepIndexB  
    }
    else
    {
      // Freq is set already so save to use.    
      if (nrOfSteps > 0)
      {
        int nrOfStepsTmp = nrOfSteps; 
        nrOfSteps = 0;
          if (Freq < FreqUpperLimit)
          {
            Freq = Freq + (Steps[FreqStepIndex].Step * nrOfStepsTmp);
            updatefreq = true;
            updatedisplayfreq = true;
          }
          if (Freq > FreqUpperLimit)
          {
            Freq = FreqUpperLimit;
          } 
      }
      else if ( nrOfSteps < 0)
      {
        int nrOfStepsTmp = nrOfSteps;
        nrOfSteps = 0;
          if (Freq > FreqLowerLimit)
          {
            Freq = Freq + (Steps[FreqStepIndex].Step * nrOfStepsTmp);
            updatefreq = true;
            updatedisplayfreq = true;
          }
          if (Freq < FreqLowerLimit)
          {
            Freq = FreqLowerLimit;
          }  
      } 
    }
  }

  if (updatefreq)
  { 
    if (useVFOA || (useVFOA == 0 && useVFOB == 0))   // when VFO A selected
      FreqVFOA = Freq; // store current freq in FreqA
    else if (useVFOB) // when VFO B selected
      FreqVFOB = Freq; // store current freq in FreqB
      
    setFreq();
  }

  updateDisplays();   
}
