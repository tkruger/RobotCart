/**
 * SliceCartWithTimeManager
 * ========================
 * Lighting controller for the SLICE robot cart.
 *
 * The cart has two lighting systems that are driven independently each loop():
 *
 *   1. The "side" LED strip (300 WS2812 pixels on DATA_PIN) that forms the
 *      illuminated S-L-I-C-E letters on both sides of the cart, plus a white
 *      internal illuminator.
 *   2. The "matrix" display: a 64x16 NeoPixel matrix built from four 32x8
 *      panels (2 x 2 tiles) on DISPLAY_PIN, used for text such as
 *      "SLICE" / "8738" / "SLICE! SLICE! SQUEEZE!!".
 *
 * Two push buttons step forward/back through the numbered LED programs in
 * loop(). A microphone and two potentiometers feed the sound-reactive
 * "noise" program.
 *
 * "Time manager": nothing here blocks for long. Each effect is a function
 * called once per loop() that looks at how long it has been running
 * (timeInSideSequence / timeInScrollSequence) and draws the right frame.
 * This lets a side effect and a matrix effect run at the same time.
 */

#include <Adafruit_GFX.h>
#include <Adafruit_NeoMatrix.h>
#include <Adafruit_NeoPixel.h>

#ifndef PSTR
#define PSTR  // Make Arduino Due happy
#endif

// ---------------------------------------------------------------------------
// Hardware configuration
// ---------------------------------------------------------------------------

// How many leds in your strip?
#define NUM_LEDS 300
// What pin is it on
#define DATA_PIN 4

// Matrix definition: each tile is 32 wide x 8 tall, arranged 2 across x 2 down,
// giving a 64 x 16 pixel display in total
#define GRID_NUM_ROWS 8
#define GRID_NUM_COLS 32
#define GRID_TILES_X 2
#define GRID_TILES_Y 2
// Matrix Data Pin
#define DISPLAY_PIN 7

// How wide is a single charater in LEDs at text size 1 (5px glyph + 1px gap).
// Text is drawn at size 2, so most math below multiplies this by 2 (12px/char),
// which means about 5 characters fit across the 64px display at once.
#define CHAR_PIX_WIDTH 6
// Button pin for up (read as active HIGH, so the button needs a pull-down resistor)
#define TOP_BUTTON_PIN 9
// Button pin for down (active HIGH, same as above)
#define BOTTOM_BUTTON_PIN 8
// Min variable resistor (potentiometer): noise floor for the mic program
#define MIN_PIN A1
// Max variable resistor (potentiometer): sensitivity/ceiling for the mic program
#define MAX_PIN A2

// How many LED programs do we have to loop through (cases 0..LED_PROGRAMS-1 in loop()).
// Remember to bump this when adding a new case.
#define LED_PROGRAMS 10

// Where is the mic connected?
#define MIC_PIN A0

// How many pixels scrolling text moves per frame. 1 is the smoothest. The
// matrix + strip can only be updated ~24 times a second (every LED takes 30us
// to send), so 1 pixel/frame scrolls at ~24 pixels (2 characters) a second.
// 2 doubles the speed but looks jumpy.
#define SCROLL_STEP 1

// ---------------------------------------------------------------------------
// ColorWheel
// ---------------------------------------------------------------------------

/**
 * Defined a group of variables focused on rolling through the colorwheel
 * at a given speed. Allows you to create a colorwheel then spin it at a given speed
 * which allows you to get red, green and blue values out for that color
 *
 * The wheel walks around the edge of the RGB cube in 6 phases:
 *   red -> yellow -> green -> cyan -> blue -> magenta -> red
 * Each call to rollingRainbow(jump) moves one channel by `jump` (0-255).
 * Bigger jumps = faster color changes, fewer distinct colors.
 **/
class ColorWheel {
  public:
    // Red green and blue
    int red = 0;
    int green = 0;
    int blue = 0;

	  ColorWheel() {}

    // Resets the colorwheel back to the start of the rainbow (pure red, phase 0)
    void resetRainbow() {
      state = 0;
      red = 255;
      green = 0;
      blue = 0;
      red_done = true;
      green_done = false;
      blue_done = false;
    }

    /**
     * Resets the rainbow to a set number of colors in (always in single jumps)
     * Used in rolling rainbow applications where we need to pick up in a known state
     * and set a bunch of follow up elements to create a wave
     **/
    void resetRainbow(int colorsIn){
      for (int i = 0; i < colorsIn; i++)
        rollingRainbow(1);
    }

    /**
     * Roll through the rainbow by one step of size `jump`.
     * Each phase ramps a single channel up or down until it hits 0 or 255,
     * then moves to the next phase.
     **/
    void rollingRainbow(int jump) {
      switch (state) {
        // Phase 0: red -> yellow (green goes up)
        case 0:
          if (green == 255) {
            state = 1;
          } else {
            green = green + jump;
          }
          break;
        // Phase 1: yellow -> green (red goes down)
        case 1:
          if (red == 0) {
            state = 2;
          } else {
            red = red - jump;
          }
          break;
        // Phase 2: green -> cyan (blue goes up)
        case 2:
          if (blue == 255) {
            state = 3;
          } else {
            blue = blue + jump;
          }
          break;
        // Phase 3: cyan -> blue (green goes down)
        case 3:
          if (green == 0) {
            state = 4;
          } else {
            green = green - jump;
          }
          break;
        // Phase 4: blue -> magenta (red goes up)
        case 4:
          if (red == 255) {
            state = 5;
          } else {
            red = red + jump;
          }
          break;
        // Phase 5: magenta -> red (blue goes down), then wrap to phase 0
        case 5:
          if (blue == 0) {
            state = 0;
          } else {
            blue = blue - jump;
          }
          break;
      }

      // Handle multiples smaller than things that add up to 0 or 255
      if (red > 255){
        red = 255;
      }
      if (green > 255){
        green = 255;
      }
      if (blue > 255){
        blue = 255;
      }
      if (red < 0){
        red = 0;
      }
      if (green < 0){
        green = 0;
      }
      if (blue < 0){
        blue = 0;
      }
    }
  
  private:
    // Which of the 6 rainbow phases we are in (0-5)
    int state = 0;
    // Currently unused - left over from an earlier implementation
    bool red_done = false;
    bool green_done = false;
    bool blue_done = false;
};

// ---------------------------------------------------------------------------
// Hardware objects
// ---------------------------------------------------------------------------

// MATRIX DECLARATION:
// Parameter 1 = width of NeoPixel matrix
// Parameter 2 = height of matrix
// Parameter 3 = pin number (most are valid)
// Parameter 4 = matrix layout flags, add together as needed:
//   NEO_MATRIX_TOP, NEO_MATRIX_BOTTOM, NEO_MATRIX_LEFT, NEO_MATRIX_RIGHT:
//     Position of the FIRST LED in the matrix; pick two, e.g.
//     NEO_MATRIX_TOP + NEO_MATRIX_LEFT for the top-left corner.
//   NEO_MATRIX_ROWS, NEO_MATRIX_COLUMNS: LEDs are arranged in horizontal
//     rows or in vertical columns, respectively; pick one or the other.
//   NEO_MATRIX_PROGRESSIVE, NEO_MATRIX_ZIGZAG: all rows/columns proceed
//     in the same order, or alternate lines reverse direction; pick one.
//   See example below for these values in action.
// Parameter 5 = pixel type flags, add together as needed:
//   NEO_KHZ800  800 KHz bitstream (most NeoPixel products w/WS2812 LEDs)
//   NEO_KHZ400  400 KHz (classic 'v1' (not v2) FLORA pixels, WS2811 drivers)
//   NEO_GRB     Pixels are wired for GRB bitstream (most NeoPixel products)
//   NEO_GRBW    Pixels are wired for GRBW bitstream (RGB+W NeoPixel products)
//   NEO_RGB     Pixels are wired for RGB bitstream (v1 FLORA pixels, not v2)


// Example for NeoPixel Shield.  In this application we'd like to use it
// as a 5x8 tall matrix, with the USB port positioned at the top of the
// Arduino.  When held that way, the first pixel is at the top right, and
// lines are arranged in columns, progressive order.  The shield uses
// 800 KHz (v2) pixels that expect GRB color data.
/*Adafruit_NeoMatrix matrix = Adafruit_NeoMatrix(GRID_NUM_COLS, GRID_NUM_ROWS, DISPLAY_PIN,
                                               NEO_MATRIX_BOTTOM + NEO_MATRIX_RIGHT + NEO_MATRIX_COLUMNS + NEO_MATRIX_ZIGZAG,
                                               NEO_GRB + NEO_KHZ800);

*/
// The actual display: four 32x8 tiles (each wired as zigzag columns starting
// bottom-right), with the tiles themselves chained top-right first in zigzag rows.
// Result is one logical 64 x 16 canvas: x = 0..63 left to right, y = 0..15 top to bottom.
Adafruit_NeoMatrix matrix = Adafruit_NeoMatrix(GRID_NUM_COLS, GRID_NUM_ROWS, GRID_TILES_X, GRID_TILES_Y, DISPLAY_PIN,
                                               NEO_TILE_TOP + NEO_TILE_RIGHT + NEO_TILE_ROWS + NEO_TILE_ZIGZAG +
                                               NEO_MATRIX_BOTTOM + NEO_MATRIX_RIGHT + NEO_MATRIX_COLUMNS + NEO_MATRIX_ZIGZAG,
                                               NEO_GRB + NEO_KHZ800);


// Declare our NeoPixel strip object:
Adafruit_NeoPixel strip(NUM_LEDS, DATA_PIN, NEO_GRB + NEO_KHZ800);
// Argument 1 = Number of pixels in NeoPixel strip
// Argument 2 = Arduino pin number (most are valid)
// Argument 3 = Pixel type flags, add together as needed:
//   NEO_KHZ800  800 KHz bitstream (most NeoPixel products w/WS2812 LEDs)
//   NEO_KHZ400  400 KHz (classic 'v1' (not v2) FLORA pixels, WS2811 drivers)
//   NEO_GRB     Pixels are wired for GRB bitstream (most NeoPixel products)
//   NEO_RGB     Pixels are wired for RGB bitstream (v1 FLORA pixels, not v2)
//   NEO_RGBW    Pixels are wired for RGBW bitstream (NeoPixel RGBW products)

// ---------------------------------------------------------------------------
// Colors and shared state
// ---------------------------------------------------------------------------

// A simple list of rainbow colors starting with red to loop through.
// These are 16-bit (RGB565) matrix colors, only for use with `matrix`, not `strip`.
const uint16_t rainbowColors[] = {
  matrix.Color(255, 0, 0),
  matrix.Color(255, 127, 0),
  matrix.Color(255, 255, 0),
  matrix.Color(127, 255, 0),
  matrix.Color(0, 255, 0),
  matrix.Color(0, 255, 127),
  matrix.Color(0, 255, 255),
  matrix.Color(0, 127, 255),
  matrix.Color(0, 0, 255),
  matrix.Color(127, 0, 255),
  matrix.Color(255, 0, 255),
  matrix.Color(255, 0, 127)
};

// Quick reference colors
const uint16_t ORANGE = matrix.Color(255, 127, 0);
const uint16_t WHITE = matrix.Color(255, 255, 255);
const uint16_t BLACK = matrix.Color(0, 0, 0);
const uint16_t CYAN = matrix.Color(0, 220, 255);
// Dim cyan used to "fill" a lit segment of the logo's orange slice
const uint16_t DIM_CYAN = matrix.Color(0, 90, 110);

// ---------------------------------------------------------------------------
// Team logo (64 x 16 pixel art)
//
// Half orange slice on the left, "SLICE" (white) over "8738" (cyan) on the right.
// Each character is one pixel, written as a hex digit:
//   0     off
//   1     orange (rind, segment dividers, hub)
//   2     white  ("SLICE")
//   4-7   inside slice segment 0-3, left to right (black unless lit)
//   8-B   part of digit 0-3 of "8738" (cyan, or white when highlighted)
// Segment N of the slice lines up with digit N of 8738, like the real logo.
//
// Generated by tools/logo_gen.py - edit and re-run that rather than hand editing.
// Stored in PROGMEM (flash) so it doesn't use any of the Arduino's RAM.
// ---------------------------------------------------------------------------
#define LOGO_WIDTH 64
#define LOGO_HEIGHT 16
const char LOGO[LOGO_HEIGHT][LOGO_WIDTH + 1] PROGMEM = {
  "1111111111111111111111111111111000022220200000022200022200222220",
  "1144444444444111117777777777711000200000200000002000200020200000",
  "1144444444441151611777777777711000200000200000002000200000200000",
  "1144444444411551661177777777711000022200200000002000200000222200",
  "0144444444115551666117777777710000000020200000002000200000200000",
  "0114444441155551666611777777110000000020200000002000200020200000",
  "0114444411555551666661177777110000222200222220022200022200222220",
  "0011444115555551666666117771100000000000000000000000000000000000",
  "0011441155555551666666611771100000000000000000000000000000000000",
  "00011115555555516666666611110000000000888009999900AAA000BBB00000",
  "0000115555555551666666666110000000000800080000090A000A0B000B0000",
  "00000111555555516666666111000000000008000800009000000A0B000B0000",
  "000000111555555166666611100000000000008880000900000AA000BBB00000",
  "00000000111155516661111000000000000008000800900000000A0B000B0000",
  "0000000000111111111110000000000000000800080090000A000A0B000B0000",
  "00000000000000111000000000000000000000888000900000AAA000BBB00000"
};

// Colorwheels to rotate through for side panels and matrix effects
ColorWheel sideColorWheel = ColorWheel();
ColorWheel matrixColorWheel = ColorWheel();

// The offset from the left for displaying text on the matrix.
// Scrolling text starts at matrix.width() (just off the right edge) and counts
// down into negative numbers until the whole phrase has left the screen.
int matrixOffset = matrix.width();

// Reset both rainbow color wheels and also reset the matrix offset
void resetRainbowScroll() {
  matrixOffset = matrix.width();
  sideColorWheel.resetRainbow();
  matrixColorWheel.resetRainbow();
}

//View array spreadsheet here: https://docs.google.com/spreadsheets/d/1uPBvxobsNldO1_tUTYW7d_6D1sfJVXOsJzB6nNNpvE0/edit?usp=sharing
//Leds 128 to 170 compose the internal illuminator
//Leds 0 to 41 compose right column 1
//Leds 84 to 42 compose right column 2
//Leds 85 to 127 compose right column 3
//Leds 171 to 213 compose left column 1
//Leds 256 to 214 compose left column 2
//Leds 257 to 298 compose left column 3
//
// Each column is ~43 LEDs tall and runs through all five letters. "Rows" (see
// setRow) count from 0 at the bottom of the E up to 42 at the top of the S, and
// the same row is lit in all six columns at once. Columns 2 and 5 are wired in
// reverse, which is why setRow uses 84 - row and 256 - row for them.

// Microphone volume (raw analogRead, 0-1023)
int micVolume = 0;

//Method Switcher Variables
// Current height of the sound meter in the noise() program (falls by 1 each loop)
int rows = 0;
// Which LED program is selected (0 = off). Changed by the buttons.
int sequence = 0;
// The program that ran last loop, to detect when the buttons changed it
int lastSequence = 0;

//FastLoop Variables
// Real time in ms, captured at the start of each loop() by cartMillis().
// All the timers are unsigned long like millis() itself: an `int` on the Mega
// is only 16 bits and overflows after ~32 seconds, which used to freeze effects.
unsigned long time = 0;

// Variables to track the state of the side panel sequences.
// Each side effect calls startSide("itsName") every frame; that returns true
// only on the first frame, which is where the effect does its setup.
unsigned long sideSequenceStartTime = 0;
String currentSideMethod = "";
// Milliseconds since the current side effect (re)started
unsigned long timeInSideSequence = 0;
// Set true by restartSide() when a side effect finishes one full cycle
bool sideFinished = false;

// Variables to track the state of the matrix, same pattern as the side panels
unsigned long scrollSequenceStartTime = 0;
String currentScrollMethod = "";
// Milliseconds since the current matrix effect (re)started
unsigned long timeInScrollSequence = 0;
// Set true by restartScroll() when a matrix effect finishes one full cycle
bool scrollFinished = false;

// ---------------------------------------------------------------------------
// Keeping time while sending to the LEDs
//
// On the Mega, interrupts are switched off while show() sends data to the
// LEDs, so millis() stops counting. Each LED takes 30us, so every matrix
// update (1024 LEDs, ~31ms) and strip update (300 LEDs, ~9ms) loses almost
// all of that time - millis() was running about 10-20x slower than real time,
// which is why some programs seemed stuck (e.g. a 0.5s blink taking 9s).
//
// Always call showStrip() / showMatrix() instead of strip.show() /
// matrix.show(): they add the lost time back, and cartMillis() includes it.
// ---------------------------------------------------------------------------
#define MATRIX_LEDS (GRID_NUM_ROWS * GRID_NUM_COLS * GRID_TILES_X * GRID_TILES_Y)
#define MICROS_PER_LED 30UL
// Total time millis() has missed, in microseconds
unsigned long lostMicros = 0;

// millis() missed the whole show() except the ~1ms tick that was pending
void addLostTime(unsigned long leds) {
  unsigned long showMicros = leds * MICROS_PER_LED;
  if (showMicros > 1024) lostMicros += showMicros - 1024;
}

void showStrip() {
  strip.show();
  addLostTime(NUM_LEDS);
}

void showMatrix() {
  matrix.show();
  addLostTime(MATRIX_LEDS);
}

// Real elapsed time in ms (millis() plus the time it missed during show())
unsigned long cartMillis() {
  return millis() + lostMicros / 1000;
}

/**
 * Call at the top of a side effect with the effect's name. Returns true on
 * the first frame after switching to it (do your setup then), and resets the
 * side timer so timeInSideSequence starts from 0.
 **/
bool startSide(const char *name) {
  if (currentSideMethod.equals(name)) return false;
  currentSideMethod = name;
  sideSequenceStartTime = time;
  timeInSideSequence = 0;
  sideFinished = false;
  return true;
}

/**
 * Same as startSide() but for matrix effects.
 **/
bool startScroll(const char *name) {
  if (currentScrollMethod.equals(name)) return false;
  currentScrollMethod = name;
  scrollSequenceStartTime = time;
  timeInScrollSequence = 0;
  scrollFinished = false;
  return true;
}

// ---------------------------------------------------------------------------
// Setup and main loop
// ---------------------------------------------------------------------------

/**
 * One-time hardware init: start serial, both LED chains, and set default
 * text options on the matrix (no wrap, size 2 = 12x16 px per character).
 **/
void setup() {
  Serial.begin(500000);
  pinMode(LED_BUILTIN, OUTPUT);
  strip.begin();
  showStrip();

  matrix.begin();
  showMatrix();
  matrix.setTextWrap(false);
  matrix.setBrightness(100);
  matrix.setTextColor(ORANGE);
  matrix.setTextSize(2);
  resetRainbowScroll();
}

/**
 * Runs forever. Each pass:
 *   1. Samples inputs and updates the sequence timers
 *   2. Checks the buttons to change program
 *   3. Calls the side-panel effect and matrix effect for the current program,
 *      each of which draws a single frame and returns
 **/
void loop() {
  //Choose HTML color codes to use here: https://g.co/kgs/S9Anr7
  //Run the method of your choice below
  micVolume = analogRead(MIC_PIN);
  time = cartMillis();
  // Unsigned subtraction stays correct even when millis() wraps (every ~50 days)
  timeInSideSequence = time - sideSequenceStartTime;
  timeInScrollSequence = time - scrollSequenceStartTime;

  // Top button = next program, bottom button = previous program (wraps around)
  if (digitalRead(TOP_BUTTON_PIN) == 1) {
    sequence++;
    while (digitalRead(TOP_BUTTON_PIN) == 1) {
      delay(1);
    }  // Wait for button to be released to prevent skipping
    delay(30);  // Ignore contact bounce on release so one press = one step
    if (sequence < 0)
      sequence = LED_PROGRAMS - 1;
    else if (sequence >= LED_PROGRAMS)
      sequence = 0;
      
    Serial.print("Sequence: ");
    Serial.println(sequence);
  } else if (digitalRead(BOTTOM_BUTTON_PIN) == 1) {
    sequence--;
    while (digitalRead(BOTTOM_BUTTON_PIN) == 1) {
      delay(1);
    }  // Wait for button to be released to prevent skipping
    delay(30);  // Ignore contact bounce on release so one press = one step
    if (sequence < 0)
      sequence = LED_PROGRAMS - 1;
    else if (sequence >= LED_PROGRAMS)
      sequence = 0;
    
    Serial.print("Sequence: ");
    Serial.println(sequence);
  }

  // Changed program? Forget the current effects so the new program's effects
  // always run their first-time setup (otherwise e.g. solidColor() wouldn't
  // redraw after program 0 cleared the strip).
  if (sequence != lastSequence) {
    lastSequence = sequence;
    currentSideMethod = "";
    currentScrollMethod = "";
  }

  // The LED programs. Most pair one side-panel effect with one matrix effect.
  // To add a program: add a case here and increase LED_PROGRAMS.
  switch (sequence) {
    // 0: Everything off
    case 0:
      resetRainbowScroll();
      strip.clear();
      showStrip();
      matrix.clear();
      showMatrix();
      break;
    // 1: Fading rainbow sides + scrolling chant that changes color as a whole
    case 1:
      rainbow(15);
      rainbowScrollingText(F("SLICE! SLICE! SQUEEZE!!"), 0, 15, SCROLL_STEP, false);
      break;
    // 2: Fading rainbow sides + scrolling chant with each letter a different color
    case 2:
      rainbow(15);
      rainbowScrollingText(F("SLICE! SLICE! SQUEEZE!!"), 0, 85, SCROLL_STEP, true);
      break;
    // 3: Sound-reactive VU meter on the sides + red "SLICE" / "8738" flashing
    case 3:
      noise(micVolume, analogRead(MAX_PIN), analogRead(MIN_PIN));
      alternatePhrases("SLICE", "8738", rainbowColors[0], 800, 300);
      break;
    // 4: Orange blinking sides + inverted (black on orange) "SLICE" / "8738"
    case 4:
      blink(255, 50, 0);
      alternatePhrases("SLICE", "8738", ORANGE, 3000, 0, true);
      break;
    // 5: Light up S, L, I, C, E one at a time, with matching matrix text
    case 5:
      cycleAllLetters(255, 50, 0, 2, true);
      break;
    // 6: Letter-by-letter then blink the whole word + scrolling orange chant
    case 6:
      cycleAndBlink();
      coloredScrollingText(F("SLICE! SLICE! SQUEEZE!!"), 0, SCROLL_STEP, ORANGE);
      break;
    // 7: One continuous rainbow wrapping side -> matrix -> side, text in shadow
    case 7:
      verticalRainbow();
      break;
    // 8: White "headlights" fading in and out with inverted text
    case 8:
      headLights();
      break;
    // 9: Solid orange sides + team logo, lighting up each slice segment and
    //    its matching digit of 8738 in turn
    case 9:
      solidColor(255, 50, 0);
      showLogo(true);
      break;
    default:
      matrix.clear();
      strip.clear();
      showMatrix();
      showStrip();
      break;
  }
}

// ---------------------------------------------------------------------------
// Side panel effects
//
// Pattern every effect follows:
//   if (!currentSideMethod.equals("myEffect")) { ...first-run setup... }
//   look at timeInSideSequence to decide what to draw this frame
//   call restartSide() when one full cycle is done
// ---------------------------------------------------------------------------

/**
 * Flash all the side LEDs: 500ms on in the given color, 500ms off.
 **/
void blink(int red, int green, int blue) {
  // Initialize function at 0 ticks
  if (startSide("blink")) {
  }

  // Show color for 500 ticks
  if (timeInSideSequence < 500) {
    setAll(red, green, blue);
    return;
  }

  // Show black for next 500 ticks
  if (timeInSideSequence < 1000) {
    setAll(0, 0, 0);
    return;
  }

  restartSide();
}

/**
 * All side LEDs one steady color. Only sends to the strip when first selected,
 * so it doesn't slow down the loop.
 **/
void solidColor(int red, int green, int blue) {
  if (startSide("solidColor")) {
    setAll(red, green, blue);
  }
}

/**
 * Set every side LED to the same color and step the colorwheel forward,
 * so the whole cart slowly fades through the rainbow.
 * colorSpeed is the colorwheel jump per frame (bigger = faster).
 **/
void rainbow(int colorSpeed) {
  if (startSide("rainbow")) {
    clearMatrix();
  }

  setAll(sideColorWheel.red, sideColorWheel.green, sideColorWheel.blue);
  sideColorWheel.rollingRainbow(colorSpeed);

  restartSide();
}

/**
 * Sound-reactive VU meter. Lights rows from the bottom of the E upward based
 * on microphone volume, jumping up instantly on loud sounds and falling back
 * down one row per frame.
 *
 * @param volume raw mic reading (0-1023)
 * @param max    raw pot reading (0-1023) - turn up to make it more sensitive
 * @param min    raw pot reading (0-1023) - noise floor subtracted from the mic
 **/
void noise(int volume, int max, int min) {
  if (startSide("noise")) {
    clearMatrix();
  }

  // Subtract the noise floor (pots are divided by 4 to bring them to 0-255)
  volume -= min / 4;
  if (volume < 0) {
    volume = 0;
  }
  // Scale up by the gain set by the two pots
  // NOTE: this is integer division, so the gain is always a whole number
  volume = volume * (255 / ((255 - max / 4) - min / 4));

  // Convert volume into a number of rows (~255 / 43 rows)
  int updatedRows = round(volume / 5.93);

  // Peak-hold: only ever jump up here, the fall happens at the bottom
  if (updatedRows > rows) {
    rows = updatedRows;
  }

  if (rows > 41) {
    rows = 41;
  }

  strip.clear();

  for (int i = 0; i < rows; i++) {
    setRow(i, 255, 50, 0);
  }

  updateLeds();

  // Let the meter fall by one row each frame
  if (rows > 0) {
    rows--;
  }
}

// headLights(): true while fading up, false while fading down
bool goingBrighter = true;

/**
 * Simple method that makes the front sign light up in white like a flashlight while fading the letters
 * The sides fade 0 -> 255 over ~2 seconds, then 255 -> 0 over the next ~2 seconds.
 * The matrix shows inverted (black on white) "SLICE" then "8738", ~1 second each.
 **/
void headLights() {
  if (startSide("headLights")) {
    goingBrighter = true;
    strip.clear();
    clearMatrix();
  } else {
    // Don't run this if we are past and will reset
    if (timeInSideSequence <= 2040){
      // Get a value from 0 to 255 depending on how far we are in the time sequence
      int fade = (timeInSideSequence % 2040)/8;
      if (!goingBrighter){
        // Invert if we are fading out
        fade = 255 - fade;     
      }
      setAll(fade,fade,fade);
    }

    // For 1 second show SLICE, then 1 second 8738
    if (timeInSideSequence <= 255 * 4) {
        showText("SLICE", WHITE, true, true);
        return;
    } else if (timeInSideSequence <= 255 * 8) {
        showText("8738", WHITE, true, true);
        return;
    } else {
      sideSequenceStartTime = time;
      goingBrighter = !goingBrighter;
    }

  }
}

// verticalRainbow(): the color at the start of the rainbow. It moves forward a
// little every frame, so the rainbow flows across the cart.
// (This used to replay the wheel from red every frame using a frame counter,
// which got slower every frame and overflowed after ~20s, freezing program 7.)
ColorWheel verticalRainbowStart = ColorWheel();

/**
 * This will create a wrapping rainbow from one side of the cart, going over the matrix display
 * and coming back to the other side, splaying 8738 and SLICE as a shadow on the rainbow
 *
 * Each frame draws one long rainbow made of: 3 right-side columns, then all 64
 * matrix columns, then 3 left-side columns. The start color shifts every frame
 * so the rainbow appears to move.
 **/
void verticalRainbow() {
  if (startSide("verticalRainbow")) {
    strip.clear();
    clearMatrix();
    verticalRainbowStart.resetRainbow();
    verticalRainbowStart.resetRainbow(15);
  } else {
    int rainbowStep = 15;

    // Move the start color along one step per column drawn (6 side + 64 matrix),
    // then draw the whole rainbow from there
    verticalRainbowStart.resetRainbow(6 + matrix.width());
    matrixColorWheel = verticalRainbowStart;

    // Right side panel columns
    for (int l = 1; l <= 3; l++){
      setColumn(l, matrixColorWheel.red, matrixColorWheel.green, matrixColorWheel.blue);
      matrixColorWheel.rollingRainbow(rainbowStep);
    }

    // Every column of the matrix, left to right, as a full-height vertical line
    for (int i = 0; i < matrix.width(); i++){
        //matrix.drawLine(i, 0, i, 16, matrix.Color(red, green, blue));
        matrix.drawFastVLine(i, 0, 16, matrix.Color(matrixColorWheel.red, matrixColorWheel.green, matrixColorWheel.blue));
        matrixColorWheel.rollingRainbow(rainbowStep);
    }

    // Left side panel columns
    for (int l = 4; l <= 6; l++){
      setColumn(l, matrixColorWheel.red, matrixColorWheel.green, matrixColorWheel.blue);
      matrixColorWheel.rollingRainbow(rainbowStep);
    }

    // Switch back and forth every 2 seconds. Text is drawn in black on top of
    // the rainbow (clear = false) so it shows up as a shadow.
    if (timeInSideSequence % 4000 < 2000){
      showText("8738", BLACK, false, false);
    } else {
      showText("SLICE", BLACK, false, false);
    }

    showMatrix();
    showStrip();
  }

}

/**
 * Cycles through each of the letters lighting up S L I C E
 * Each letter is shown for 142ms * speed (speed 2 = ~0.28s per letter).
 * If includeMatrix is true, the matrix also changes along with the letters:
 * colored "SLICE", then inverted "SLICE", then a scrolling "SQUEEZE!".
 **/
void cycleAllLetters(int red, int green, int blue, int speed, boolean includeMatrix) {
  if (startSide("cycleAllLetters")) {
    clearMatrix();
  }

  strip.clear();

  // S
  if (timeInSideSequence < 142 * speed) {
    restartScroll();
    setLetter(1, red, green, blue);
    if (includeMatrix) showText("SLICE", rainbowColors[1]);
    return;
  }

  // L
  if (timeInSideSequence < 142 * speed * 2) {
    setLetter(2, red, green, blue);
    if (includeMatrix) showText("SLICE", WHITE, true, true);
    return;
  }

  // I
  if (timeInSideSequence < 142 * speed * 3) {
    setLetter(3, red, green, blue);
    if (includeMatrix) coloredScrollingText(F("SQUEEZE!"), 0, 3, rainbowColors[3]);
    return;
  }

  // C
  if (timeInSideSequence < 142 * speed * 4) {
    setLetter(4, red, green, blue);
    if (includeMatrix) coloredScrollingText(F("SQUEEZE!"), 0, 3, rainbowColors[3]);
    return;
  }

  // E
  if (timeInSideSequence < 142 * speed * 5) {
    setLetter(5, red, green, blue);   
    if (includeMatrix) coloredScrollingText(F("SQUEEZE!"), 0, 3, rainbowColors[3]);
    return;
  }

  restartSide();
}


// Which phase we are in to turn on different groups
int groupPhase = 0;

/**
 * Cycles through all of the letters than blinks the whole name
 * Phase 1 runs cycleAllLetters() once, phases 2-4 run blink() three times,
 * then it starts over. Moves on each time the current effect sets sideFinished.
 **/
void cycleAndBlink() {
  if (groupPhase == 0) {
    sideFinished = false;
    groupPhase++;
  }

  if (sideFinished == true) {
    if (groupPhase >= 4) {
      groupPhase = 0;
      return;
    } else {
      groupPhase++;
      sideFinished = false;
    }
  }

  if (sideFinished == false && groupPhase == 1) {
    cycleAllLetters(255, 50, 0, 2, false);
  }

  if (sideFinished == false && groupPhase > 1) {
    blink(255, 50, 0);
  }
}

// ---------------------------------------------------------------------------
// Side panel helpers
// ---------------------------------------------------------------------------

/**
 * Sets a particular letter to a color
 * letter: 1 = S, 2 = L, 3 = I, 4 = C, 5 = E (each is a range of rows)
 **/
void setLetter(int letter, int red, int green, int blue) {
  if (letter == 1) {
    for (int i = 34; i < 43; i++) {
      setRow(i, red, green, blue);
    }
  } else if (letter == 2) {
    for (int i = 26; i < 34; i++) {
      setRow(i, red, green, blue);
    }
  } else if (letter == 3) {
    for (int i = 17; i < 26; i++) {
      setRow(i, red, green, blue);
    }
  } else if (letter == 4) {
    for (int i = 8; i < 17; i++) {
      setRow(i, red, green, blue);
    }
  } else if (letter == 5) {
    for (int i = 0; i < 8; i++) {
      setRow(i, red, green, blue);
    }
  }

  updateLeds();
}

/**
 * Sets a particular row to a color
 * Update a row of lights from 0 (E1) to 42 (S9)
 * @param colors the rgb value to set to
 **/
void setRow(int row, int red, int green, int blue) {
  //right
  strip.setPixelColor(row, red, green, blue);
  strip.setPixelColor(84 - row, red, green, blue);
  strip.setPixelColor(85 + row, red, green, blue);

  //left
  strip.setPixelColor(171 + row, red, green, blue);
  strip.setPixelColor(256 - row, red, green, blue);
  strip.setPixelColor(257 + row, red, green, blue);
}

/**
 * Sets a particular column to a color
 * col 1-3 are the right side panel, 4-6 are the left side panel.
 * Does not call strip.show() - the caller must do that.
 * @param colors the rgb value to set to
 **/
void setColumn(int col, int red, int green, int blue) {
  switch (col) {
    case 1:
      updateLEDs(0, 41, red, green, blue);
      break;
    case 2:
      updateLEDs(42, 84, red, green, blue);
      break;
    case 3:
      updateLEDs(85, 127, red, green, blue);
      break;
    case 4:
      updateLEDs(171, 213, red, green, blue);
      break;
    case 5:
      updateLEDs(214, 256, red, green, blue);
      break;
    case 6:
      updateLEDs(257, 298, red, green, blue);
      break;
  }
}

/**
 * Updates all LEDs to a color in the range provided (inclusive)
 * Not to be confused with updateLeds() below, which pushes the strip to the LEDs.
 **/
void updateLEDs(int start, int end, int red, int green, int blue) {
  for (int i = start; i <= end; i++) {
    strip.setPixelColor(i, red, green, blue);
  }
}

// Sets all the LEDs to the provided color
/// @param colors the rgb value to set to
void setAll(int red, int green, int blue) {
  for (int i = 0; i < NUM_LEDS; i++) {
    strip.setPixelColor(i, red, green, blue);
  }

  updateLeds();
}

// Display the LED states
// Always forces the internal illuminator (LEDs 128-170) to white, then sends
// the whole strip out at brightness 100/255.
void updateLeds() {
  for (int i = 128; i <= 170; i++) {
    strip.setPixelColor(i, 255, 255, 255);
  }

  strip.setBrightness(100);
  showStrip();
}

/**
 * For the Side panels
 * Resets the start time and sets finished to true if any cleanup needs to be done
 **/
void restartSide() {
  sideSequenceStartTime = time;
  sideFinished = true;
}

/**
 * For the Matrix Display
 * Resets the start time and sets finished to true if any cleanup needs to be done
 **/
void restartScroll(){
  scrollSequenceStartTime = time;
  scrollFinished = true;
  matrixOffset = matrix.width();
}

// ---------------------------------------------------------------------------
// Matrix display effects
//
// Same pattern as the side effects, but using currentScrollMethod,
// timeInScrollSequence and restartScroll().
// Text is size 2: 12px wide per character, 16px tall, so ~5 characters fit.
// ---------------------------------------------------------------------------

/**
 * Switches back and forth showing two phrases in a paricular color,
 * displaying each for the displayTime and pausing in black for the time in pauseBuffer
 **/
void alternatePhrases(String text, String altText, uint16_t color, int displayTime, int pauseBuffer){
  alternatePhrases(text, altText, color, displayTime, pauseBuffer, false);
}

/**
 * Switches back and forth showing two phrases in a paricular color, 
 * displaying each for the displayTime and pausing in black for the time in pauseBuffer
 * has the option to be inverted where the color is used as the background and the text is in shadow/black
 **/
void alternatePhrases(String text, String altText, uint16_t color, int displayTime, int pauseBuffer, bool inverted){
  if (startScroll("alternatePhrases")) {
  }

  // In order to try to stay sane, we declare the times that we need to pay attention to
  // We obviously start at zero
  int startTime = 0;
  // This is when the first phrase stops being shown
  int endTimeText = displayTime;
  // This is when we stop showing the black pause buffer (if > 0)
  int endTimePause = endTimeText + pauseBuffer;
  // This is when we stop showing the seconf phrase
  int endTimeAltText = endTimePause + displayTime;
  // This is when we stop showing the final black pause buffer (if > 0)
  int endTime = endTimeAltText + pauseBuffer;

  // Calculate if we are in the alternate timeslot
  bool alternate = timeInScrollSequence >= endTimePause;

  // Give .2 seconds clear between words
  if ((timeInScrollSequence > endTimeText && timeInScrollSequence <= endTimePause) 
      || (timeInScrollSequence > endTimeAltText && timeInScrollSequence <= endTime)) 
  {
    // Show blank
    clearMatrix();
  } else if (timeInScrollSequence > endTime){
    // Start over
    restartScroll();
  } else {
    // Show text
    String output = text;
    if (alternate){
      output = altText;
    }
    showText(output, color, true, inverted);
  }
}

/**
 * Simply shows text in a color, centered
 **/
void showText(String text, uint16_t color){
  showText(text, color, true, false);
}

/**
 * Simply shows text in a color, centered
 * Has the option in invert using the color as background and the text as black
 * Pass clear = false to draw text on top of whatever is already on the matrix.
 * Centering assumes text size 2 and only works for up to 5 characters.
 **/
void showText(String text, uint16_t color, bool clear, bool inverted){
  if (clear) matrix.clear();
  
  if (inverted){
    matrix.fillScreen(color);
    matrix.setTextColor(BLACK);
  } else {
    matrix.setTextColor(color);
  }

  // Left padding = (display width - text width) / 2, +1 to account for the trailing gap
  int padding = (int)(((GRID_NUM_COLS * GRID_TILES_X) - ((int)text.length() * CHAR_PIX_WIDTH * 2))/2) + 1;
  matrix.setCursor(padding, 1);
  matrix.print(text);
  showMatrix();

}

/**
 * Draw the team logo from the LOGO table.
 * @param litSegment which slice segment / digit (0-3) to highlight, or -1 for none
 **/
void drawLogo(int litSegment) {
  matrix.clear();
  for (int y = 0; y < LOGO_HEIGHT; y++) {
    for (int x = 0; x < LOGO_WIDTH; x++) {
      // Read the hex digit out of flash and turn it into a number 0-15
      char c = pgm_read_byte(&LOGO[y][x]);
      int code = (c <= '9') ? c - '0' : c - 'A' + 10;

      uint16_t color = BLACK;
      if (code == 1) {
        color = ORANGE;
      } else if (code == 2) {
        color = WHITE;
      } else if (code >= 4 && code <= 7) {
        // Inside of a slice segment: only filled when it's the lit one
        if (code - 4 == litSegment) color = DIM_CYAN;
      } else if (code >= 8 && code <= 11) {
        // A digit of 8738: white when highlighted, cyan otherwise
        color = (code - 8 == litSegment) ? WHITE : CYAN;
      }

      if (color != BLACK) matrix.drawPixel(x, y, color);
    }
  }
  showMatrix();
}

// How long each step of the logo animation lasts, in ms
#define LOGO_STEP_MS 600
// The last logo frame drawn, so we only redraw when it changes
int lastLogoStep = -1;

/**
 * Show the team logo. With animate = true it steps through:
 *   plain logo -> segment 0 + "8" -> segment 1 + "7" -> segment 2 + "3" -> segment 3 + "8"
 * then repeats. With animate = false it just shows the plain logo.
 **/
void showLogo(bool animate) {
  if (startScroll("showLogo")) {
    lastLogoStep = -1;  // force a redraw
  }

  int step = 0;
  if (animate) {
    if (timeInScrollSequence >= LOGO_STEP_MS * 5) {
      restartScroll();
    } else {
      step = timeInScrollSequence / LOGO_STEP_MS;
    }
  }

  // Drawing 1024 pixels takes a while, so skip it if nothing changed
  if (step == lastLogoStep) return;
  lastLogoStep = step;

  // step 0 = nothing lit, steps 1-4 = segments 0-3
  drawLogo(step - 1);
}

/**
 * Clear out the matrix display
 **/
void clearMatrix(){
  matrix.clear();
  showMatrix();
}

/**
 * Shows text in a color, scrolling starting off the right through the screen until gone, then repeats
 * and the move Delta determines how fast to go by moving that many LEDs per run
 **/
void coloredScrollingText(String scrollText, int pauseTime, int moveDelta, uint16_t color){
  if (startScroll("coloredScrollingText")) {
    clearMatrix();
    resetRainbowScroll();
  }

  matrix.setTextColor(color);
  scrollingText(scrollText, pauseTime, moveDelta);
}

/**
 * Shows text rolling through colors, scrolling starting off the right through the screen until gone, then repeats
 * Has the option to show the letters as rainbow vs. changing the whole phrase
 * The speed of the color change is controlled by colorspeed (how many RGB vales to skip) 
 * and the move Delta determines how fast to go by moving that many LEDs per run
 **/
void rainbowScrollingText(String scrollText, int pauseTime, int colorSpeed, int moveDelta, bool rainbowText){
  if (startScroll("rainbowScrollingText")) {
    clearMatrix();
    resetRainbowScroll();
  }  

  if (!rainbowText){
    matrix.setTextColor(matrix.Color(matrixColorWheel.red, matrixColorWheel.green, matrixColorWheel.blue));
    matrixColorWheel.rollingRainbow(colorSpeed);
  }
  scrollingText(scrollText, pauseTime, moveDelta, rainbowText, colorSpeed);
}

/**
 * Draw one frame of scrolling text using whatever text color is already set.
 **/
void scrollingText(String scrollText, int pauseTime, int moveDelta) {
  scrollingText(scrollText, pauseTime, moveDelta, false, 0);
}

/**
 * Core scroller used by all scrolling text effects. Draws one frame:
 * prints the text at matrixOffset, then moves it moveDelta pixels left.
 * When the text has completely left the screen it restarts from the right.
 *
 * @param pauseTime  delay in ms after each frame (this one does block)
 * @param rainbow    if true each character gets the next colorwheel color
 * @param colorSpeed colorwheel jump between characters when rainbow is true
 **/
void scrollingText(String scrollText, int pauseTime, int moveDelta, bool rainbow, int colorSpeed) {
  matrix.clear();

  matrix.setCursor(matrixOffset, 1);
  if (rainbow){
    matrixColorWheel.resetRainbow();
    for (int i = 0; i < scrollText.length(); i++){
      matrixColorWheel.rollingRainbow(colorSpeed);
      matrix.setTextColor(matrix.Color(matrixColorWheel.red, matrixColorWheel.green, matrixColorWheel.blue));
      matrix.print(scrollText.charAt(i));
    }
  } else {
    matrix.print(scrollText);
  }

  // Width of the whole phrase in pixels at text size 2
  int length = (int)scrollText.length() * CHAR_PIX_WIDTH * 2;
  matrixOffset = matrixOffset - moveDelta;
  if (matrixOffset < -length) {
    restartScroll();
  }
  showMatrix();
  delay(pauseTime);
}
