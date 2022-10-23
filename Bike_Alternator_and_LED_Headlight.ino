/*
  Bike Alterator LED Headlight control system.
  Runs on an Arduino/ATMEGA328.
  
  * LED brightness based on alternator output voltage:
    - Total system load, including uC and battery charging needs.
    - Maximum battery drain current when alternator output too low
  * Main LED overtemperature protection flickers LEDs to keep them cool.
  * Alternator output voltage limited by crowbar FET.
  * Tuned to optimum system impedance on the alternator.
  * (Broken) Alarm sound if any parameters are beyond critical thresholds:
    - Battery voltage too low.
    - Battery backup current too high.
    - Alternator current too high.
    - Alternator output voltage too high (analog trigger, not uC).
    - LED temperature too high.
    
  Schematic compatibility: "3.2 Headlight.sch"
*/
#include <Wire.h>
#include <Adafruit_MCP4725.h>
#include <Adafruit_INA219.h>
#include <Adafruit_NeoPixel.h>

Adafruit_INA219 battVI(0x41);
Adafruit_MCP4725 Led1;
Adafruit_MCP4725 Led2;

// Comment out to disable debug output on serial
//#define DEBUG

// Serial on 0
// Serial on 1
#define indicatorLedsPin 2
// 3
// 4
#define led1EnablePin 5
#define led2EnablePin 6
#define alarmPin 7
#define chargePowerPin 8
#define chargeChargePin 9
#define chargeDonePin 10
// 11
// 12
// 13

#define ledTemperaturePin A0
#define boostVoltagePin A1
#define alternatorCurrentPin A2
#define alternatorVoltagePin A3
// A4 (SDA)
// A5 (SCL)

Adafruit_NeoPixel indicatorLeds = Adafruit_NeoPixel(3, indicatorLedsPin, NEO_GRB + NEO_KHZ800);

// Alternator
float alternatorVoltageIn;            // Alternator output voltage (V)
float alternatorVoltageMax = 33.9;    // Above this value (V), the LEDs are forced to maximum brightness and the alarm is sounded
float alternatorVoltageHi = 30.0;     // Main LEDs at max bright
float alternatorVoltageVpcc = 7.1;    // Below this value (V), the LEDs are forced to minimum brightness
float alternatorVoltageMin = 4.7;     // Minimum usable voltage
float alternatorCurrentIn;            // Alternator output current (mA)
float alternatorCurrentMax = 2.5;     // Maximum alternator output current (mA)
byte alternatorStatus = 0;
//float alternatorPowerIn;
// Smoothing
const int altReadCount = 11;
float altReadings[altReadCount]; // the readings from the analog input
int altReadIndex = 0;          // the index of the current reading
float altReadTotal = 0;          // the running total
//int altReadAvg = 0;            // the average

// Battery
float batteryVoltageIn;            // Battery voltage (V)
float batteryVoltageMin = 3.0;     // Below this value (V), LEDs are forced to OFF and the alarm is sounded
float batteryVoltageLow = 3.4;     // Battery is low and needs to charge
float batteryVoltageMid = 3.8;     // Mid-point battery voltage
float batteryVoltageMax = 4.21;     // Battery is charged and/or charger is bypassing battery
float batteryCurrentIn;            // Battery backup drain current (mA)
float batteryCurrentMax = 1750.0;  // Above this value (mA), the LED brightness is decreased and the alarm is sounded
byte batteryStatus = 0;
boolean chargeCharge;
boolean chargeDone;
boolean chargePower;
byte chargeStatus = 0;
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
long led1Level;                             // Output value for LED1
long led2Level;                             // Output value for LED2
long led2Offset = dacRange*75/100;          // LED2 starts to turn on after about 75% brightness on LED1
long systemLedLevelMax = dacRange*175/100;  // Maximum system LED brightness. LED levels are mapped into this so that LED1 is almost full bright before LED2 starts to light.
long systemLedLevelBbu = 2500;              // This must be tuned on the test rig
long systemLedLevelMin = 1150;              // Min bright, dead zone at the lowest LED level;
long ledDeadZoneLow = 900;
long systemLedLevelOut = systemLedLevelMin; // System LED brightness level

// Alarm
int alarmAudibleDuration = 1000;  // How long the alarm sounds each time it's triggered
uint32_t alarmAudibleTime = 0;        // The time in millis() when the alarm went audible
boolean alarmState = LOW;

// System
byte errorLevel = 0;
byte powerStatus = 0;
int displayUpdateInterval = 250;
long displayUpdateTime = 0;
long indicatorBlinkInterval = 250;  
long indicatorBlinkTime = 0;
boolean indicatorBlinkState = LOW;
uint32_t indOff;
uint32_t indRed;
uint32_t indYel;
uint32_t indGreen;
uint32_t indBlue;
uint32_t indWhite;

void setup() {
#ifdef DEBUG
  Serial.begin(115200);
#endif
  indicatorLeds.begin();
  indOff   = indicatorLeds.Color(0,0,0);
  indRed   = indicatorLeds.Color(5,0,0);
  indYel   = indicatorLeds.Color(5,5,0);
  indGreen = indicatorLeds.Color(0,5,0);
  indBlue  = indicatorLeds.Color(0,0,5);
  indWhite = indicatorLeds.Color(5,8,6);
  battVI.begin();
  Led1.begin(0x63);
  Led2.begin(0x62);
  pinMode(alarmPin, OUTPUT);
  pinMode(led1EnablePin, OUTPUT);
  pinMode(led2EnablePin, OUTPUT);
  pinMode(boostVoltagePin, INPUT);
  pinMode(ledTemperaturePin, INPUT);
  pinMode(chargePowerPin, INPUT);
  pinMode(chargeChargePin, INPUT);
  pinMode(chargeDonePin, INPUT);
  digitalWrite(alarmPin, LOW);
  digitalWrite(led1EnablePin, LOW);
  digitalWrite(led2EnablePin, LOW);
  Led1.setVoltage(4095, true);
  Led2.setVoltage(4095, true);
  for (int thisReading = 0; thisReading < altReadCount; thisReading++) {
    altReadings[thisReading] = 0;
  }

#ifdef DEBUG
  //Serial.println("AS\tBS\tCS\tValt\tIalt\tVbat\tIbat\tVbbu\tLED1\tLED2\tTled");
  Serial.println("Valt\tIalt\tVbat\tIbat\tVbbu\tLED1\tLED2\tTled");
#endif
}

// Read all sensor values in a single pass
void readSensors(){
  alternatorVoltageIn = smoothAltVoltage();
  alternatorCurrentIn = analogRead(alternatorCurrentPin)*(2.5/1023.0);
  batteryVoltageIn = battVI.getBusVoltage_V();
  batteryCurrentIn = battVI.getCurrent_mA();
  systemLedTemperatureIn  = analogRead(ledTemperaturePin);
  boostVoltageIn = analogRead(boostVoltagePin)*(19.3/1023.0);  // dac:819.2=4v 0.004882813V/div Vbbu/45*12=4 1024/5*4 33K and 12K 
  chargeCharge = digitalRead(chargeChargePin);
  chargeDone = digitalRead(chargeDonePin);
  chargePower = digitalRead(chargePowerPin);
}

float smoothAltVoltage(){
  altReadTotal = altReadTotal - altReadings[altReadIndex];
  altReadings[altReadIndex] = analogRead(alternatorVoltagePin)*(40.0/1023.0);
  altReadTotal = altReadTotal + altReadings[altReadIndex];
  altReadIndex = altReadIndex + 1;
  if (altReadIndex >= altReadCount) {
    altReadIndex = 0;
  }
  return altReadTotal / altReadCount;
}

// Threshold violations
void checkThresholds(){
  // Error level
  //  0 - OK
  //  1 - Overtemp warning, alarm
  //  2 - Overdrive error, max bright, alarm
  //  3 - Overload, max temp error, min bright, alarm
  if ( systemLedTemperatureIn >= systemLedTemperatureHi )  errorLevel = 1;
  if ( alternatorVoltageIn >= alternatorVoltageMax )       errorLevel = 2;
  if ( alternatorCurrentIn >= alternatorCurrentMax )       errorLevel = 2;
  if ( batteryCurrentIn >= batteryCurrentMax )             errorLevel = 3;
  if ( systemLedTemperatureIn >= systemLedTemperatureMax ) errorLevel = 3;
  if ( batteryVoltageIn <= batteryVoltageMin )             errorLevel = 3;
}

// The audible alarm
void checkAudibleAlarm(){
  if ( alarmState ) {
    if ( millis() >= (alarmAudibleTime + alarmAudibleDuration) && errorLevel == 0 ){
      alarmState = LOW;
    }
  } else {
    if ( errorLevel > 0 ){
      alarmAudibleTime = millis();
      alarmState = HIGH;
    }
  }
  digitalWrite(alarmPin, alarmState);
}

// Adjust the brightness level
void setMainLeds(){
  if ( errorLevel == 2 ){  // Overdrive error
    systemLedLevelOut = systemLedLevelMax;
  } 
  else if ( errorLevel == 3 ){  // Overload error
    systemLedLevelOut = systemLedLevelMin;
  }
  else if ( errorLevel <= 1 ){  // Warning or OK
    // Normal brightness adjustment
    if ( alternatorVoltageIn < alternatorVoltageVpcc ) {
      systemLedLevelOut = systemLedLevelMin; // Below Vpcc, min bright
    }
    // Between Vpcc and boost voltage
    else if ( alternatorVoltageIn >= alternatorVoltageVpcc && alternatorVoltageIn <= boostVoltageIn ) {
      systemLedLevelOut = map(alternatorVoltageIn*1000,alternatorVoltageVpcc*1000,boostVoltageIn*1000,systemLedLevelMin,systemLedLevelBbu);
    }
    // Everything above boost voltage
    else {
      systemLedLevelOut = map(alternatorVoltageIn*1000,boostVoltageIn*1000,alternatorVoltageHi*1000,systemLedLevelBbu-350,systemLedLevelMax);
    }

    if ( systemLedLevelOut > systemLedLevelMax ){   // If we're above the global max
      systemLedLevelOut = systemLedLevelMax;             // Then set output to maximum
    } else if ( systemLedLevelOut < systemLedLevelMin ){   // If we're below the global min
      systemLedLevelOut = systemLedLevelMin;             // Then set output to minimum
    }
  }
  // Write out the level to the LEDs
  if ( systemLedLevelOut >= dacRange ){        // If we're above LED1 max
    led1Level = dacRange;                      // Then set LED1 to max
  }
  else {
    led1Level = systemLedLevelOut;             // Otherwise set LED1 to value
  }
  if ( systemLedLevelOut <= led2Offset+ledDeadZoneLow ){      // If we're below LED2 min
    led2Level = ledDeadZoneLow;                             // Then set LED2 to off
  }
  else {
    led2Level = systemLedLevelOut-led2Offset;  // Otherwise set LED2 to value
  }
  Led1.setVoltage(dacRange - led1Level, false);
  Led2.setVoltage(dacRange - led2Level, false);
}

// Power status
void checkPowerStatus(){
/* Alternator Status
  0 - Red < ValtMin 4.7
  1 - Yel ValtMin 4.7 -ValtVpcc 6.9
  2 - Grn ValtVpcc 6.9 -Vbbu 13.3
  3 - Blu Vbbu 13.3 - ValtHi 30
  4 - Wht ValtHi 30 - ValtMax 33.9
  5 - ERR > Vmax 33.9 (Red blink)
*/
  if ( alternatorVoltageIn <= alternatorVoltageMin ){
    alternatorStatus = 0;
  } else if ( alternatorVoltageIn > alternatorVoltageMin && alternatorVoltageIn <= alternatorVoltageVpcc ){
    alternatorStatus = 1;
  } else if ( alternatorVoltageIn > alternatorVoltageVpcc && alternatorVoltageIn <= boostVoltageIn ){
    alternatorStatus = 2;
  } else if ( alternatorVoltageIn > boostVoltageIn && alternatorVoltageIn <= alternatorVoltageHi ){
    alternatorStatus = 3;
  } else if ( alternatorVoltageIn > alternatorVoltageHi && alternatorVoltageIn <= alternatorVoltageMax ){
    alternatorStatus = 4;
  } else if ( alternatorVoltageIn > alternatorVoltageMax ){
    alternatorStatus = 5;
  }
/* Battery Status
  0 - Red blink < batteryVoltageMin (3.5V)
  1 - Yel batteryVoltageMin (3.5V)-batteryVoltageLow (3.7V)
  2 - Grn batteryVoltageLow (3.7V)-batteryVoltageMid (3.9V) 
  3 - Blu batteryVoltageMid (3.9V)-batteryVoltageMax (4.2V)
  4 - Wht > 4.2V
*/
  if ( batteryVoltageIn <= batteryVoltageMin ){
    batteryStatus = 0;
  } else if ( batteryVoltageIn > batteryVoltageMin && batteryVoltageIn <= batteryVoltageLow ){
    batteryStatus = 1;
  } else if ( batteryVoltageIn > batteryVoltageLow && batteryVoltageIn <= batteryVoltageMid ){
    batteryStatus = 2;
  } else if ( batteryVoltageIn > batteryVoltageMid && batteryVoltageIn <= batteryVoltageMax ){
    batteryStatus = 3;
  } else if ( batteryVoltageIn > batteryVoltageMax ){
    batteryStatus = 4;
  } 
/* Charge Status
  0 - Red No power
  1 - Yel Power, no charge
  2 - ERR Battery < 3.1V (Red blink)
  3 - Grn Charge
  4 - Wht Done charging
  5 - ERR Temperature (Red blink)
*/
  if ( chargePower == HIGH && chargeCharge == HIGH && chargeDone == HIGH ){
    chargeStatus = 0;  // No power
  }  else if ( chargePower == LOW  && chargeCharge == HIGH && chargeDone == HIGH ){
    chargeStatus = 1;  // Power, no charge
  }  else if ( chargePower == HIGH  && chargeCharge == LOW && chargeDone == HIGH ){
    chargeStatus = 2;  // ERROR (Battery < 3.1V)
  }  else if ( chargePower == LOW && chargeCharge == LOW && chargeDone == HIGH ){
    chargeStatus = 3;  // Charging
  }  else if ( chargePower == LOW  && chargeCharge == HIGH && chargeDone == LOW ){
    chargeStatus = 4;  // Charge done
  }  else if ( chargePower == LOW && chargeCharge == LOW && chargeDone == LOW ){
    chargeStatus = 5;  // ERROR (Battery temperature)
  }
}

// Checks the timer and flips the LED state
void blinkIndicatorLed(){
  if ( indicatorBlinkState ){
    if ( millis() >= (indicatorBlinkTime + indicatorBlinkInterval)){
      indicatorBlinkTime = millis();
      indicatorBlinkState = LOW;
    }
  } else {
    if ( millis() >= (indicatorBlinkTime + indicatorBlinkInterval)){
      indicatorBlinkTime = millis();  // Increment the timer
      indicatorBlinkState = HIGH;
    }
  }
}

void setIndicatorLed(){
  switch (alternatorStatus){
    case 0:
      // Red
      indicatorLeds.setPixelColor(2, indRed);
      break;
    case 1:
      // Yellow
      indicatorLeds.setPixelColor(2, indYel);
      break;
    case 2:
      // Green
      indicatorLeds.setPixelColor(2, indGreen);
      break;
    case 3:
      // Blue
      indicatorLeds.setPixelColor(2, indBlue);
      break;
    case 4:
      // Blue
      indicatorLeds.setPixelColor(2, indWhite);
      break;
    case 5:
      // Red Blink
      blinkIndicatorLed();
      if ( indicatorBlinkState == HIGH ) {
        indicatorLeds.setPixelColor(2, indRed);
      }
      else {
        indicatorLeds.setPixelColor(2, indOff);
      }
      break;
  }
  switch (batteryStatus){
    case 0:
      // Red
      blinkIndicatorLed();
      if ( indicatorBlinkState == HIGH ) {
        indicatorLeds.setPixelColor(1, indRed);
      } else {
        indicatorLeds.setPixelColor(1, indOff);
      }
      break;
    case 1:
      // Yellow
      indicatorLeds.setPixelColor(1, indYel);
      break;
    case 2:
      // Green
      indicatorLeds.setPixelColor(1, indGreen);
      break;
    case 3:
      // Blue
      indicatorLeds.setPixelColor(1, indBlue);
      break;
    case 4:
      // White
      indicatorLeds.setPixelColor(1, indWhite);
      break;
  }
  switch (chargeStatus){
    case 0:
      // Red
      indicatorLeds.setPixelColor(0, indRed);
      break;
    case 1:
      // Yellow
      indicatorLeds.setPixelColor(0, indYel);
      break;
    case 2:
      // Red blink
      blinkIndicatorLed();
      if ( indicatorBlinkState == HIGH ) {
        indicatorLeds.setPixelColor(0, indRed);
      } else {
        indicatorLeds.setPixelColor(0, indOff);
      }
      break;
    case 3:
      // Green
      indicatorLeds.setPixelColor(0, indGreen);
      break;
    case 4:
      // White
      indicatorLeds.setPixelColor(0, indWhite);
      break;
    case 5:
      // Red blink
      blinkIndicatorLed();
      if ( indicatorBlinkState == HIGH ) {
        indicatorLeds.setPixelColor(0, indRed);
      } else {
        indicatorLeds.setPixelColor(0, indOff);
      }
      break;
  }
  indicatorLeds.show();
}

// The main loop
void loop(){
  // Reset these each loop; events build them up during each pass
  errorLevel = 0;
  alternatorStatus = 0;
  batteryStatus = 0;
  chargeStatus = 0;
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
void zeroPad(float value, bool hundreds=LOW, bool thousands=LOW){
  if ( thousands ) if ( value < 1000 ) Serial.print("0");
  if ( hundreds ) if ( value < 100 ) Serial.print("0");
  if ( value < 10 ) Serial.print("0");
}

void updateSerial(){
//Serial.print("AS\tBS\tCS\tValt\tIalt\tVbat\tIbat\tVbbu\tLED1\tLED2\tTled");
  //Serial.print(alternatorStatus); Serial.print("\t");
  //Serial.print(batteryStatus); Serial.print("\t");
  //Serial.print(chargeStatus); Serial.print("\t");
  
  Serial.print("AltV "); zeroPad(alternatorVoltageIn); Serial.print(alternatorVoltageIn); Serial.print("V; ");
  Serial.print("AltI "); Serial.print(alternatorCurrentIn,3); Serial.print("A;   ");
  
  Serial.print("BattV "); zeroPad(batteryVoltageIn); Serial.print(batteryVoltageIn); Serial.print("V; ");
  Serial.print("BattI "); zeroPad(batteryCurrentIn,HIGH); Serial.print(batteryCurrentIn); Serial.print("mA;   ");
  Serial.print("BbuV "); Serial.print(boostVoltageIn); Serial.print("V;   ");
  
  Serial.print("L1 "); zeroPad(led1Level,HIGH,HIGH); Serial.print(led1Level); Serial.print("; ");
  Serial.print("L2 "); zeroPad(led2Level,HIGH,HIGH); Serial.print(led2Level); Serial.print("; ");
  Serial.print("Tl "); Serial.print(systemLedTemperatureIn);
  Serial.println();
}
#endif
