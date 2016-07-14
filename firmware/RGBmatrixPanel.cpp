/*
RGBmatrixPanel Arduino library for Adafruit 16x32 and 32x32 RGB LED
matrix panels.  Pick one up at:
  http://www.adafruit.com/products/420
  http://www.adafruit.com/products/607

This version uses a few tricks to achieve better performance and/or
lower CPU utilization:

- To control LED brightness, traditional PWM is eschewed in favor of
  Binary Code Modulation, which operates through a succession of periods
  each twice the length of the preceeding one (rather than a direct
  linear count a la PWM).  It's explained well here:

    http://www.batsocks.co.uk/readme/art_bcm_1.htm

  I was initially skeptical, but it works exceedingly well in practice!
  And this uses considerably fewer CPU cycles than software PWM.

- Although many control pins are software-configurable in the user's
  code, a couple things are tied to specific PORT registers.  It's just
  a lot faster this way -- port lookups take time.  Please see the notes
  later regarding wiring on "alternative" Arduino boards.

- A tiny bit of inline assembly language is used in the most speed-
  critical section.  The C++ compiler wasn't making optimal use of the
  instruction set in what seemed like an obvious chunk of code.  Since
  it's only a few short instructions, this loop is also "unrolled" --
  each iteration is stated explicitly, not through a control loop.

Written by Limor Fried/Ladyada & Phil Burgess/PaintYourDragon for
Adafruit Industries.
BSD license, all text above must be included in any redistribution.
*/

#include "SparkIntervalTimer/SparkIntervalTimer.h"
#include "RGBmatrixPanel.h"
#include "gamma.h"

// A full PORT register is required for the data lines, though only the
// top 6 output bits are used.  For performance reasons, the port # cannot
// be changed via library calls, only by changing constants in the library.
// For similar reasons, the clock pin is only semi-configurable...it can
// be specified as any pin within a specific PORT register stated below.

//#define FASTER		// Uncomment for fast port GPIO - ONLY SUPPORTED ON CORE!

#if !defined(PLATFORM_ID)		// Core v0.3.4
#warning "CORE v0.3.4"
  #define pinSetFast(_pin)		PIN_MAP[_pin].gpio_peripheral->BSRR = PIN_MAP[_pin].gpio_pin
  #define pinResetFast(_pin)	PIN_MAP[_pin].gpio_peripheral->BRR = PIN_MAP[_pin].gpio_pin
#endif

#if defined (STM32F10X_MD) || !defined(PLATFORM_ID)		//Core
#warning "CORE NEW"
//  #define pinSetFast(_pin)		PIN_MAP[_pin].gpio_peripheral->BSRR = PIN_MAP[_pin].gpio_pin
//  #define pinResetFast(_pin)	PIN_MAP[_pin].gpio_peripheral->BRR = PIN_MAP[_pin].gpio_pin

 #if defined(FASTER)		// Parital Port writes
  #define R1	A6		// bit 2 = RED 1
  #define G1	D4		// bit 3 = GREEN 1
  #define B1	D3		// bit 4 = BLUE 1
  #define R2	D2		// bit 5 = RED 2
  #define G2	D1		// bit 6 = GREEN 2
  #define B2	D0		// bit 7 = BLUE 2
  static const uint16_t	dur[4] = {30, 60, 120, 240};

 #else  					// Bit banging
  #define R1	D0		// bit 2 = RED 1
  #define G1	D1		// bit 3 = GREEN 1
  #define B1	D2		// bit 4 = BLUE 1
  #define R2	D3		// bit 5 = RED 2
  #define G2	D4		// bit 6 = GREEN 2
  #define B2	D5		// bit 7 = BLUE 2
  static const uint16_t	dur[4] = {50, 100, 200, 400};
 #endif
#elif defined (STM32F2XX)	//Photon
  #define R1	D0		// bit 2 = RED 1
  #define G1	D1		// bit 3 = GREEN 1
  #define B1	D2		// bit 4 = BLUE 1
  #define R2	D3		// bit 5 = RED 2
  #define G2	D4		// bit 6 = GREEN 2
  #define B2	D5		// bit 7 = BLUE 2
  static const uint16_t	dur[4] = {30, 60, 120, 240};
#endif

#define nPlanes 4

//Define hardware IntervalTimer
IntervalTimer refreshTimer;

void refreshISR(void);


// The fact that the display driver interrupt stuff is tied to the
// singular Timer1 doesn't really take well to object orientation with
// multiple RGBmatrixPanel instances.  The solution at present is to
// allow instances, but only one is active at any given time, via its
// begin() method.  The implementation is still incomplete in parts;
// the prior active panel really should be gracefully disabled, and a
// stop() method should perhaps be added...assuming multiple instances
// are even an actual need.
static RGBmatrixPanel *activePanel = NULL;

// Code common to both the 16x32 and 32x32 constructors:
void RGBmatrixPanel::init(uint8_t rows, uint8_t a, uint8_t b, uint8_t c,
  uint8_t sclk, uint8_t latch, uint8_t oe, boolean dbuf, uint8_t width) {

  nRows = rows; // Number of multiplexed rows; actual height is 2X this

  // Allocate and initialize matrix buffer:
  int buffsize  = width * nRows * 3, // x3 = 3 bytes holds 4 planes "packed"
      allocsize = (dbuf == true) ? (buffsize * 2) : buffsize;
  if(NULL == (matrixbuff[0] = (uint8_t *)malloc(allocsize))) return;
  memset(matrixbuff[0], 0, allocsize);
  // If not double-buffered, both buffers then point to the same address:
  matrixbuff[1] = (dbuf == true) ? &matrixbuff[0][buffsize] : matrixbuff[0];

  // Save pin numbers for use by begin() method later.
  _a     = a;
  _b     = b;
  _c     = c;
  _sclk  = sclk;
  _latch = latch;
  _oe    = oe;

  plane     = nPlanes - 1;
  row       = nRows   - 1;
  swapflag  = false;
  backindex = 0;     // Array index of back buffer
}

// Constructor for 16x32 panel:
RGBmatrixPanel::RGBmatrixPanel(
  uint8_t a, uint8_t b, uint8_t c,
  uint8_t sclk, uint8_t latch, uint8_t oe, boolean dbuf, uint8_t width) :
  Adafruit_GFX(width, 16) {

  init(8, a, b, c, sclk, latch, oe, dbuf, width);
}

// Constructor for 32x32 or 32x64 panel:
RGBmatrixPanel::RGBmatrixPanel(
  uint8_t a, uint8_t b, uint8_t c, uint8_t d,
  uint8_t sclk, uint8_t latch, uint8_t oe, boolean dbuf, uint8_t width) :
  Adafruit_GFX(width, 32) {

  init(16, a, b, c, sclk, latch, oe, dbuf, width);

  // Init a few extra 32x32-specific elements:
  _d        = d;

}

void RGBmatrixPanel::begin(void) {

  backindex   = 0;                         // Back buffer
  buffptr     = matrixbuff[1 - backindex]; // -> front buffer
  activePanel = this;                      // For interrupt hander

  // Enable all comm & address pins as outputs, set default states:
  pinMode(_sclk , OUTPUT); pinResetFast(_sclk);	//Low
  pinMode(_latch, OUTPUT); pinResetFast(_latch);	//Low
  pinMode(_oe   , OUTPUT); pinSetFast(_oe);		//High  (disable output)
  pinMode(_a    , OUTPUT); pinResetFast(_a);		//Low
  pinMode(_b    , OUTPUT); pinResetFast(_b);		//Low
  pinMode(_c    , OUTPUT); pinResetFast(_c);		//Low
  if(nRows > 8) {
    pinMode(_d  , OUTPUT); pinResetFast(_d);		//Low
  }

  pinMode(R1, OUTPUT); pinResetFast(R1);			//Low
  pinMode(G1, OUTPUT); pinResetFast(G1);			//Low
  pinMode(B1, OUTPUT); pinResetFast(B1);			//Low
  pinMode(R2, OUTPUT); pinResetFast(R2);			//Low
  pinMode(G2, OUTPUT); pinResetFast(G2);			//Low
  pinMode(B2, OUTPUT); pinResetFast(B2);			//Low

#if defined (STM32F10X_MD) || !defined(PLATFORM_ID)		//Core
  refreshTimer.begin(refreshISR, 200, uSec);		// Use allocated timer
#else
  refreshTimer.begin(refreshISR, 200, uSec, TIMER7);	// Use non-GPIO timer
#endif
}

// Original RGBmatrixPanel library used 3/3/3 color.  Later version used
// 4/4/4.  Then Adafruit_GFX (core library used across all Adafruit
// display devices now) standardized on 5/6/5.  The matrix still operates
// internally on 4/4/4 color, but all the graphics functions are written
// to expect 5/6/5...the matrix lib will truncate the color components as
// needed when drawing.  These next functions are mostly here for the
// benefit of older code using one of the original color formats.

// Promote 3/3/3 RGB to Adafruit_GFX 5/6/5
uint16_t RGBmatrixPanel::Color333(uint8_t r, uint8_t g, uint8_t b) {
  // RRRrrGGGgggBBBbb
  return ((r & 0x7) << 13) | ((r & 0x6) << 10) |
         ((g & 0x7) <<  8) | ((g & 0x7) <<  5) |
         ((b & 0x7) <<  2) | ((b & 0x6) >>  1);
}

// Promote 4/4/4 RGB to Adafruit_GFX 5/6/5
uint16_t RGBmatrixPanel::Color444(uint8_t r, uint8_t g, uint8_t b) {
  // RRRRrGGGGggBBBBb
  return ((r & 0xF) << 12) | ((r & 0x8) << 8) |
         ((g & 0xF) <<  7) | ((g & 0xC) << 3) |
         ((b & 0xF) <<  1) | ((b & 0x8) >> 3);
}

// Demote 8/8/8 to Adafruit_GFX 5/6/5
// If no gamma flag passed, assume linear color
uint16_t RGBmatrixPanel::Color888(uint8_t r, uint8_t g, uint8_t b) {
  return ((uint16_t)(r & 0xF8) << 8) | ((uint16_t)(g & 0xFC) << 3) | (b >> 3);
}

// 8/8/8 -> gamma -> 5/6/5
uint16_t RGBmatrixPanel::Color888(
  uint8_t r, uint8_t g, uint8_t b, boolean gflag) {
  if(gflag) { // Gamma-corrected color?
    r = gamma[r]; // Gamma correction table maps
    g = gamma[g]; // 8-bit input to 4-bit output
    b = gamma[b];
    return ((uint16_t)r << 12) | ((uint16_t)(r & 0x8) << 8) | // 4/4/4->5/6/5
           ((uint16_t)g <<  7) | ((uint16_t)(g & 0xC) << 3) |
           (          b <<  1) | (           b        >> 3);
  } // else linear (uncorrected) color
  return ((uint16_t)(r & 0xF8) << 8) | ((uint16_t)(g & 0xFC) << 3) | (b >> 3);
}

uint16_t RGBmatrixPanel::ColorHSV(
  long hue, uint8_t sat, uint8_t val, boolean gflag) {

  uint8_t  r, g, b, lo;
  uint16_t s1, v1;

  // Hue
  hue %= 1536;             // -1535 to +1535
  if(hue < 0) hue += 1536; //     0 to +1535
  lo = hue & 255;          // Low byte  = primary/secondary color mix
  switch(hue >> 8) {       // High byte = sextant of colorwheel
    case 0 : r = 255     ; g =  lo     ; b =   0     ; break; // R to Y
    case 1 : r = 255 - lo; g = 255     ; b =   0     ; break; // Y to G
    case 2 : r =   0     ; g = 255     ; b =  lo     ; break; // G to C
    case 3 : r =   0     ; g = 255 - lo; b = 255     ; break; // C to B
    case 4 : r =  lo     ; g =   0     ; b = 255     ; break; // B to M
    default: r = 255     ; g =   0     ; b = 255 - lo; break; // M to R
  }

  // Saturation: add 1 so range is 1 to 256, allowig a quick shift operation
  // on the result rather than a costly divide, while the type upgrade to int
  // avoids repeated type conversions in both directions.
  s1 = sat + 1;
  r  = 255 - (((255 - r) * s1) >> 8);
  g  = 255 - (((255 - g) * s1) >> 8);
  b  = 255 - (((255 - b) * s1) >> 8);

  // Value (brightness) & 16-bit color reduction: similar to above, add 1
  // to allow shifts, and upgrade to int makes other conversions implicit.
  v1 = val + 1;
  if(gflag) { // Gamma-corrected color?
    r = gamma[(r * v1) >> 8]; // Gamma correction table maps
    g = gamma[(g * v1) >> 8]; // 8-bit input to 4-bit output
    b = gamma[(b * v1) >> 8];
  } else { // linear (uncorrected) color
    r = (r * v1) >> 12; // 4-bit results
    g = (g * v1) >> 12;
    b = (b * v1) >> 12;
  }
  return (r << 12) | ((r & 0x8) << 8) | // 4/4/4 -> 5/6/5
         (g <<  7) | ((g & 0xC) << 3) |
         (b <<  1) | ( b        >> 3);
}

void RGBmatrixPanel::drawPixel(int16_t x, int16_t y, uint16_t c) {
  uint8_t r, g, b, bit, limit, *ptr;

  if((x < 0) || (x >= _width) || (y < 0) || (y >= _height)) return;

  switch(rotation) {
   case 1:
    swap(x, y);
    x = WIDTH  - 1 - x;
    break;
   case 2:
    x = WIDTH  - 1 - x;
    y = HEIGHT - 1 - y;
    break;
   case 3:
    swap(x, y);
    y = HEIGHT - 1 - y;
    break;
  }

  // Adafruit_GFX uses 16-bit color in 5/6/5 format, while matrix needs
  // 4/4/4.  Pluck out relevant bits while separating into R,G,B:
  r =  c >> 12;        // RRRRrggggggbbbbb
  g = (c >>  7) & 0xF; // rrrrrGGGGggbbbbb
  b = (c >>  1) & 0xF; // rrrrrggggggBBBBb

  // Loop counter stuff
  bit   = 2;
  limit = 1 << nPlanes;

  if(y < nRows) {
    // Data for the upper half of the display is stored in the lower
    // bits of each byte.
    ptr = &matrixbuff[backindex][y * WIDTH * (nPlanes - 1) + x]; // Base addr
    // Plane 0 is a tricky case -- its data is spread about,
    // stored in least two bits not used by the other planes.
    ptr[WIDTH*2] &= ~0B00000011;           // Plane 0 R,G mask out in one op
    if(r & 1) ptr[WIDTH*2] |=  0B00000001; // Plane 0 R: 64 bytes ahead, bit 0
    if(g & 1) ptr[WIDTH*2] |=  0B00000010; // Plane 0 G: 64 bytes ahead, bit 1
    if(b & 1) ptr[WIDTH]   |=  0B00000001; // Plane 0 B: 32 bytes ahead, bit 0
    else      ptr[WIDTH]   &= ~0B00000001; // Plane 0 B unset; mask out
    // The remaining three image planes are more normal-ish.
    // Data is stored in the high 6 bits so it can be quickly
    // copied to the DATAPORT register w/6 output lines.
    for(; bit < limit; bit <<= 1) {
      *ptr &= ~0B00011100;            // Mask out R,G,B in one op
      if(r & bit) *ptr |= 0B00000100; // Plane N R: bit 2
      if(g & bit) *ptr |= 0B00001000; // Plane N G: bit 3
      if(b & bit) *ptr |= 0B00010000; // Plane N B: bit 4
      ptr  += WIDTH;                 // Advance to next bit plane
    }
  } else {
    // Data for the lower half of the display is stored in the upper
    // bits, except for the plane 0 stuff, using 2 least bits.
    ptr = &matrixbuff[backindex][(y - nRows) * WIDTH * (nPlanes - 1) + x];
    *ptr &= ~0B00000011;                  // Plane 0 G,B mask out in one op
    if(r & 1)  ptr[WIDTH] |=  0B00000010; // Plane 0 R: 32 bytes ahead, bit 1
    else       ptr[WIDTH] &= ~0B00000010; // Plane 0 R unset; mask out
    if(g & 1) *ptr        |=  0B00000001; // Plane 0 G: bit 0
    if(b & 1) *ptr        |=  0B00000010; // Plane 0 B: bit 0
    for(; bit < limit; bit <<= 1) {
      *ptr &= ~0B11100000;            // Mask out R,G,B in one op
      if(r & bit) *ptr |= 0B00100000; // Plane N R: bit 5
      if(g & bit) *ptr |= 0B01000000; // Plane N G: bit 6
      if(b & bit) *ptr |= 0B10000000; // Plane N B: bit 7
      ptr  += WIDTH;                 // Advance to next bit plane
    }
  }
}

void RGBmatrixPanel::fillScreen(uint16_t c) {
  if((c == 0x0000) || (c == 0xffff)) {
    // For black or white, all bits in frame buffer will be identically
    // set or unset (regardless of weird bit packing), so it's OK to just
    // quickly memset the whole thing:
    memset(matrixbuff[backindex], c, WIDTH * nRows * 3);
  } else {
    // Otherwise, need to handle it the long way:
    Adafruit_GFX::fillScreen(c);
  }
}

// Return address of back buffer -- can then load/store data directly
uint8_t *RGBmatrixPanel::backBuffer() {
  return matrixbuff[backindex];
}

// For smooth animation -- drawing always takes place in the "back" buffer;
// this method pushes it to the "front" for display.  Passing "true", the
// updated display contents are then copied to the new back buffer and can
// be incrementally modified.  If "false", the back buffer then contains
// the old front buffer contents -- your code can either clear this or
// draw over every pixel.  (No effect if double-buffering is not enabled.)
void RGBmatrixPanel::swapBuffers(boolean copy) {
  if(matrixbuff[0] != matrixbuff[1]) {
    // To avoid 'tearing' display, actual swap takes place in the interrupt
    // handler, at the end of a complete screen refresh cycle.
    swapflag = true;                  // Set flag here, then...
    while(swapflag == true) delay(1); // wait for interrupt to clear it
    if(copy == true)
      memcpy(matrixbuff[backindex], matrixbuff[1-backindex], WIDTH * nRows * 3);
  }
}

// Dump display contents to the Serial Monitor, adding some formatting to
// simplify copy-and-paste of data as a PROGMEM-embedded image for another
// sketch.  If using multiple dumps this way, you'll need to edit the
// output to change the 'img' name for each.  Data can then be loaded
// back into the display using a pgm_read_byte() loop.
void RGBmatrixPanel::dumpMatrix(void) {

  int i, buffsize = WIDTH * nRows * 3;

  Serial.print(F("\n\n"
    "static const uint8_t PROGMEM img[] = {\n  "));

  for(i=0; i<buffsize; i++) {
    Serial.print(F("0x"));
    if(matrixbuff[backindex][i] < 0x10) Serial.write('0');
    Serial.print(matrixbuff[backindex][i],HEX);
    if(i < (buffsize - 1)) {
      if((i & 7) == 7) Serial.print(F(",\n  "));
      else             Serial.write(',');
    }
  }
  Serial.println(F("\n};"));
}

// -------------------- Interrupt handler stuff --------------------
void refreshISR(void)
{
  activePanel->updateDisplay();   // Call refresh func for active display
}

// Two constants are used in timing each successive BCM interval.
// These were found empirically, by checking the value of TCNT1 at
// certain positions in the interrupt code.
// CALLOVERHEAD is the number of CPU 'ticks' from the timer overflow
// condition (triggering the interrupt) to the first line in the
// updateDisplay() method.  It's then assumed (maybe not entirely 100%
// accurately, but close enough) that a similar amount of time will be
// needed at the opposite end, restoring regular program flow.
// LOOPTIME is the number of 'ticks' spent inside the shortest data-
// issuing loop (not actually a 'loop' because it's unrolled, but eh).
// Both numbers are rounded up slightly to allow a little wiggle room
// should different compilers produce slightly different results.
#define CALLOVERHEAD 60   // Actual value measured = 56
#define LOOPTIME     200  // Actual value measured = 188
// The "on" time for bitplane 0 (with the shortest BCM interval) can
// then be estimated as LOOPTIME + CALLOVERHEAD * 2.  Each successive
// bitplane then doubles the prior amount of time.  We can then
// estimate refresh rates from this:
// 4 bitplanes = 320 + 640 + 1280 + 2560 = 4800 ticks per row.
// 4800 ticks * 16 rows (for 32x32 matrix) = 76800 ticks/frame.
// 16M CPU ticks/sec / 76800 ticks/frame = 208.33 Hz.
// Actual frame rate will be slightly less due to work being done
// during the brief "LEDs off" interval...it's reasonable to say
// "about 200 Hz."  The 16x32 matrix only has to scan half as many
// rows...so we could either double the refresh rate (keeping the CPU
// load the same), or keep the same refresh rate but halve the CPU
// load.  We opted for the latter.
// Can also estimate CPU use: bitplanes 1-3 all use 320 ticks to
// issue data (the increasing gaps in the timing invervals are then
// available to other code), and bitplane 0 takes 920 ticks out of
// the 2560 tick interval.
// 320 * 3 + 920 = 1880 ticks spent in interrupt code, per row.
// From prior calculations, about 4800 ticks happen per row.
// CPU use = 1880 / 4800 = ~39% (actual use will be very slightly
// higher, again due to code used in the LEDs off interval).
// 16x32 matrix uses about half that CPU load.  CPU time could be
// further adjusted by padding the LOOPTIME value, but refresh rates
// will decrease proportionally, and 200 Hz is a decent target.

// The flow of the interrupt can be awkward to grasp, because data is
// being issued to the LED matrix for the *next* bitplane and/or row
// while the *current* plane/row is being shown.  As a result, the
// counter variables change between past/present/future tense in mid-
// function...hopefully tenses are sufficiently commented.

void RGBmatrixPanel::updateDisplay(void) {
  uint8_t  i, *ptr;
  uint16_t duration, pins;

  pinSetFast(_oe);			// Disable LED output during row/plane switchover
  pinSetFast(_latch);		// Latch data loaded during *prior* interrupt
  pinResetFast(_sclk);		// Start the clock LOW

  // Get the time to next interrupt
  duration = dur[plane];

  // Borrowing a technique here from Ray's Logic:
  // www.rayslogic.com/propeller/Programming/AdafruitRGB/AdafruitRGB.htm
  // This code cycles through all four planes for each scanline before
  // advancing to the next line.  While it might seem beneficial to
  // advance lines every time and interleave the planes to reduce
  // vertical scanning artifacts, in practice with this panel it causes
  // a green 'ghosting' effect on black pixels, a much worse artifact.

  if(++plane >= nPlanes) {      // Advance plane counter.  Maxed out?
    plane = 0;                  // Yes, reset to plane 0, and
    if(++row >= nRows) {        // advance row counter.  Maxed out?
      row     = 0;              // Yes, reset row counter, then...
      if(swapflag == true) {    // Swap front/back buffers if requested
        backindex = 1 - backindex;
        swapflag  = false;
      }
      buffptr = matrixbuff[1-backindex]; // Reset into front buffer
    }
  } else if(plane == 1) {
    // Plane 0 was loaded on prior interrupt invocation and is about to
    // latch now, so update the row address lines before we do that:

    (row & 0x1) ? pinSetFast(_a) : pinResetFast(_a);
    (row & 0x2) ? pinSetFast(_b) : pinResetFast(_b);
    (row & 0x4) ? pinSetFast(_c) : pinResetFast(_c);
    if(nRows > 8) {
      (row & 0x8) ? pinSetFast(_d) : pinResetFast(_d);
    }
  }

  // buffptr, being 'volatile' type, doesn't take well to optimization.
  // A local register copy can speed some things up:
  ptr = (uint8_t *)buffptr;

  // RESET timer duration
  refreshTimer.resetPeriod_SIT(duration, uSec);

  pinResetFast(_oe);		// Re-enable output
  pinResetFast(_latch);		// Latch down

  if(plane > 0) {

    // Planes 1-3 must be unpacked and bit-banged
    for (uint8_t i=0; i < WIDTH; i++) {

#if defined (FASTER) && (defined(STM32F10X_MD) || !defined(PLATFORM_ID))
		pins = (ptr[i] & 0xF8) | ((ptr[i] & 0x04) >> 2);		//Shift R1 to bit 0
		GPIOB->BSRR = pins;
		GPIOB->BRR = ~pins & 0xF9;
		pinSetFast(_sclk);
		pinResetFast(_sclk);
#else
		(ptr[i] & 0x04) ? pinSetFast(R1) : pinResetFast(R1); 	//R1
		(ptr[i] & 0x08) ? pinSetFast(G1) : pinResetFast(G1);	//G1
		(ptr[i] & 0x10) ? pinSetFast(B1) : pinResetFast(B1); 	//B1
		(ptr[i] & 0x20) ? pinSetFast(R2) : pinResetFast(R2);	 //R2
		(ptr[i] & 0x40) ? pinSetFast(G2) : pinResetFast(G2);	 //G2
		(ptr[i] & 0x80) ? pinSetFast(B2) : pinResetFast(B2);	 //B2
		pinSetFast(_sclk);		//hi
		pinResetFast(_sclk);	//lo
#endif
	}

    buffptr += WIDTH;

  } else {

    // Plane 0 has its data packed into the 2 least bits not
    // used by the other planes.  This works because the unpacking and
    // output for plane 0 is handled while plane 3 is being displayed...
    // because binary coded modulation is used (not PWM), that plane
    // has the longest display interval, so the extra work fits.

	for(i=0; i<WIDTH; i++) {
		uint8_t bits = ( ptr[i] << 6) | ((ptr[i+WIDTH] << 4) & 0x30) | ((ptr[i+WIDTH*2] << 2) & 0x0C);

#if defined (FASTER) && (defined(STM32F10X_MD) || !defined(PLATFORM_ID))
		pins = (bits & 0xF8) | ((bits & 0x04) >> 2);		//Shift R1 to bit 0
		GPIOB->BSRR = pins;
		GPIOB->BRR = ~pins & 0xF9;

		pinSetFast(_sclk);
		pinResetFast(_sclk);
#else
		(bits & 0x04) ? pinSetFast(R1) : pinResetFast(R1);		//R1
		(bits & 0x08) ? pinSetFast(G1) : pinResetFast(G1);		//G1
		(bits & 0x10) ? pinSetFast(B1) : pinResetFast(B1);		//B1
		(bits & 0x20) ? pinSetFast(R2) : pinResetFast(R2);		//R2
		(bits & 0x40) ? pinSetFast(G2) : pinResetFast(G2);		//G2
		(bits & 0x80) ? pinSetFast(B2) : pinResetFast(B2);		//B2
		pinSetFast(_sclk);		//hi
		pinResetFast(_sclk);		//lo
#endif
    }
  }
}
