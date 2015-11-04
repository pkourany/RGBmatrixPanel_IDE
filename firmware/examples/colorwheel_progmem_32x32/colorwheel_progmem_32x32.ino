// colorwheel_progmem demo for Adafruit RGBmatrixPanel library.
// Renders a nice circle of hues on our 32x32 RGB LED matrix:
// http://www.adafruit.com/products/607

// This version uses precomputed image data stored in PROGMEM
// rather than calculating each pixel.  Nearly instantaneous!  Woo!

// Written by Limor Fried/Ladyada & Phil Burgess/PaintYourDragon
// for Adafruit Industries.
// BSD license, all text above must be included in any redistribution.


#include "Adafruit_mfGFX/Adafruit_mfGFX.h"   // Core graphics library
#include "RGBmatrixPanel.h" // Hardware-specific library
#include "math.h"
#include "image.h"


/** Define RGB matrix panel GPIO pins **/
#if defined (STM32F10X_MD)	//Core
	#define CLK D6
	#define OE  D7
	#define LAT A4
	#define A   A0
	#define B   A1
	#define C   A2
	#define D	A3		// Only used for 32x32 panels
#endif

#if defined (STM32F2XX)	//Photon
	#define CLK D6
	#define OE  D7
	#define LAT A4
	#define A   A0
	#define B   A1
	#define C   A2
	#define D	A3		// Only used for 32x32 panels
#endif
/****************************************/


RGBmatrixPanel matrix(A, B, C, D, CLK, LAT, OE, false);

void setup() {
  int     i, len;
  uint8_t *ptr = matrix.backBuffer(); // Get address of matrix data

  // Copy image from image to matrix buffer:
  memcpy(ptr, img, sizeof(img));

  // Start up matrix AFTER data is copied.  The RGBmatrixPanel
  // interrupt code ties up about 40% of the CPU time, so starting
  // it now allows the prior drawing code to run even faster!
  matrix.begin();
}

void loop() {
  // do nothing
}
