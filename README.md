Bike_Alternator_and_LED_Headlight
==========================

Bicycle Alterator and LED Headlight control system.
Runs on an Arduino/ATMega328P 16MHz.

* LED brightness based on alternator output power, in consideration of:
  - System load on the alternator and backup battery, including uC and battery charging.
  - Alternator output voltage
  - Battery drain current
  - LED temperature
* MPPT-like loading for maximum alternator output efficiency.
* Voltage-proportional battery charge current
* Audible alarm if any parameters are beyond critical thresholds.
* RGB LED power status and error condition indicator
