RGB Matrix Panel
================

Arduino library for Adafruit 16x32 and 32x32 RGB LED matrix panels.
	http://www.adafruit.com/products/420
	http://www.adafruit.com/products/607

Adapted for Spark by Paul Kourany, June 2014

Updated for Particle Photon, Sept 2015

Core Adaptation
---
The orginal Arduino library used a lot of direct I/O port write tricks and
assembler to achieve a calculated 283Hz refresh rate for a 16x32 panel.

The Core version comes in two flavours: bit-banging and (partial) port
write for the output which could be optimized using inline assembler.  As it
stands the calculated refresh rate for a 16x32 panel using the bit-banged
version is 90Hz while the (partial) port write version is 140Hz (half those
values on 32x32 panel).

The Photon version only uses bit-banging due to the GPIO pin mapping not
allocating enough pin on a single port.  However, with the Photon's 120MHz
clock, the refresh rate is 140Hz

Components Required
---
This library requires the Adafruit_mfGFX and SparkIntervalTimer libraries

Wiring
---
Wiring between the Spark and 16x32 or 32x32 display is as follows:

```
Panel Pin	Core Pin	Photon Pin
----------------------------------
  GND				GND			GND		
  CLK 				D6          D6
  OE  				D7          D7
  LAT 				A4          A4
  A   				A0          A0
  B   				A1          A1
  C   				A2          A2
  D					A3			A3		//32x32 display only
  R1				D0			D0		
  G1				D1			D1		
  B1				D2			D2		
  R2				D3			D3		
  G2				D4			D4		
  B2				D5			D5		
```

The display needs its own 5V supply.

Connect, compile, flash and run.
