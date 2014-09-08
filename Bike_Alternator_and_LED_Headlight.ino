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
#define R_IND_PIN 2
#define G_IND_PIN 3
#define B_IND_PIN 4
#define LED1_EN_PIN 5
#define LED2_EN_PIN 6
#define ALARM_PIN 7
// 8
#define LED1_PWM_PIN 9
#define LED2_PWM_PIN 10
// 11
// 12
// 13

#define V_ALT_PIN A0
#define I_ALT_PIN A1
#define V_BATT_PIN A2
#define I_BATT_PIN A3
#define T_LED_PIN A4
// A5

// Measured values
int V_ALT;  // Alternator output voltage
int V_BATT; // Battery voltage
int I_ALT;  // Alternator output current 
int I_BATT; // Battery backup drain current  
int T_LED;  // LED temperature

// Thresholds and limits
int V_ALT_HI     = 682; // 20V/6/ADC - Increase LED brightness at or above this voltage
int V_ALT_BBU    = 546; // 15V/6/ADC - Voltage at which LED power transitions between alternator and battery backup
int V_ALT_MIN    = 170; // 5V/6/ADC - Minimum alternator output voltage
//int V_BATT_MIN   = 626; // 3V/ADC - Minimum battery voltage
//int I_ALT_MAX    = 626; // 3A (from ref above) - Maximum alternator output current
int I_BATT_MAX   = 77; // 2A (from ref above) - Maximum battery backup drain current

// Calibration and calculated values
int LED1_PWM       = 0;
int LED2_PWM       = 0;
int LED_BRT        = 0;   // LED brightness
int LED_MAX        = 447; // 447 Overall LED brightness range. LED PWMs are is mapped into this so that LED1 is almost full bright before LED2 starts to light.
int LED_MIN        = 48;  // Minimum useful PWM value, below which the LED is off or won't dim any further (low dead zone).
byte R_SYS_OPT     = 31;  // Optimum impedance load on the alternator, in terms of ADC scale. We allow +/- 3
byte R_SYS_GAP     = 3;   // Dead zone centered around the the R_SYS_OPT value. Reduces flicker. 
unsigned int R_SYS = 100; // Start the calculated impedance at a safe value.
byte RUNMODE       = 0;   // Used for debugging
byte PWM_INCR      = 1;   // 

//#define DEBUG

void setup() {
  #ifdef DEBUG
    Serial.begin(9600);
  #endif
  TCCR1B = TCCR1B & 0b11111000 | 0x01; // Sets LED PWM freq to ~31,250Hz
  pinMode(LED1_PWM_PIN, OUTPUT);
  pinMode(LED2_PWM_PIN, OUTPUT);
  pinMode(ALARM_PIN, OUTPUT);
  pinMode(R_IND_PIN, OUTPUT);
  pinMode(G_IND_PIN, OUTPUT);
  pinMode(B_IND_PIN, OUTPUT);
  pinMode(LED1_EN_PIN, OUTPUT);
  pinMode(LED2_EN_PIN, OUTPUT);
  analogWrite(LED1_PWM_PIN, 255-LED_MIN);      // Driver dimming is inverted (255=off, 0=max). Use analogWrite(PIN,255-VAL);)
  analogWrite(LED2_PWM_PIN, 255);      // Driver dimming is inverted (255=off, 0=max). Use analogWrite(PIN,255-VAL);)
  digitalWrite(ALARM_PIN, LOW);
  digitalWrite(R_IND_PIN, LOW);
  digitalWrite(G_IND_PIN, LOW);
  digitalWrite(B_IND_PIN, LOW);
  digitalWrite(LED1_EN_PIN, HIGH);
  digitalWrite(LED2_EN_PIN, HIGH);
}

void readSensors(){
  V_ALT  = analogRead(V_ALT_PIN);
  I_ALT  = abs(analogRead(I_ALT_PIN)-511);
  V_BATT = analogRead(V_BATT_PIN);
  I_BATT = abs(analogRead(I_BATT_PIN)-511);
  T_LED  = analogRead(T_LED_PIN);
}

void adjustLEDPWM(){
  if ( R_SYS >= (R_SYS_OPT + R_SYS_GAP) ){ increaseLEDPWM(); }
  if ( R_SYS <= (R_SYS_OPT - R_SYS_GAP) ){ decreaseLEDPWM(); }
}

void setLEDs(){
  if ( LED_BRT > LED_MAX ){ LED_BRT = LED_MAX; }
  if ( LED_BRT < LED_MIN ){ LED_BRT = LED_MIN; }
  LED1_PWM = LED_BRT;
  LED2_PWM = LED_BRT - 192;
  if ( LED1_PWM > 255 ){ LED1_PWM = 255; }
  if ( LED2_PWM < 0 )  { LED2_PWM = 0; }
  analogWrite(LED1_PWM_PIN,255-LED1_PWM);
  analogWrite(LED2_PWM_PIN,255-LED2_PWM);
}

void increaseLEDPWM(){
  if ( LED_BRT <= LED_MAX ){
    LED_BRT = LED_BRT + PWM_INCR;
  }
  setLEDs();
}

void decreaseLEDPWM(){ // Decrease by 10% via integer math
  if ( LED_BRT >= LED_MIN ){
    LED_BRT = LED_BRT - PWM_INCR; 
  }
  setLEDs();
}

void checkOverCurrent(){
  while ( I_BATT >= I_BATT_MAX ) {
    decreaseLEDPWM();
    readSensors();
  }
}

void loop() {
//  if ( PWM_INCR < 0 ) { PWM_INCR=1; }
  readSensors();
  checkOverCurrent();
  R_SYS = V_ALT/(I_ALT+(I_BATT/4));
  if ( V_ALT > V_ALT_HI ){  // Above max alternator output voltage
    R_SYS_OPT=10;
    R_SYS_GAP=1;
    adjustLEDPWM();
    RUNMODE=3;
  }
  if ( V_ALT > V_ALT_BBU && V_ALT < V_ALT_HI ){ // Optimum range
    R_SYS_OPT=20;
    R_SYS_GAP=2;
    adjustLEDPWM();
    RUNMODE=2;
  }
  if ( V_ALT > V_ALT_MIN && V_ALT < V_ALT_BBU ){ // Within the BBU and charging
    R_SYS_OPT=30;
    R_SYS_GAP=3;
    adjustLEDPWM();
    RUNMODE=1;
  }
  if ( V_ALT < V_ALT_MIN ){ // Below min alternator output voltage
    R_SYS_OPT=40;
    R_SYS_GAP=4;
    adjustLEDPWM();
    RUNMODE=0;
  }
  
  #ifdef DEBUG
    Serial.print("RM ");
    Serial.print(RUNMODE);
    Serial.print("\tRS ");
    Serial.print(R_SYS);
    Serial.print("\t1L ");
    Serial.print(LED1_PWM);
    Serial.print("\t2L ");
    Serial.print(LED2_PWM);
    Serial.print("\tV ");
    Serial.print(V_ALT * 6 * 0.004882812);
    Serial.print("\tI ");
    Serial.print(I_ALT / 0.185 * 0.004887586);
  //  Serial.print("\tV ");
    //Serial.print(V_BATT * 0.004882812);
    Serial.print("\tI ");
    Serial.print(I_BATT / 0.185 * 0.004887586);
  //  Serial.print("\tT ");
   // Serial.print(T_LED);
   Serial.println();
  #endif
//delay(3);
}
