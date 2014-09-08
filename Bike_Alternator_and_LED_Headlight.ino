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

Conversion info
ADC: 0.004882812 V/step (5V ARef)

Current sensor: ACS712 - 185mV/A, centered on 2.5V
Ireal = (Iadc - 512) * 0.004882812 / 0.185
Iadc = (Ireal * 0.185 / 0.004882812) + 512
In calculations, we subtract 512adc to get a number with its sign centered on zero.
 5A = 3.425V -> 702
 4A = 3.24V  -> 664
 3A = 3.055V -> 626
 2A = 2.87V  -> 588
 1A = 2.685V -> 550
 0A = 2.5V   -> 511
-1A = 2.315V -> 474
-2A = 2.13V  -> 436
-3A = 1.945V -> 398
-4A = 1.76V  -> 360
-5A = 1.575V -> 322

Voltage sensor: Reads 0-30V across 5V ADC range. 
It uses a resistive voltage divider to scale the voltage to 1/6 with a 5.2V zener for protection.
Vreal = Vadc * 0.004882812 * 6 (where x is the analogRead value)
Vadc  = Vreal / 0.004882812 / 6 (where y is the voltage in Volts)
*/

// Serial on 0
// Serial on 1
#define indicatorRedPin 2
#define indicatorGreenPin 3
#define indicatorBluePin 4
#define led1EnablePin 5
#define led2EnablePin 6
#define alarmPin 7
// 8
#define led1PwmPin 9
#define led2PwmPin 10
// 11
// 12
// 13

#define alternatorVoltagePin A0
#define alternatorCurrentPin A1
#define batteryVoltagePin A2
#define batteryCurrentPin A3
#define ledTemperaturePin A4
// A5

// Measured values
int alternatorVoltageIn;  // Alternator output voltage
int alternatorCurrentIn;  // Alternator output current 
int batteryVoltageIn;     // Battery voltage
int batteryCurrentIn;     // Battery backup drain current  
int systemLedTemperatureIn;   // LED temperature

// Thresholds and limits
int alternatorVoltageMax = 682;    // 20V/6/ADC - Increase LED brightness at or above this voltage
int alternatorVoltageBbu = 546;    // 15V/6/ADC - Voltage at which LED power transitions between alternator and battery backup
int alternatorVoltageMin = 170;    // 5V/6/ADC - Minimum alternator output voltage
//int alternatorCurrentMax = 626;  // 3A (from ref above) - Maximum alternator output current
//int batteryVoltageMin = 626;     // 3V/ADC - Minimum battery voltage
int batteryCurrentMax = 77;        // 2A (from ref above) - Maximum battery backup drain current

// Calibration and calculated values
int led1PwmOut = 0;                      // PWM output value for LED1
int led2PwmOut = 0;                      // PWM output value for LED2
int systemLedBrightnessOut = 0;             // Overall LED brightness scale
int systemLedBrightnessMax = 447;        // 447 Overall LED brightness range. LED PWMs are is mapped into this so that LED1 is almost full bright before LED2 starts to light.
int systemLedBrightnessMin = 48;         // Minimum useful PWM value, below which the LED is off or won't dim any further (low dead zone).
byte systemImpedanceOpt = 31;            // Optimum impedance load on the alternator, in terms of ADC scale. We allow +/- 3
byte systemImpedanceGap = 3;             // Dead zone centered around the the systemImpedanceOpt value. Reduces flicker. 
unsigned int systemImpedanceInit = 100;  // Start the calculated impedance at a safe value.
byte systemRunMode = 0;                  // Used for debugging
byte ledPwmIncrement = 1;                // LED PWM output increment value

//#define DEBUG

void setup() {
  #ifdef DEBUG
    Serial.begin(9600);
  #endif
  TCCR1B = TCCR1B & 0b11111000 | 0x01; // Sets LED PWM freq to ~31,250Hz
  pinMode(led1PwmPin, OUTPUT);
  pinMode(led2PwmPin, OUTPUT);
  pinMode(alarmPin, OUTPUT);
  pinMode(indicatorRedPin, OUTPUT);
  pinMode(indicatorGreenPin, OUTPUT);
  pinMode(indicatorBluePin, OUTPUT);
  pinMode(led1EnablePin, OUTPUT);
  pinMode(led2EnablePin, OUTPUT);
  analogWrite(led1PwmPin, 255-systemLedBrightnessMin);      // Driver dimming is inverted (255=off, 0=max). Use analogWrite(PIN,255-VAL);)
  analogWrite(led2PwmPin, 255);      // Driver dimming is inverted (255=off, 0=max). Use analogWrite(PIN,255-VAL);)
  digitalWrite(alarmPin, LOW);
  digitalWrite(indicatorRedPin, LOW);
  digitalWrite(indicatorGreenPin, LOW);
  digitalWrite(indicatorBluePin, LOW);
  digitalWrite(led1EnablePin, HIGH);
  digitalWrite(led2EnablePin, HIGH);
}

void readSensors(){
  alternatorVoltageIn  = analogRead(alternatorVoltagePin);
  alternatorCurrentIn  = abs(analogRead(alternatorCurrentPin)-511);
  batteryVoltageIn = analogRead(batteryVoltagePin);
  batteryCurrentIn = abs(analogRead(batteryCurrentPin)-511);
  systemLedTemperatureIn  = analogRead(ledTemperaturePin);
}

void adjustLEDPWM(){
  if ( systemImpedanceInit >= (systemImpedanceOpt + systemImpedanceGap) ){ increaseLEDPWM(); }
  if ( systemImpedanceInit <= (systemImpedanceOpt - systemImpedanceGap) ){ decreaseLEDPWM(); }
}

void setLEDs(){
  if ( systemLedBrightnessOut > systemLedBrightnessMax ){ systemLedBrightnessOut = systemLedBrightnessMax; }
  if ( systemLedBrightnessOut < systemLedBrightnessMin ){ systemLedBrightnessOut = systemLedBrightnessMin; }
  led1PwmOut = systemLedBrightnessOut;
  led2PwmOut = systemLedBrightnessOut - 192;
  if ( led1PwmOut > 255 ){ led1PwmOut = 255; }
  if ( led2PwmOut < 0 )  { led2PwmOut = 0; }
  analogWrite(led1PwmPin,255-led1PwmOut);
  analogWrite(led2PwmPin,255-led2PwmOut);
}

void increaseLEDPWM(){
  if ( systemLedBrightnessOut <= systemLedBrightnessMax ){
    systemLedBrightnessOut = systemLedBrightnessOut + ledPwmIncrement;
  }
  setLEDs();
}

void decreaseLEDPWM(){ // Decrease by 10% via integer math
  if ( systemLedBrightnessOut >= systemLedBrightnessMin ){
    systemLedBrightnessOut = systemLedBrightnessOut - ledPwmIncrement; 
  }
  setLEDs();
}

void checkOverCurrent(){
  while ( batteryCurrentIn >= batteryCurrentMax ) {
    decreaseLEDPWM();
    readSensors();
  }
}

void loop() {
//  if ( ledPwmIncrement < 0 ) { ledPwmIncrement=1; }
  readSensors();
  checkOverCurrent();
  systemImpedanceInit = alternatorVoltageIn/(alternatorCurrentIn+(batteryCurrentIn/4));
  if ( alternatorVoltageIn > alternatorVoltageMax ){  // Above max alternator output voltage
    systemImpedanceOpt=10;
    systemImpedanceGap=1;
    adjustLEDPWM();
    systemRunMode=3;
  }
  if ( alternatorVoltageIn > alternatorVoltageBbu && alternatorVoltageIn < alternatorVoltageMax ){ // Optimum range
    systemImpedanceOpt=20;
    systemImpedanceGap=2;
    adjustLEDPWM();
    systemRunMode=2;
  }
  if ( alternatorVoltageIn > alternatorVoltageMin && alternatorVoltageIn < alternatorVoltageBbu ){ // Within the BBU and charging
    systemImpedanceOpt=30;
    systemImpedanceGap=3;
    adjustLEDPWM();
    systemRunMode=1;
  }
  if ( alternatorVoltageIn < alternatorVoltageMin ){ // Below min alternator output voltage
    systemImpedanceOpt=40;
    systemImpedanceGap=4;
    adjustLEDPWM();
    systemRunMode=0;
  }
  
  #ifdef DEBUG
    Serial.print("RM ");
    Serial.print(systemRunMode);
    Serial.print("\tRS ");
    Serial.print(systemImpedanceInit);
    Serial.print("\t1L ");
    Serial.print(led1PwmOut);
    Serial.print("\t2L ");
    Serial.print(led2PwmOut);
    Serial.print("\tV ");
    Serial.print(alternatorVoltageIn * 6 * 0.004882812);
    Serial.print("\tI ");
    Serial.print(alternatorCurrentIn / 0.185 * 0.004887586);
  //  Serial.print("\tV ");
    //Serial.print(batteryVoltageIn * 0.004882812);
    Serial.print("\tI ");
    Serial.print(batteryCurrentIn / 0.185 * 0.004887586);
  //  Serial.print("\tT ");
   // Serial.print(systemLedTemperatureIn);
   Serial.println();
  #endif
//delay(3);
}
