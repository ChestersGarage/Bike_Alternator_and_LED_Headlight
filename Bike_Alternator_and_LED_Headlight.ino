/*
  Bike Alterator LED Headlight control system.
  Runs on an Arduino/ATMEGA328.
  
  * LED brightness based on alternator output voltage:
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
#include <Adafruit_NeoPixel.h>

Adafruit_INA219 battVI(0x41);
Adafruit_MCP4725 Led1;
Adafruit_MCP4725 Led2;

// Comment out to disable debug output on serial
//#define DEBUG

// Serial on 0
// Serial on 1
#define indicatorLedsPin 2
#define led1EnablePin 5
#define led2EnablePin 6
#define alarmPin 7
// 8
// 9
// 10
#define chargePowerPin 8
#define chargeChargePin 9
#define chargeDonePin 10

#define ledTemperaturePin A0
#define boostVoltagePin A1
#define alternatorCurrentPin A2
#define alternatorVoltagePin A3
// A4 (SDA)
// A5 (SCL)

Adafruit_NeoPixel indicatorLeds = Adafruit_NeoPixel(3, indicatorLedsPin, NEO_RGB + NEO_KHZ400);

// Alternator
float alternatorVoltageIn;            // Alternator output voltage (V)
float alternatorVoltageMax = 33.9;    // Above this value (V), the LEDs are forced to maximum brightness and the alarm is sounded
float alternatorVoltageHi = 30.0;     // Main LEDs at max bright
float alternatorVoltageVpcc = 7.0;    // Below this value (V), the LEDs are forced to minimum brightness
float alternatorVoltageMin = 5.0;     // Minimum usable voltage
float alternatorCurrentIn;            // Alternator output current (mA)
float alternatorCurrentMax = 3.0;     // Maximum alternator output current (mA)
byte alternatorStatus = 0;
//float alternatorPowerIn;

// Battery
float batteryVoltageIn;            // Battery voltage (V)
float batteryVoltageMin = 3.3;     // Below this value (V), LEDs are forced to OFF and the alarm is sounded
float batteryVoltageMid = 3.8;     // Mid-point battery voltage
float batteryVoltageMax = 4.2;     // Battery is charged and/or charger is bypassing battery
float batteryCurrentIn;            // Battery backup drain current (mA)
float batteryCurrentMax = 1500.0;  // Above this value (mA), the LED brightness is decreased and the alarm is sounded
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
long ledDeadZoneLow = 900;
long led1Level;                             // Output value for LED1
long led2Level;                             // Output value for LED2
long led2Offset = dacRange*75/100;          // LED2 starts to turn on after about 75% brightness on LED1
long systemLedLevelBbu = 2500;
long systemLedLevelMin = 1150;              // Dead zone at the lowest LED level;
long systemLedLevelOut = systemLedLevelMin; // System LED brightness level 
long systemLedLevelMax = dacRange*175/100;  // Maximum system LED brightness. LED levels are mapped into this so that LED1 is almost full bright before LED2 starts to light.

// System
byte errorLevel = 0;
byte powerStatus = 0;
int displayUpdateInterval = 333;
long displayUpdateTime = 0;
long indicatorBlinkInterval = 666;  
long indicatorBlinkTime = 0;
boolean indicatorBlinkState = HIGH;

void setup() {
#ifdef DEBUG
  Serial.begin(115200);
#endif
  indicatorLeds.begin();
  battVI.begin();
//  altVI.begin();
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
#ifdef DEBUG
  Serial.print("AS\tBS\tCS\tValt\tIalt\tVbat\tIbat\tVbbu\tLED1\tLED2\tTled");
#endif
}

// Read all sensor values in a single pass
void readSensors(){
  alternatorVoltageIn = analogRead(alternatorVoltagePin)*(40.0/1023.0);
  alternatorCurrentIn = analogRead(alternatorCurrentPin)*(2.5/1023.0);
  batteryVoltageIn =    battVI.getBusVoltage_V();
  batteryCurrentIn =    battVI.getCurrent_mA();
  systemLedTemperatureIn  = analogRead(ledTemperaturePin);
  boostVoltageIn = analogRead(boostVoltagePin)*(18.75/1023.0);  // dac:819.2=4v 0.004882813V/div Vbbu/45*12=4 1024/5*4 33K and 12K 
  chargeCharge = digitalRead(chargeChargePin);
  chargeDone = digitalRead(chargeDonePin);
  chargePower = digitalRead(chargePowerPin);
}

// Threshold violations
void checkThresholds(){
  // Error level
  //  0 - OK
  //  1 - Warning, alarm
  //  2 - Error, alarm, min bright
  if ( alternatorVoltageIn >= alternatorVoltageMax )       errorLevel = 1;
  if ( systemLedTemperatureIn >= systemLedTemperatureHi )  errorLevel = 1;
  if ( batteryCurrentIn >= batteryCurrentMax )             errorLevel = 2;
  if ( alternatorCurrentIn >= alternatorCurrentMax )       errorLevel = 2;
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
  if ( errorLevel == 2 ){
    systemLedLevelOut = systemLedLevelMin;
  } 
  else if ( errorLevel <= 1 ){
    // Normal brightness adjustment
    if ( alternatorVoltageIn < alternatorVoltageVpcc ) {
      systemLedLevelOut = systemLedLevelMin;
    }
    else if ( alternatorVoltageIn >= alternatorVoltageVpcc && alternatorVoltageIn <= boostVoltageIn ) {
      systemLedLevelOut = map(alternatorVoltageIn*1000,alternatorVoltageVpcc*1000,boostVoltageIn*1000,systemLedLevelMin,systemLedLevelBbu);
    }
    else {
      systemLedLevelOut = map(alternatorVoltageIn*1000,boostVoltageIn*1000,alternatorVoltageHi*1000,systemLedLevelBbu-400,systemLedLevelMax);
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
0 - Red < 5V
1 - Grn 5V-7V (Vvpcc)
2 - Blu 7V-Vbbu
3 - Wht Vbbu-Vmax
4 - ERR > Vmax (Red blink)
*/
  if ( alternatorVoltageIn < alternatorVoltageMin ){
    alternatorStatus = 0;
  } else if ( alternatorVoltageIn >= alternatorVoltageMin && alternatorVoltageIn < alternatorVoltageVpcc ){
    alternatorStatus = 1;
  } else if ( alternatorVoltageIn >= alternatorVoltageVpcc && alternatorVoltageIn < boostVoltageIn ){
    alternatorStatus = 2;
  } else if ( alternatorVoltageIn >= boostVoltageIn && alternatorVoltageIn < alternatorVoltageMax ){
    alternatorStatus = 3;
  } else if ( alternatorVoltageIn >= alternatorVoltageMax ){
    alternatorStatus = 4;
  }
/* Battery Status
0 - Red < 3.3V (Red blink)
1 - Grn 3.3V-3.8V
2 - Blu 3.8V-4.2V
3 - Wht > 4.2V
*/
  if ( batteryVoltageIn < batteryVoltageMin ){
    batteryStatus = 0;
  } else if ( batteryVoltageIn >= batteryVoltageMin && batteryVoltageIn < batteryVoltageMid ){
    batteryStatus = 1;
  } else if ( batteryVoltageIn >= batteryVoltageMid && batteryVoltageIn < batteryVoltageMax ){
    batteryStatus = 2;
  } else if ( batteryVoltageIn >= batteryVoltageMax ){
    batteryStatus = 3;
  } 
/* Charge Status
0 - Red No power
1 - Grn Power, no charge
2 - ERR Battery < 3.1V (Red blink)
3 - Blu Charge
5 - Wht Done charging
7 - ERR Temperature (Red blink)
*/
  if ( chargePower == HIGH && chargeCharge == HIGH && chargeDone == HIGH ){
    chargeStatus = 0;  // No power
  } 
  else if ( chargePower == LOW  && chargeCharge == HIGH && chargeDone == HIGH ){
    chargeStatus = 1;  // Power, no charge
  } 
  else if ( chargePower == HIGH  && chargeCharge == LOW && chargeDone == HIGH ){
    chargeStatus = 2;  // ERROR (Battery < 3.1V)
  } 
  else if ( chargePower == LOW && chargeCharge == LOW && chargeDone == HIGH ){
    chargeStatus = 3;  // Charging
  } 
  else if ( chargePower == LOW  && chargeCharge == HIGH && chargeDone == LOW ){
    chargeStatus = 5;  // Charge done
  } 
  else if ( chargePower == LOW && chargeCharge == LOW && chargeDone == LOW ){
    chargeStatus = 7;  // ERROR (Battery temperature)
  }
/* Error level
1 - Grn blink
2 - Red blink
*/
}

void blinkIndicatorLed(){                                  // Checks the timer and flips the LED state
  if ( millis() >= (indicatorBlinkTime + indicatorBlinkInterval)){                     // If it's time
    indicatorBlinkTime = millis();  // Increment the timer
    indicatorBlinkState = !indicatorBlinkState;              // Flip the led state
  }
}

void setIndicatorLed(){
  if ( errorLevel < 2 ){
    // Blink red
    indicatorBlinkState = HIGH;
  } else {
  }
  
  switch (alternatorStatus){
    case 0:
      // Red
      indicatorLeds.setPixelColor(2, indicatorLeds.Color(1,0,0));
      break;
    case 1:
      // Green
      indicatorLeds.setPixelColor(2, indicatorLeds.Color(0,1,0));
      break;
    case 2:
      // Blue
      indicatorLeds.setPixelColor(2, indicatorLeds.Color(0,0,1));
      break;
    case 3:
      // White
      indicatorLeds.setPixelColor(2, indicatorLeds.Color(1,2,1));
      break;
    case 4:
      // Red Blink
      if ( indicatorBlinkState == HIGH ) {
        indicatorLeds.setPixelColor(2, indicatorLeds.Color(1,0,0));
      }
      else {
        indicatorLeds.setPixelColor(2, indicatorLeds.Color(0,0,0));
      }
      break;
  }
  switch (batteryStatus){
    case 0:
      // Red
      if ( indicatorBlinkState == HIGH ) {
        indicatorLeds.setPixelColor(1, indicatorLeds.Color(1,0,0));
      } else {
        indicatorLeds.setPixelColor(1, indicatorLeds.Color(0,0,0));
      }
      break;
    case 1:
      // Green
      indicatorLeds.setPixelColor(1, indicatorLeds.Color(0,1,0));
      break;
    case 2:
      // Blue
      indicatorLeds.setPixelColor(1, indicatorLeds.Color(0,0,1));
      break;
    case 3:
      // White
      indicatorLeds.setPixelColor(1, indicatorLeds.Color(1,2,1));
      break;
  }
  switch (chargeStatus){
    case 0:
      // Red
      indicatorLeds.setPixelColor(0, indicatorLeds.Color(1,0,0));
      break;
    case 1:
      // Green
      indicatorLeds.setPixelColor(0, indicatorLeds.Color(0,1,0));
      break;
    case 2:
      // Red blink
      if ( indicatorBlinkState == HIGH ) {
        indicatorLeds.setPixelColor(0, indicatorLeds.Color(1,0,0));
      } else {
        indicatorLeds.setPixelColor(0, indicatorLeds.Color(0,0,0));
      }
      break;
    case 3:
      // Blue
      indicatorLeds.setPixelColor(0, indicatorLeds.Color(0,0,1));
      break;
    case 5:
      // White
      indicatorLeds.setPixelColor(0, indicatorLeds.Color(1,2,1));
      break;
    case 7:
      // Red blink
      if ( indicatorBlinkState == HIGH ) {
        indicatorLeds.setPixelColor(0, indicatorLeds.Color(1,0,0));
      } else {
        indicatorLeds.setPixelColor(0, indicatorLeds.Color(0,0,0));
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
  //
  blinkIndicatorLed();
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
//Serial.print("AS\tBS\tCS\tValt\tIalt\tVbat\tIbat\tVbbu\tLED1\tLED2\tTled");
  Serial.print(alternatorStatus); Serial.print("\t");
  Serial.print(batteryStatus); Serial.print("\t");
  Serial.print(chargeStatus); Serial.print("\t");
  Serial.print(alternatorVoltageIn); Serial.print("\t");
  Serial.print(alternatorCurrentIn); Serial.print("\t");
  Serial.print(batteryVoltageIn); Serial.print("\t");
  Serial.print(batteryCurrentIn); Serial.print("\t");
  Serial.print(boostVoltageIn); Serial.print("\t");
  Serial.print(led1Level); Serial.print("\t");
  Serial.print(led2Level); Serial.print("\t");
  Serial.print(systemLedTemperatureIn);
  Serial.println();
}
#endif

