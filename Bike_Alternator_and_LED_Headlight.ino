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

// Measured values
float alternatorVoltageIn;   // Alternator output voltage (V)
float alternatorCurrentIn;   // Alternator output current (mA)
float batteryVoltageIn;      // Battery voltage (V)
float batteryCurrentIn;      // Battery backup drain current (mA)
int systemLedTemperatureIn;  // LED temperature (arbitrary)

// Thresholds and limits
float alternatorVoltageMax = 22.0;    // Increase LED brightness at or above this voltage
float alternatorVoltageBbu = 15.0;    // Voltage at which LED power transitions between alternator and battery backup
float alternatorVoltageMin = 5.0;     // Minimum alternator output voltage (V)
float alternatorCurrentMax = 3000.0;  // Maximum alternator output current (mA)
float batteryVoltageMin = 3.0;        // Minimum battery voltage (V)
float batteryCurrentMax = 2000.0;     // Maximum battery backup drain current (mA)

// Calibration and calculated values
int led1Level = 0;                 // PWM/DAC output value for LED1
int led2Level = 0;                 // PWM/DAC output value for LED2
int ledLevelMax = 4096;            // Maximum possible LED level
int ledLevelHi = 3996;             // Maximum useful LED level, above which the LED does not get any brighter. (high dead zone)
int ledLevelLo = 100;              // Minimum useful LED level, below which the LED is off (low dead zone).
int ledLevelMin = 0;               // Minimum possible LED level
int systemLedLevelOut = 0;         // System LED brightness level 
int systemLedLevelMax = 7168;      // Maximum system LED brightness. LED levels are mapped into this so that LED1 is almost full bright before LED2 starts to light.
int systemLedLevelMin = 440;       // Minimum LED1 brightness while system is powered on (roughly 75mA or 1W)
float systemImpedanceOpt = 34.0;   // Optimum impedance load on the alternator, in Ohms, calculated based on alternator output voltage
float systemImpedanceGap = 3.0;    // Dead zone +/- systemImpedanceOpt value. Reduces flicker. 
float systemImpedanceNow = 100.0;  // Start the calculated impedance at a safe value.
float systemOverhead = 4.2;        // System idle power draw in Watts
byte systemRunMode = 0;            // Used for debugging
byte ledLevelIncrement = 1;        // LED PWM output increment value

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

void determineRunParams(){
  if ( alternatorVoltageIn > alternatorVoltageMax ){  // Above max alternator output voltage
    systemImpedanceOpt=10;
    systemImpedanceGap=1;
    systemImpedanceNow = alternatorVoltageIn/alternatorCurrentIn;
    systemRunMode=3;
  }
  if ( alternatorVoltageIn > alternatorVoltageBbu && alternatorVoltageIn < alternatorVoltageMax ){ // Optimum range
    systemImpedanceOpt=20;
    systemImpedanceGap=2;
    systemImpedanceNow = alternatorVoltageIn/alternatorCurrentIn;
    systemRunMode=2;
  }
  if ( alternatorVoltageIn > alternatorVoltageMin && alternatorVoltageIn < alternatorVoltageBbu ){ // Within the BBU and charging
    systemImpedanceOpt=30;
    systemImpedanceGap=3;
    systemImpedanceNow = (alternatorVoltageIn*alternatorVoltageIn)/((alternatorVoltageIn/alternatorCurrentIn)+(batteryVoltageIn/batteryCurrentIn));
    systemRunMode=1;
  }
  if ( alternatorVoltageIn < alternatorVoltageMin ){ // Below min alternator output voltage
    systemImpedanceOpt=40;
    systemImpedanceGap=4;
    systemImpedanceNow = batteryVoltageIn/batteryCurrentIn;
    systemRunMode=0;
  }
}

void adjustLedPwm(){
  if ( systemImpedanceNow >= (systemImpedanceOpt + systemImpedanceGap) ){ increaseLedLevel(); }
  if ( systemImpedanceNow <= (systemImpedanceOpt - systemImpedanceGap) ){ decreaseLedLevel(); }
}

void setMainLeds(){
  if ( systemLedLevelOut > systemLedLevelMax ){ systemLedLevelOut = systemLedLevelMax; }
  if ( systemLedLevelOut < systemLedLevelMin ){ systemLedLevelOut = systemLedLevelMin; }
  led1Level = systemLedLevelOut;
  led2Level = systemLedLevelOut - (ledLevelMax/4*3);
  if ( led1Level > ledLevelMax ){ led1Level = ledLevelMax; }
  if ( led2Level < ledLevelMin ){ led2Level = ledLevelMin; }
  Led1.setVoltage(led1Level, false);
  Led2.setVoltage(led2Level, false);
}

void increaseLedLevel(){
  if ( systemLedLevelOut <= systemLedLevelMax ){
    systemLedLevelOut = systemLedLevelOut + ledLevelIncrement;
  }
  setMainLeds();
}

void decreaseLedLevel(){ 
  if ( systemLedLevelOut >= systemLedLevelMin ){
    systemLedLevelOut = systemLedLevelOut - ledLevelIncrement; 
  }
  setMainLeds();
}

void checkBattOverCurrent(){
  while ( batteryCurrentIn >= batteryCurrentMax ){
    decreaseLedLevel();
    readSensors();
  }
}

void checkAltOverVoltage(){
  while ( alternatorVoltageIn >= alternatorVoltageMax ){
    increaseLedLevel();
    readSensors();
  }
}

void checkLedOverTemp(){
  
}

void loop(){
  readSensors();
  checkBattOverCurrent();
  checkAltOverVoltage();
  checkLedOverTemp();
  determineRunParams();
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

