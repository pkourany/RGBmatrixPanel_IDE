RGB Matrix Panel
================

Arduino library for Adafruit 16x32 and 32x32 RGB LED matrix panels.
	http://www.adafruit.com/products/420
	http://www.adafruit.com/products/607

Adapted for Spark by Paul Kourany, June 2014

Updated for Particle Photon, Sept 2015

Updated Dec 2015 to properly support wide or daisychained panels with a "width" parameter.
Thanks to Andrew Holmes (author of RGBPongClock) for this contribution!!

Confirmed compatible with Electron, May 2016
Updated examples for new v4 RGBMatrixPanel shield pinout, May 2016


Particle Photon Adaptation
---
The Photon version only uses bit-banging due to the GPIO pin mapping not
allocating enough pin on a single port.  However, with the Photon's 120MHz
clock, the refresh rate is 140Hz

Particle Core Adaptation
---
The orginal Arduino library used a lot of direct I/O port write tricks and
assembler to achieve a calculated 283Hz refresh rate for a 16x32 panel.

The Particle version comes in two flavours: bit-banging and (partial) port
write for the output which could be optimized using inline assembler.  As it
stands the calculated refresh rate for a 16x32 panel using the bit-banged
version is 90Hz while the (partial) port write version is 140Hz (half those
values on 32x32 panel).

Display configuration
---
The library supports 16 pixel high and 32 pixel high panels of any multiples
of 32 pixels wide.  Adafruit sells 16x32, 32x32 and 64x32 panels fully compatible
with the library.  Daisy chaining displays will also allow for wider configurations,
limited only by available Core,  Photon or Electron RAM.

Constructor examples:

`RGBmatrixPanel matrix(A, B, C, CLK, LAT, OE, true);` //16x32 panel, width is 32 by default if not specified

`RGBmatrixPanel matrix(A, B, C, CLK, LAT, OE, true, 64);` //2 x 16x32 daisy chained = 16x64 panels

`RGBmatrixPanel matrix(A, B, C, D,CLK, LAT, OE, true, 32);` //32x32 panel

`RGBmatrixPanel matrix(A, B, C, D,CLK, LAT, OE, true, 64);` //64x32 panel


Components Required
---
This library requires the Adafruit_mfGFX and SparkIntervalTimer libraries

Wiring
---
Wiring between the Spark and 16x32 or 32x32 display is as follows:

```
Panel Pin	Core Pin	Photon/Electron Pin
--------------------------------------
  GND			GND			GND		
  CLK 			D6          D6		// Specified in constructor
  OE  			D7          D7		// Specified in constructor
  LAT 			A4          A4		// Specified in constructor
  A   			A0          A0		// Specified in constructor
  B   			A1          A1		// Specified in constructor
  C   			A2          A2		// Specified in constructor
  D				A3			A3		// 32x32 display only - Specified in constructor
  R1			D0			D0		
  G1			D1			D1		
  B1			D2			D2		
  R2			D3			D3		
  G2			D4			D4		
  B2			D5			D5		
```

The display needs its own 5V supply.

Connect, compile, flash and run.
