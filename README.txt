RGB Matrix Panel
================

Arduino library for Adafruit 16x32 and 32x32 RGB LED matrix panels.
	http://www.adafruit.com/products/420
	http://www.adafruit.com/products/607

Adapted for Spark by Paul Kourany, June 2014

Components Required
---
This library requires the Adafruit_mfGFX and SparkIntervalTimer libraries

Wiring
---
Wiring between the Spark and 16x32 or 32x32 display is as follows:

16x32 Pin		Spark Pin
--------------------------
  GND				GND
  CLK 				D6
  OE  				D7
  LAT 				A4
  A   				A0
  B   				A1
  C   				A2
  D					A3	//32x32 display only
  R1				D0				
  G1				D1				
  B1				D2				
  R2				D3				
  G2				D4				
  B2				D5				

The display needs its own 5V supply.

Connect, compile, flash and run.