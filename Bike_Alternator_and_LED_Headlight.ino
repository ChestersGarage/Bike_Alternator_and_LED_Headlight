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
float alternatorVoltageMax = 25.9;     // Absolute maximum safe alternator voltage
float alternatorVoltageHi = 22.0;     // Increase LED brightness at or above this voltage
float alternatorVoltageBbu = 15.0;    // Voltage at which LED power transitions between alternator and battery backup
float alternatorVoltageMin = 5.0;     // Minimum alternator output voltage (V)
float alternatorCurrentIn;            // Alternator output current (mA)
float alternatorCurrentMax = 3000.0;  // Maximum alternator output current (mA)
boolean altOverVolt = false;          // True if alternator voltage is too high

// Battery
float batteryVoltageIn;            // Battery voltage (V)
float batteryVoltageMin = 3.0;     // Minimum battery voltage (V)
float batteryCurrentIn;            // Battery backup drain current (mA)
float batteryCurrentMax = 2000.0;  // Maximum battery backup drain current (mA)
boolean battOverCurr = false;      // True if battery current is too high

// LED Temperature
int systemLedTemperatureIn;         // LED temperature ADC scale
int systemLedTemperatureMax = 660;  // Maximum LED temperature
int systemLedTemperatureHi = 650;   // High water mark LED temperature.  Decrease brightness to stay below
boolean ledOverTemp = false;        // True if LED temperature is too high

// LEDs and DACs
// RCD-24 driver dimming range: 100%=0.13V - 0%=4.5V
// DAC output range 0-4096, 5V VCC
// Dimming is on an inverted scale: 4096=off, 0=max
// Useful dimming range in terms of DAC: 106 - 3686
int led1Level;                 // PWM/DAC output value for LED1
long led2Level;                 // PWM/DAC output value for LED2
int dacRange = 4096;               // Resolution of the LED dimming output
long led2Offset = dacRange*75/100;
int ledLevelHiDz = 106;            // Dead zone at the brightest LED level
int ledLevelLoDz = 410;            // Dead zone at the lowest LED level
int systemLedLevelOut = 0;         // System LED brightness level 
long systemLedRange = dacRange*175/100;         // Maximum system LED brightness. LED levels are mapped into this so that LED1 is almost full bright before LED2 starts to light.
byte ledLevelIncrement = 1;        // LED PWM output increment value

// System load and impedance
float systemImpedanceOpt = 34.0;   // Optimum impedance load on the alternator, in Ohms, calculated based on alternator output voltage
float systemImpedanceGap = 3.0;    // Dead zone +/- systemImpedanceOpt value. Reduces flicker. 
float systemImpedanceNow = 100.0;  // Start the calculated impedance at a safe value.
float systemOverhead = 4.2;        // System idle power draw in Watts
#ifdef DEBUG
byte systemRunMode = 0;            // Used for debugging
#endif
//#define DEBUG

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

void readSensors(){
  alternatorVoltageIn  = altVI.getBusVoltage_V();
  alternatorCurrentIn  = altVI.getCurrent_mA();
  batteryVoltageIn = battVI.getBusVoltage_V();
  batteryCurrentIn = battVI.getCurrent_mA();
  systemLedTemperatureIn  = analogRead(ledTemperaturePin);
}

void calcImpedanceParams(){
  systemImpedanceNow = (alternatorVoltageIn*alternatorVoltageIn)/((alternatorVoltageIn*alternatorCurrentIn)+(batteryVoltageIn*batteryCurrentIn));
  if ( alternatorVoltageIn > alternatorVoltageHi ){  // Above max alternator output voltage
    systemImpedanceOpt=10;
    systemImpedanceGap=1;
#ifdef DEBUG
    systemRunMode=3;
#endif
  }
  if ( alternatorVoltageIn > alternatorVoltageBbu && alternatorVoltageIn < alternatorVoltageHi ){ // Optimum range
    systemImpedanceOpt=20;
    systemImpedanceGap=2;
#ifdef DEBUG
    systemRunMode=2;
#endif
  }
  if ( alternatorVoltageIn > alternatorVoltageMin && alternatorVoltageIn < alternatorVoltageBbu ){ // Within the BBU and charging
    systemImpedanceOpt=30;
    systemImpedanceGap=3;
#ifdef DEBUG
    systemRunMode=1;
#endif
  }
  if ( alternatorVoltageIn < alternatorVoltageMin ){ // Below min alternator output voltage
    systemImpedanceOpt=40;
    systemImpedanceGap=4;
#ifdef DEBUG
    systemRunMode=0;
#endif
  }
}

void adjustLedPwm(){
  if ( systemImpedanceNow >= (systemImpedanceOpt + systemImpedanceGap) ){ increaseLedLevel(); }
  if ( systemImpedanceNow <= (systemImpedanceOpt - systemImpedanceGap) ){ decreaseLedLevel(); }
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
  Led1.setVoltage(dacRange-led1Level, false);
  Led2.setVoltage(dacRange-led2Level, false);
}

void increaseLedLevel(){
  if ( systemLedLevelOut >= systemLedRange-ledLevelHiDz ){      // If we're above the global HDZ
    systemLedLevelOut = systemLedRange;                         // Then set output to maximum
  } else {
    systemLedLevelOut = systemLedLevelOut + ledLevelIncrement;  // Otherwise increase the output level
  }
  setMainLeds();
}

void decreaseLedLevel(){ 
  if ( systemLedLevelOut <= ledLevelLoDz ){                     // If we're below the global LDZ
    systemLedLevelOut = 1;                                      // Then set output to minimum
  } else {
    systemLedLevelOut = systemLedLevelOut - ledLevelIncrement;  // Otherwise decrease the output level
  }
  setMainLeds();
}

void checkBattOverCurrent(){
  if ( batteryCurrentIn >= batteryCurrentMax ){
    decreaseLedLevel();
    battOverCurr = true;
  } else {
    battOverCurr = false;
  }
}

void checkAltOverVoltage(){
  if ( alternatorVoltageIn >= alternatorVoltageMax ){
    increaseLedLevel();
    altOverVolt = true;
  } else {
    altOverVolt = false;
  }
}

void checkLedOverTemp(){
  if ( systemLedTemperatureIn >= systemLedTemperatureMax ){
    decreaseLedLevel();
    ledOverTemp = true;
  } else if ( systemLedTemperatureIn >= systemLedTemperatureHi ){
    decreaseLedLevel();
    ledOverTemp = false;
  } else {
    ledOverTemp = false;
  }
}

void checkAlarm(){
  if ( battOverCurr || altOverVolt || ledOverTemp ){
    digitalWrite(alarmPin, HIGH);
  } else {
    digitalWrite(alarmPin, LOW);
  }
}

void loop(){
  readSensors();
  checkAltOverVoltage();
  checkBattOverCurrent();
  checkLedOverTemp();
  checkAlarm();
  calcImpedanceParams();
  adjustLedPwm();
#ifdef DEBUG
  updateSerial();
#endif
}

#ifdef DEBUG
void updateSerial(){
  Serial.print("Runmode:      "); Serial.println(systemRunMode);
  Serial.print("Impedance:    "); Serial.println(systemImpedanceNow);
  Serial.print("LED1:         "); Serial.println(led1Level);
  Serial.print("LED2:         "); Serial.println(led2Level);
  Serial.print("Alt Voltage:  "); Serial.print(alternatorVoltageIn); Serial.println("\tV");
  Serial.print("Alt Current:  "); Serial.print(alternatorCurrentIn); Serial.println("\tmA");
  Serial.print("Batt Voltage: "); Serial.print(batteryVoltageIn); Serial.println("\tV");
  Serial.print("Batt Current: "); Serial.print(batteryCurrentIn); Serial.println("\tmA");
  Serial.print("LED Temp:     "); Serial.println(systemLedTemperatureIn);
  Serial.println();
}
#endif

