Bike_Alternator_and_LED_Headlight
==========================

Bicycle Alterator and LED Headlight control system.
Runs on an Arduino/ATMega328P 16MHz.

UNFINISHED CODE
  
* LED brightness based on alternator output power, in consideration of:
  - System load on the alternator and backup battery, including uC and battery charging.
  - Maximum alternator output voltage
  - Maximum battery drain current
  - LED temperature
* MPPT-like loading for maximum alternator output efficiency.
* Alarm sound if any parameters are beyond critical thresholds:
  - Battery voltage too low.
  - Battery current too high.
  - Alternator current too high.
  - Alternator voltage too high (analog trigger, not uC).
  - LED temperature too high.
* RGB LED status indicator
