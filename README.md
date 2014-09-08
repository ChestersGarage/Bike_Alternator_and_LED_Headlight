BikeAlternatorLEDHeadlight
==========================

Bicycle Alterator/LED Headlight control system.
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
