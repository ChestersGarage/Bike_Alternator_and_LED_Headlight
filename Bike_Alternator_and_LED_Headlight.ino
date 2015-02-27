/*
  Bike Alterator LED Headlight control system.
  Runs on an Arduino/ATMEGA328.
  
  * LED brightness based on alternator output power, in consideration of:
    - Total system load, including uC and battery charging needs.
    - Maximum battery drain current when alternator output too low
  * LED brightness based on LED temperature.
  * Alternator cut-in/out at startup or over-output condition.
  * System impedance on the alternator for MPPT-like loading.
  * Alarm sound if any parameters are beyond critical thresholds:
    - Battery voltage too low.
    - Battery backup current too high.
    - Alternator current too high.
    - Alternator output voltage too high (analog trigger, not uC).
    - LED temperature too high.
*/
#include <Wire.h>
#include <Adafruit_MCP4725.h>
#include <Adafruit_INA219.h>

Adafruit_INA219 battVI(0x41);
//Adafruit_INA219 altVI(0x44);
Adafruit_MCP4725 Led1;
Adafruit_MCP4725 Led2;

// Comment out to disable debug output on serial
//#define DEBUG

// Serial on 0
// Serial on 1
#define indicatorRedPin 2
#define indicatorGreenPin 3
#define indicatorBluePin 4
#define led1EnablePin 5
#define led2EnablePin 6
#define alarmPin 7
// 8
// 9
// 10
#define chargePowerPin 11
#define chargeChargePin 12
#define chargeDonePin 13

#define ledTemperaturePin A0
#define boostVoltagePin A1
#define alternatorCurrentPin A2
#define alternatorVoltagePin A3
// A4 (SDA)
// A5 (SCL)

// Alternator
float alternatorVoltageIn;            // Alternator output voltage (V)
float alternatorVoltageMax = 35.0;    // Above this value (V), the LEDs are forced to maximum brightness and the alarm is sounded
float alternatorVoltageHi = 30.0;
float alternatorVoltageMin = 7.0;     // Below this value (V), the LEDs are forced to minimum brightness
float alternatorCurrentIn;            // Alternator output current (mA)
float alternatorCurrentMax = 3.0;  // Maximum alternator output current (mA)
//float alternatorPowerIn;

// Battery
float batteryVoltageIn;            // Battery voltage (V)
float batteryVoltageMin = 3.5;     // Below this value (V), LEDs are forced to OFF and the alarm is sounded
float batteryCurrentIn;            // Battery backup drain current (mA)
float batteryCurrentMax = 1500.0;  // Above this value (mA), the LED brightness is decreased and the alarm is sounded
//float batteryCurrentOpr = 500.0;  // An ideal level of batterty current (noted FYI)
boolean batteryChargeGood;
boolean batteryChargeDone;
float boostVoltageIn;         // Battery voltage is boosted to 15V before being used for anything. (measured)

// LED Temperature
int systemLedTemperatureIn;         // LED temperature ADC scale
int systemLedTemperatureMax = 660;  // Maximum safe LED temperature
int systemLedTemperatureHi = 650;   // High water mark LED temperature.  Decrease brightness to stay below

// Main LEDs and DACs
// RCD-24 driver dimming range: 100%=0.13V - 0%=4.5V
// DAC output range 0-4095, 5V VCC
// Dimming is on an inverted scale: 4095=off, 0=max
// Useful dimming range in terms of DAC: 106 - 3686
long dacRange = 4096;                       // Resolution of the LED dimming output
long ledDeadZoneLow = 900;
long led1Level;                             // Output value for LED1
long led2Level;                             // Output value for LED2
long led2Offset = dacRange*75/100;          // LED2 starts to turn on after about 75% brightness on LED1
long systemLedLevelBbu=2500;
long systemLedLevelMin = 1150;              // Dead zone at the lowest LED level;
long systemLedLevelOut = systemLedLevelMin; // System LED brightness level 
long systemLedLevelMax = dacRange*175/100;  // Maximum system LED brightness. LED levels are mapped into this so that LED1 is almost full bright before LED2 starts to light.
long ledLevelIncrementNormal = 25;          // LED output increment value

// System impedance
float systemImpedanceOpt;           // The calculated optimum system impedance, in Ohms.
#define systemImpedanceGap systemImpedanceOpt*0.25        // systemImpedanceTotal allowed to be this much less than systemImpedanceOpt. Reduces flicker.
float systemImpedanceTotal;         // The calculated total system impedance.
// LED brightness adjusted per "systemImpedanceOpt = systemImpedanceHi-(3*(alternatorVoltageIn-alternatorVoltageMin));"
//float systemImpedanceRef = 33.0;  // Based on offline device testing, this is a point that has to be met along the calculated impedance curve (at 14V output)
//float systemImpedanceLo = 12.0;   // Target lowest desireable impedance, based on offline device testing
#define systemImpedanceHi 50.0   // Target highest desireable impedance, based on offline device testing
byte errorLevel = 0;
byte powerStatus = 0;
int displayUpdateInterval = 100;
long displayUpdateTime = 0;

long indicatorBlinkInterval = 250;  
long indicatorBlinkTime = 0;
boolean indicatorBlinkState = HIGH;

void setup() {
#ifdef DEBUG
  Serial.begin(115200);
#endif
  battVI.begin();
//  altVI.begin();
  Led1.begin(0x63);
  Led2.begin(0x62);
  pinMode(alarmPin, OUTPUT);
  pinMode(indicatorRedPin, OUTPUT);
  pinMode(indicatorGreenPin, OUTPUT);
  pinMode(indicatorBluePin, OUTPUT);
  pinMode(led1EnablePin, OUTPUT);
  pinMode(led2EnablePin, OUTPUT);
  pinMode(boostVoltagePin, INPUT);
  pinMode(ledTemperaturePin, INPUT);
  pinMode(chargePowerPin, INPUT);
  pinMode(chargeChargePin, INPUT);
  pinMode(chargeDonePin, INPUT);
  digitalWrite(alarmPin, LOW);
  digitalWrite(indicatorRedPin, LOW);
  digitalWrite(indicatorGreenPin, LOW);
  digitalWrite(indicatorBluePin, LOW);
  digitalWrite(led1EnablePin, LOW);
  digitalWrite(led2EnablePin, LOW);
  Led1.setVoltage(4095, true);
  Led2.setVoltage(4095, true);
#ifdef DEBUG
  Serial.println("Valt\tIalt\tVbat\tIbat\tVbbu\tNow\tOpt\tGap\tLED1\tLED2\tTled");
#endif
}

// Read all sensor values in a single pass
void readSensors(){
//  alternatorVoltageIn  = altVI.getBusVoltage_V();
//  alternatorCurrentIn  = altVI.getCurrent_mA();
//  alternatorVoltageIn = 0.038242188*analogRead(alternatorVoltagePin);
//  alternatorCurrentIn = analogRead(alternatorCurrentPin);
  alternatorVoltageIn = analogRead(alternatorVoltagePin)*(40.0/1023.0);
  alternatorCurrentIn = analogRead(alternatorCurrentPin)*(2.5/1023.0);

  batteryVoltageIn =    battVI.getBusVoltage_V();
  batteryCurrentIn =    battVI.getCurrent_mA();
  systemLedTemperatureIn  = analogRead(ledTemperaturePin);
//  boostVoltageIn = analogRead(boostVoltagePin)*0.004882813/12.0*45.0;
  boostVoltageIn = analogRead(boostVoltagePin)*(18.75/1023.0);  // dac:819.2=4v 0.004882813V/div Vbbu/45*12=4 1024/5*4 33K and 12K 
  batteryChargeGood = digitalRead(chargeChargePin);
  batteryChargeDone = digitalRead(chargeDonePin);
}

// Threshold violations
void checkThresholds(){
  // Error level
  //  0 - OK
  //  1 - Warning, minimum brightness
  //  2 - Error, shutdown
  if ( batteryCurrentIn >= batteryCurrentMax )             errorLevel = 1;
  if ( alternatorCurrentIn >= alternatorCurrentMax )       errorLevel = 1;
  if ( alternatorVoltageIn >= alternatorVoltageMax )       errorLevel = 1;
  if ( systemLedTemperatureIn >= systemLedTemperatureHi )  errorLevel = 1;
  if ( systemLedTemperatureIn >= systemLedTemperatureMax ) errorLevel = 2;
  if ( batteryVoltageIn <= batteryVoltageMin )             errorLevel = 2;
}

// The audible alarm
void checkAudibleAlarm(){
  if ( errorLevel > 0 ){
    digitalWrite(alarmPin, HIGH);
  } else {
    digitalWrite(alarmPin, LOW);
  }
}

// Adjust the brightness level
void setMainLeds(){
  int increment = 0;
  // Act upon error levels if necessary
  // Errors cause a total shutdown
  if ( errorLevel == 2 ){
    systemLedLevelOut = 1;
  }
  // Warning sets LEDs to minimum brightness
  else if ( errorLevel == 1 ){
    systemLedLevelOut = systemLedLevelMin;
  }
  else if ( errorLevel == 0 ){
    // Normal brightness adjustment
    if ( alternatorVoltageIn < alternatorVoltageMin ) {
      systemLedLevelOut = systemLedLevelMin;
    } 
    else if ( alternatorVoltageIn >= alternatorVoltageMin && alternatorVoltageIn <= boostVoltageIn ) {
      systemLedLevelOut = map(alternatorVoltageIn*1000,alternatorVoltageMin*1000,boostVoltageIn*1000,systemLedLevelMin,systemLedLevelBbu);
    } 
    else {
      systemLedLevelOut = map(alternatorVoltageIn*1000,boostVoltageIn*1000,alternatorVoltageHi*1000,systemLedLevelBbu,systemLedLevelMax);
    }

    if ( systemLedLevelOut <= 1 ){                       // Capture if we've shut down the LED
      systemLedLevelOut = 1;                             // and make sure it shuts down
    }
    else if ( systemLedLevelOut > systemLedLevelMax ){   // If we're above the global max
      systemLedLevelOut = systemLedLevelMax;             // Then set output to maximum
    }
    else if ( systemLedLevelOut < systemLedLevelMin ){   // If we're below the global min
      systemLedLevelOut = systemLedLevelMin;             // Then set output to minimum
    }
  }
  // Write out the level to the LEDs
  if ( systemLedLevelOut >= dacRange ){        // If we're above LED1 max
    led1Level = dacRange;                      // Then set LED1 to max
  } else {
    led1Level = systemLedLevelOut;             // Otherwise set LED1 to value
  }
  if ( systemLedLevelOut <= led2Offset+ledDeadZoneLow ){      // If we're below LED2 min
    led2Level = ledDeadZoneLow;                             // Then set LED2 to off
  } else {
    led2Level = systemLedLevelOut-led2Offset;  // Otherwise set LED2 to value
  }
  Led1.setVoltage(dacRange - led1Level, false);
  Led2.setVoltage(dacRange - led2Level, false);
}

// Power status
void checkPowerStatus(){
// 0 - batt lo
// 1 - batt OK, alt lo
// 2 - batt OK, alt OK
// 3 - batt OK, alt OK, charge
// 4 - batt OK, alt OK, done
  if ( batteryVoltageIn > batteryVoltageMin ){
    powerStatus = 1;
    if ( alternatorVoltageIn >= alternatorVoltageMin ) {
      powerStatus = 2;
      if ( batteryChargeGood ){
        powerStatus = 3;
        if ( batteryChargeDone ){
          powerStatus = 4;
        }
      }
    }
  }
}

/*
The RGB indicator LED
Power Status
0 - Red (we never actually get here, because errorLevel 2 is the same place)
1 - Yel
2 - Grn
3 - Blu
4 - Wht

Error level
1 - Yel blink
2 - Red blink
*/
void checkIndicatorState(){                                  // Checks the timer and flips the LED state
  if ( millis() >= indicatorBlinkTime ){                     // If it's time
    indicatorBlinkState = !indicatorBlinkState;              // Flip the led state
    indicatorBlinkTime = millis() + indicatorBlinkInterval;  // Increment the timer
  }
}

void setIndicatorLed(){
  if ( errorLevel == 2 ){
    // Blink red
    checkIndicatorState();
    digitalWrite(indicatorRedPin, indicatorBlinkState);
    digitalWrite(indicatorGreenPin, LOW);
    digitalWrite(indicatorBluePin, LOW);
  }
  else if ( errorLevel == 1 ){
    // Blink yellow
    checkIndicatorState();
    digitalWrite(indicatorRedPin, indicatorBlinkState);
    digitalWrite(indicatorGreenPin, indicatorBlinkState);
    digitalWrite(indicatorBluePin, LOW);
  }
  else if ( errorLevel == 0 ){
    switch (powerStatus){
      case 1:
        // Yellow
        digitalWrite(indicatorRedPin, HIGH);
        digitalWrite(indicatorGreenPin, HIGH);
        digitalWrite(indicatorBluePin, LOW);
        break;
      case 2:
        // Green
        digitalWrite(indicatorRedPin, LOW);
        digitalWrite(indicatorGreenPin, HIGH);
        digitalWrite(indicatorBluePin, LOW);
        break;
      case 3:
        // Blue
        digitalWrite(indicatorRedPin, LOW);
        digitalWrite(indicatorGreenPin, LOW);
        digitalWrite(indicatorBluePin, HIGH);
        break;
      case 4:
        // White
        digitalWrite(indicatorRedPin, HIGH);
        digitalWrite(indicatorGreenPin, HIGH);
        digitalWrite(indicatorBluePin, HIGH);
        break;
    }
  }
}

// The main loop
void loop(){
  // Reset these each loop; events build them up during each pass
  errorLevel = 0;
  powerStatus = 0;
  // Bring in a new set of data
  readSensors();
  // Make sure we're running within bounds on all sensors
  checkThresholds();
  // Make noise if anything is out of tolerance
  checkAudibleAlarm();
  // Update the headlight
  setMainLeds();
  // human interface is slowed to once per displayUpdateInterval millis()
  if ( millis() >= displayUpdateTime + displayUpdateInterval ){
    displayUpdateTime = millis();
    // Gather data for the indicator LED
    checkPowerStatus();
    // Update the LED
    setIndicatorLed();
#ifdef DEBUG
    // And if we're in DEBUG, update serial
    updateSerial();
#endif
  }
}

// Debug output to serial
#ifdef DEBUG
void updateSerial(){
//Serial.print("Valt\tIalt\tVbat\tIbat\tVbbu\tNow\tOpt\tGap\tLED1\tLED2\tTled");
  Serial.print(alternatorVoltageIn); Serial.print("\t");
  Serial.print(alternatorCurrentIn); Serial.print("\t");
  Serial.print(batteryVoltageIn); Serial.print("\t");
  Serial.print(batteryCurrentIn); Serial.print("\t");
  Serial.print(boostVoltageIn); Serial.print("\t");
  Serial.print(systemImpedanceTotal); Serial.print("\t");
  Serial.print(systemImpedanceOpt); Serial.print("\t");
  Serial.print(systemImpedanceGap); Serial.print("\t");
  Serial.print(led1Level); Serial.print("\t");
  Serial.print(led2Level); Serial.print("\t");
  Serial.print(systemLedTemperatureIn);
  Serial.println();
}
#endif

