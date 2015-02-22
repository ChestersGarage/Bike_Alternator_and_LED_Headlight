Bike_Alternator_and_LED_Headlight
==========================

Bicycle Alterator and LED Headlight control system.
Runs on an Arduino/ATMega328P 16MHz.

* LED brightness statically mapped against alternator output voltage, in consideration of:
  - Alternator voltage above minimum, and below battery backup threshold.
  - Alternator voltage above battery backup threhold.
  - LED temperature.
* Voltage-proportional battery charge current near alternator voltage minimum.
* Audible alarm if any parameters are beyond critical thresholds.
* RGB LED power status and error condition indicator.
