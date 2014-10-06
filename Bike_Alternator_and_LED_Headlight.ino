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
Adafruit_INA219 altVI(0x44);
Adafruit_MCP4725 Led1;
Adafruit_MCP4725 Led2;

// Comment out to disable debug output on serial
#define DEBUG

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
// 11
// chg_err 12
// charging 13

#define ledTemperaturePin A0
// A1
// A2
// A3
// A4
// A5

// Alternator
float alternatorVoltageIn;            // Alternator output voltage (V)
float alternatorVoltageMax = 25.5;    // Above this value (V), the LEDs are forced to maximum brightness and the alarm is sounded
float alternatorVoltageMin = 6.5;     // Below this value (V), the LEDs are forced to minimum brightness
float alternatorCurrentIn;            // Alternator output current (mA)
float alternatorCurrentMax = 3000.0;  // Maximum alternator output current (mA)
boolean altOverVolt = false;          // True if alternator voltage is too high
boolean altOverCurr = false;          // True if alternator current is too high

// Battery
float batteryVoltageIn;            // Battery voltage (V)
float batteryVoltageMin = 3.0;     // Below this value (V), LEDs are forced to OFF and the alarm is sounded
float batteryCurrentIn;            // Battery backup drain current (mA)
float batteryCurrentMax = 2000.0;  // Above this value (mA), the LED brightness is decreased and the alarm is sounded
boolean battOverCurr = false;      // True if battery current is too high
boolean battUnderVolt = false;     // True if battery voltage too low

// LED Temperature
int systemLedTemperatureIn;         // LED temperature ADC scale
int systemLedTemperatureMax = 660;  // Maximum safe LED temperature
int systemLedTemperatureHi = 650;   // High water mark LED temperature.  Decrease brightness to stay below
boolean ledOverTemp = false;        // True if LED temperature is beyond safe level
boolean ledHighTemp = false;        // True if LED temperature is too high

// Main LEDs and DACs
// RCD-24 driver dimming range: 100%=0.13V - 0%=4.5V
// DAC output range 0-4095, 5V VCC
// Dimming is on an inverted scale: 4095=off, 0=max
// Useful dimming range in terms of DAC: 106 - 3686
long dacRange = 4096;                     // Resolution of the LED dimming output
long led1Level;                           // Output value for LED1
long led2Level;                          // Output value for LED2
long led2Offset = dacRange*75/100;       // LED2 starts to turn on after about 75% brightness on LED1
long ledLevelHiDz = 106;                  // Dead zone at the brightest LED level
long ledLevelLoDz = 1150;                  // Dead zone at the lowest LED level
long systemLedLevelOut = ledLevelLoDz;    // System LED brightness level 
long systemLedRange = dacRange*175/100;  // Maximum system LED brightness. LED levels are mapped into this so that LED1 is almost full bright before LED2 starts to light.
long ledLevelIncrement = 1;              // LED output increment value

// System impedance
float systemImpedanceOpt;         // The calculated optimum system impedance, in Ohms.
float systemImpedanceGap;         // systemImpedanceNow allowed to be this much less than systemImpedanceOpt. Reduces flicker.
float systemImpedanceNow;         // The calculated current system impedance.
float systemImpedanceRef = 33.0;  // LED brightness adjusted per "systemImpedanceOpt = systemImpedanceRef-(0.5*(alternatorVoltageIn-alternatorVoltageMin))"

#ifdef DEBUG
int displayUpdateInterval = 250;
long displayUpdateTime = 0;
#endif

void setup() {
#ifdef DEBUG
  Serial.begin(115200);
#endif
  battVI.begin();
  altVI.begin();
  Led1.begin(0x63);
  Led2.begin(0x62);
  pinMode(alarmPin, OUTPUT);
  pinMode(indicatorRedPin, OUTPUT);
  pinMode(indicatorGreenPin, OUTPUT);
  pinMode(indicatorBluePin, OUTPUT);
  pinMode(led1EnablePin, OUTPUT);
  pinMode(led2EnablePin, OUTPUT);
  digitalWrite(alarmPin, LOW);
  digitalWrite(indicatorRedPin, LOW);
  digitalWrite(indicatorGreenPin, LOW);
  digitalWrite(indicatorBluePin, LOW);
  digitalWrite(led1EnablePin, HIGH);
  digitalWrite(led2EnablePin, HIGH);
  Led1.setVoltage(4095, true);
  Led2.setVoltage(4095, true);
}

// Read all sensor values in a single pass
void readSensors(){
  alternatorVoltageIn  = altVI.getBusVoltage_V();
  alternatorCurrentIn  = altVI.getCurrent_mA();
  batteryVoltageIn = battVI.getBusVoltage_V();
  batteryCurrentIn = battVI.getCurrent_mA();
  systemLedTemperatureIn  = analogRead(ledTemperaturePin);
}

// Manipulate the LED overall brightness level, based on impedance and threshold violations
void adjustSystemLedLevel(){
  if ( battUnderVolt || ledOverTemp ){
    forceLedLevelOff();
  }
  else if ( alternatorVoltageIn <= alternatorVoltageMin ){
    setLedLevelMinimum();
  } 
  else {
    // System impedance value is calculated from the combination of alternator power and battery power consumption.
    // This means the calculation NEVER actually determines the real impedance against the alternator.
    // But it's within a valid range and more suitable for determining LED brightness.
    systemImpedanceNow = (alternatorVoltageIn * alternatorVoltageIn) / ((alternatorVoltageIn * (alternatorCurrentIn/1000)) + (batteryVoltageIn * (batteryCurrentIn/1000)));
    // This creates an impedance curve that reduces the optimum impedance as the alternator voltage increases.
    systemImpedanceOpt = systemImpedanceRef - (0.5 * (alternatorVoltageIn - alternatorVoltageMin));
    // And the allowable deviation from optimum is +0 to -10%
    systemImpedanceGap = systemImpedanceOpt / 10;
    if ( systemImpedanceNow <= (systemImpedanceOpt - systemImpedanceGap) || altOverCurr || battOverCurr || ledHighTemp ){
      decreaseLedLevel();
    }
    else if ( systemImpedanceNow >= systemImpedanceOpt || altOverVolt ){
      increaseLedLevel();
    }
  }
}

// Apply the overall brightness level to the LEDs
void increaseLedLevel(){
  if ( systemLedLevelOut >= systemLedRange - ledLevelHiDz ){    // If we're above the global HDZ
    systemLedLevelOut = systemLedRange;                         // Then set output to maximum
  } else {
    systemLedLevelOut = systemLedLevelOut + ledLevelIncrement;  // Otherwise increase the output level
  }
  setMainLeds();
}

void decreaseLedLevel(){ 
  if ( systemLedLevelOut <= ledLevelLoDz ){                     // If we're below the global LDZ
    systemLedLevelOut = ledLevelLoDz;                           // Then set output to minimum
  } else {
    systemLedLevelOut = systemLedLevelOut - ledLevelIncrement;  // Otherwise decrease the output level
  }
  setMainLeds();
}

void setLedLevelMinimum(){
  systemLedLevelOut = ledLevelLoDz;
  setMainLeds();
}

void forceLedLevelOff(){
  systemLedLevelOut = 1;
  setMainLeds();
}

void setMainLeds(){
  if ( systemLedLevelOut >= dacRange-ledLevelHiDz ){    // If we're above LED1 HDZ
    led1Level = dacRange;                               // The set LED1 to max
  } else {
    led1Level = systemLedLevelOut;                      // Otherwise set LED1 to value
  }
  if ( systemLedLevelOut <= led2Offset+ledLevelLoDz ){  // If we're below LED2 LDZ
    led2Level = 1;                                      // Then set LED2 to off
  } else {
    led2Level = systemLedLevelOut-led2Offset;           // Otherwise set LED2 to value
  }
  Led1.setVoltage(dacRange - led1Level, false);
  Led2.setVoltage(dacRange - led2Level, false);
}

// Threshold violations
void checkBattOverCurrent(){
  if ( batteryCurrentIn > batteryCurrentMax ){
    battOverCurr = true;
  } else {
    battOverCurr = false;
  }
}

void checkBattUnderVoltage(){
  if ( batteryVoltageIn < batteryVoltageMin ){
    battUnderVolt = true;
  } else {
    battUnderVolt = false;
  }
}

void checkAltOverVoltage(){
  if ( alternatorVoltageIn >= alternatorVoltageMax ){
    altOverVolt = true;
  } else {
    altOverVolt = false;
  }
}

void checkAltOverCurrent(){
  if ( alternatorCurrentIn >= alternatorCurrentMax ){
    altOverCurr = true;
  } else {
    altOverCurr = false;
  }
}

void checkLedOverTemp(){
  if ( systemLedTemperatureIn >= systemLedTemperatureMax ){
    ledOverTemp = true;
    ledHighTemp = false;
  } else if ( systemLedTemperatureIn >= systemLedTemperatureHi ){
    ledOverTemp = false;
    ledHighTemp = true;
  } else {
    ledOverTemp = false;
    ledHighTemp = false;
  }
}

// The audible alarm
void checkAlarm(){
  if ( altOverVolt || altOverCurr || battOverCurr || battUnderVolt || ledOverTemp ){
    digitalWrite(alarmPin, HIGH);
  } else {
    digitalWrite(alarmPin, LOW);
  }
}

// The RGB indicator LED
void setIndicatorLed(){
  // We have a RGB LED to play with
  // Normal operation is green
  digitalWrite(indicatorRedPin, LOW); digitalWrite(indicatorGreenPin, HIGH); digitalWrite(indicatorBluePin, LOW);
  // On battery only, yellow
  if ( alternatorVoltageIn <= alternatorVoltageMin ){ digitalWrite(indicatorRedPin, HIGH); digitalWrite(indicatorGreenPin, HIGH); digitalWrite(indicatorBluePin, LOW); }
  // Alarm condition is red
  if ( altOverVolt || altOverCurr || battOverCurr || battUnderVolt || ledOverTemp ){ digitalWrite(indicatorRedPin, HIGH); digitalWrite(indicatorGreenPin, LOW); digitalWrite(indicatorBluePin, LOW); }
}

// The main loop
void loop(){
  digitalWrite(led1EnablePin, LOW);
  digitalWrite(led2EnablePin, LOW);
  readSensors();
  checkAltOverVoltage();
  checkBattOverCurrent();
  checkBattUnderVoltage();
  checkAltOverCurrent();
  checkLedOverTemp();
  checkAlarm();
  adjustSystemLedLevel();
  setIndicatorLed();
#ifdef DEBUG
  updateSerial();
#endif
}

// Debug output to serial
#ifdef DEBUG
void updateSerial(){
  if ( millis() >= displayUpdateTime + displayUpdateInterval ){
    displayUpdateTime = millis();
    Serial.print("Now:  "); Serial.print(systemImpedanceNow); Serial.print("\tOpt:  "); Serial.print(systemImpedanceOpt); Serial.print("\tGap:  "); Serial.println(systemImpedanceGap);
    Serial.print("LED1: "); Serial.print(dacRange-led1Level); Serial.print("\tLED2:         "); Serial.println(dacRange-led2Level);
    Serial.print("AltV: "); Serial.print(alternatorVoltageIn); Serial.print(" V"); Serial.print("\tAltC: "); Serial.print(alternatorCurrentIn); Serial.println(" mA");
    Serial.print("BatV: "); Serial.print(batteryVoltageIn); Serial.print(" V"); Serial.print("\tBatC: "); Serial.print(batteryCurrentIn); Serial.println(" mA");
    Serial.print("LEDT: "); Serial.println(systemLedTemperatureIn);
    Serial.println();
  }
}
#endif

