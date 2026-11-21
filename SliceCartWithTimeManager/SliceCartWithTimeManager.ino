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
#define LED_PROGRAMS 20

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

// Position, size and rotation of the orange slice for the animated logo
// programs (see drawSlice). Defined up here so the Arduino IDE's automatic
// function prototypes can use it.
struct SlicePose {
  float cx, cy;   // where the middle of the flat edge goes on the matrix
  float r;        // radius in pixels
  float angle;    // degrees clockwise. 0 = flat edge on top, rind below.
                  // 45 = tilted like the real logo. 180 = upside down (a dome).
  float sx, sy;   // stretch along / across the flat edge (1 = normal,
                  // sx = -1 mirrors it left to right)
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

// What the side letters were last set to, so we only send to the strip
// (which takes ~9ms) when something actually changed
uint32_t lastSideState = 0xFFFFFFFF;

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
    lastSideState = 0xFFFFFFFF;  // make the logo programs redraw the sides
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
    // ----- Team logo programs -----
    // 1: Team logo, each slice segment + digit of 8738 lighting up in turn
    //    while the side letters turn cyan one at a time
    case 1:
      animatedLogo();
      break;
    // 2: Slice pops up, fills in, then slides into place as the text types in
    case 2:
      logoZoom();
      break;
    // 3: Slice spins like a wheel and lands at a 45 degree tilt
    case 3:
      logoSpin();
      break;
    // 4: Slice rolls across uncovering "SLICE 8738", then rolls back
    case 4:
      logoRoll();
      break;
    // 5: Slice gets squeezed and sprays juice, then "SLICE! SQUEEZE!"
    case 5:
      logoSqueeze();
      break;
    // 6: Slice rises like a sun, then the digits fly out into place
    case 6:
      logoSunrise();
      break;

    // ----- Rainbow programs -----
    // 7: Logo with a soft rainbow sheen sweeping across it every few seconds
    case 7:
      prismLogo();
      break;
    // 8: Big SLICE / 8738 filled with a sweeping rainbow, rainbow up the sides
    case 8:
      rainbowWave();
      break;
    // 9: Swirling rainbow plasma with SLICE / 8738 cut out in black
    case 9:
      plasma();
      break;
    // 10: Rainbow confetti twinkling behind white SLICE / 8738
    case 10:
      confetti();
      break;
    // 11: Sound-reactive rainbow equalizer bars + rainbow sound meter sides
    case 11:
      rainbowBeat();
      break;

    // ----- Original programs -----
    // 12: Fading rainbow sides + scrolling chant that changes color as a whole
    case 12:
      rainbow(15);
      rainbowScrollingText(F("SLICE! SLICE! SQUEEZE!!"), 0, 15, SCROLL_STEP, false);
      break;
    // 13: Fading rainbow sides + scrolling chant with each letter a different color
    case 13:
      rainbow(15);
      rainbowScrollingText(F("SLICE! SLICE! SQUEEZE!!"), 0, 85, SCROLL_STEP, true);
      break;
    // 14: Sound-reactive VU meter on the sides + red "SLICE" / "8738" flashing
    case 14:
      noise(micVolume, analogRead(MAX_PIN), analogRead(MIN_PIN));
      alternatePhrases("SLICE", "8738", rainbowColors[0], 800, 300);
      break;
    // 15: Orange blinking sides + inverted (black on orange) "SLICE" / "8738"
    case 15:
      blink(255, 50, 0);
      alternatePhrases("SLICE", "8738", ORANGE, 3000, 0, true);
      break;
    // 16: Light up S, L, I, C, E one at a time, with matching matrix text
    case 16:
      cycleAllLetters(255, 50, 0, 2, true);
      break;
    // 17: Letter-by-letter then blink the whole word + scrolling orange chant
    case 17:
      cycleAndBlink();
      coloredScrollingText(F("SLICE! SLICE! SQUEEZE!!"), 0, SCROLL_STEP, ORANGE);
      break;
    // 18: One continuous rainbow wrapping side -> matrix -> side, text in shadow
    case 18:
      verticalRainbow();
      break;
    // 19: White "headlights" fading in and out with inverted text
    case 19:
      headLights();
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
//   if (startSide("myEffect")) { ...first-run setup... }
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
    // Only reset the matrix if this effect owns it; otherwise (program 17)
    // it would cut off the chant scrolling alongside every time S lights up.
    if (includeMatrix) restartScroll();
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
  fillLetter(letter, red, green, blue);
  updateLeds();
}

/**
 * Same as setLetter() but doesn't send to the strip, so you can color several
 * letters and then call updateLeds() once.
 **/
void fillLetter(int letter, int red, int green, int blue) {
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
 * @param litMask which slice segments / digits to highlight: bit 0 = the
 *                leftmost segment and the first "8", up to bit 3. 0 = none.
 **/
void drawLogo(uint8_t litMask) {
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
        // Inside of a slice segment: only filled when it's lit
        if (litMask & (1 << (code - 4))) color = DIM_CYAN;
      } else if (code >= 8 && code <= 11) {
        // A digit of 8738: white when highlighted, cyan otherwise
        color = (litMask & (1 << (code - 8))) ? WHITE : CYAN;
      }

      if (color != BLACK) matrix.drawPixel(x, y, color);
    }
  }
  showMatrix();
}

// ---------------------------------------------------------------------------
// Side letters for the logo programs
// ---------------------------------------------------------------------------

// Letters as a bitmask: bit 0 = S, 1 = L, 2 = I, 3 = C, 4 = E
#define ALL_LETTERS 0x1F
// Side strip colors (raw strip RGB, not matrix colors)
#define SIDE_ORANGE_R 255
#define SIDE_ORANGE_G 50
#define SIDE_ORANGE_B 0
#define SIDE_CYAN_R 0
#define SIDE_CYAN_G 160
#define SIDE_CYAN_B 255

// (lastSideState is declared with the other shared state near the top)

// Scale a 0-255 color channel by a 0-255 brightness
uint8_t scale8(uint8_t c, uint8_t level) {
  return ((uint16_t)c * level) / 255;
}

/**
 * Color the side letters: letters in cyanMask are cyan, the other letters in
 * litMask are orange, the rest are off. level is the brightness (0-255).
 **/
void sideLetters(uint8_t cyanMask, uint8_t litMask, uint8_t level) {
  uint32_t state = ((uint32_t)level << 16) | ((uint32_t)cyanMask << 8) | litMask;
  if (state == lastSideState) return;
  lastSideState = state;

  strip.clear();
  for (int l = 0; l < 5; l++) {
    if (cyanMask & (1 << l)) {
      fillLetter(l + 1, scale8(SIDE_CYAN_R, level), scale8(SIDE_CYAN_G, level), scale8(SIDE_CYAN_B, level));
    } else if (litMask & (1 << l)) {
      fillLetter(l + 1, scale8(SIDE_ORANGE_R, level), scale8(SIDE_ORANGE_G, level), scale8(SIDE_ORANGE_B, level));
    }
  }
  updateLeds();
}

/**
 * All side letters one color, only sending to the strip when it changes.
 **/
void sideColor(uint8_t red, uint8_t green, uint8_t blue) {
  uint32_t state = 0x01000000UL | ((uint32_t)red << 16) | ((uint32_t)green << 8) | blue;
  if (state == lastSideState) return;
  lastSideState = state;
  setAll(red, green, blue);
}

/**
 * Light the side letters from the bottom up like a rising sun: the bottom
 * `rows` rows (0-43) in cyan, the rest off.
 **/
void sideRows(int rows) {
  uint32_t state = 0x02000000UL | rows;
  if (state == lastSideState) return;
  lastSideState = state;
  strip.clear();
  for (int i = 0; i < rows; i++) setRow(i, SIDE_CYAN_R, SIDE_CYAN_G, SIDE_CYAN_B);
  updateLeds();
}

// A mask of the first n letters (n = 0-5), e.g. 2 -> S and L
uint8_t firstLetters(int n) {
  if (n <= 0) return 0;
  if (n >= 5) return ALL_LETTERS;
  return (1 << n) - 1;
}

// ---------------------------------------------------------------------------
// Program: animated logo
// ---------------------------------------------------------------------------

// How long each step of the logo animation lasts, in ms
#define LOGO_STEP_MS 600
// The last logo frame drawn, so we only redraw when it changes
int lastLogoStep = -1;

/**
 * The team logo with the side letters turning cyan one at a time:
 *   step 0: plain logo, all side letters cyan
 *   steps 1-4: segment N + its digit of 8738 light up, letters S, L, I, C turn orange
 *   step 5: the whole slice and 8738 light up and E turns orange
 * then it starts over.
 **/
void animatedLogo() {
  startSide("animatedLogo");
  if (startScroll("animatedLogo")) {
    lastLogoStep = -1;  // force a redraw
  }

  int step = (timeInScrollSequence / LOGO_STEP_MS) % 6;

  // Drawing 1024 pixels takes a while, so skip it if nothing changed
  if (step == lastLogoStep) return;
  lastLogoStep = step;

  uint8_t litMask = 0;
  if (step >= 1 && step <= 4) litMask = 1 << (step - 1);
  if (step == 5) litMask = 0x0F;
  drawLogo(litMask);
  // Letters start cyan and turn orange one per step: S, L, I, C, then E
  sideLetters(ALL_LETTERS & ~firstLetters(step), ALL_LETTERS, 255);
}

// ---------------------------------------------------------------------------
// Orange slice drawn from shapes
//
// The LOGO bitmap above can only be drawn in one place at one size. For the
// animated logo programs the slice is drawn from triangles and lines instead,
// so it can be moved, zoomed, spun and squashed.
//
// The slice is described in its own coordinates: u runs along the flat edge
// (-r on the left to +r on the right) and v goes from the flat edge (0) out to
// the rind (r). Segment 0 is the leftmost wedge, segment 3 the rightmost,
// matching the digits 8 7 3 8.
// ---------------------------------------------------------------------------

// The rind is drawn as this many straight lines (4 per segment)
#define SLICE_ARC_STEPS 16

// (struct SlicePose is defined near the top of the file)

// cos/sin of the points around the rind, worked out once
float sliceArcCos[SLICE_ARC_STEPS + 1];
float sliceArcSin[SLICE_ARC_STEPS + 1];
bool sliceTablesReady = false;

void initSliceTables() {
  if (sliceTablesReady) return;
  for (int i = 0; i <= SLICE_ARC_STEPS; i++) {
    // From the left end of the flat edge (180 degrees) round to the right end (0)
    float a = radians(180.0 - i * (180.0 / SLICE_ARC_STEPS));
    sliceArcCos[i] = cos(a);
    sliceArcSin[i] = sin(a);
  }
  sliceTablesReady = true;
}

// Convert a point in slice coordinates (u, v) to a matrix pixel
void sliceToScreen(const SlicePose &p, float u, float v, int16_t &x, int16_t &y) {
  float a = radians(p.angle);
  float ca = cos(a), sa = sin(a);
  u *= p.sx;
  v *= p.sy;
  x = (int16_t)floor(p.cx + u * ca - v * sa + 0.5);
  y = (int16_t)floor(p.cy + u * sa + v * ca + 0.5);
}

// Where to draw the tiny digit for segment k (its top-left corner)
void sliceDigitPos(const SlicePose &p, int k, int16_t &x, int16_t &y) {
  int mid = k * 4 + 2;  // the rind point in the middle of the segment
  sliceToScreen(p, 0.62 * p.r * sliceArcCos[mid], 0.62 * p.r * sliceArcSin[mid], x, y);
  x -= 1;  // tiny digits are 3x5, so shift by half
  y -= 2;
}

/**
 * Draw a 3x5 pixel digit (8, 7 or 3). The normal 5x7 font is too big to fit
 * between the dividers of the slice.
 **/
void drawTinyDigit(int x, int y, char c, uint16_t color) {
  // 5 rows of 3 pixels, top row in the highest bits
  uint16_t bits = 0b111101111101111;            // 8
  if (c == '7') bits = 0b111001010010010;
  if (c == '3') bits = 0b111001011001111;
  for (int row = 0; row < 5; row++) {
    for (int col = 0; col < 3; col++) {
      if (bits & (1 << (14 - row * 3 - col))) matrix.drawPixel(x + col, y + row, color);
    }
  }
}

/**
 * Draw the orange slice.
 * @param litMask    segments to fill with fillColor (bit 0 = leftmost); the
 *                   rest are filled black so the slice covers what's behind it
 * @param showDigits draw 8 7 3 8 inside the segments (only when it's big enough)
 **/
void drawSlice(const SlicePose &p, uint8_t litMask, uint16_t fillColor, bool showDigits) {
  uint16_t segColors[4];
  for (int k = 0; k < 4; k++) segColors[k] = (litMask & (1 << k)) ? fillColor : BLACK;
  drawSliceSegments(p, segColors, showDigits);
}

/**
 * Draw the orange slice with each segment filled in its own color
 * (segColors[0] = leftmost). Digits in filled segments are drawn white.
 **/
void drawSliceSegments(const SlicePose &p, const uint16_t segColors[4], bool showDigits) {
  initSliceTables();
  if (p.r < 0.5) return;

  int16_t cx, cy;
  sliceToScreen(p, 0, 0, cx, cy);

  // Points around the rind
  int16_t ax[SLICE_ARC_STEPS + 1], ay[SLICE_ARC_STEPS + 1];
  for (int i = 0; i <= SLICE_ARC_STEPS; i++) {
    sliceToScreen(p, p.r * sliceArcCos[i], p.r * sliceArcSin[i], ax[i], ay[i]);
  }

  // Fill each segment as a fan of triangles from the center
  for (int k = 0; k < 4; k++) {
    uint16_t color = segColors[k];
    for (int j = k * 4; j < k * 4 + 4; j++) {
      matrix.fillTriangle(cx, cy, ax[j], ay[j], ax[j + 1], ay[j + 1], color);
    }
  }

  // Rind, plus a second line inside it when it's big enough to look thin
  for (int j = 0; j < SLICE_ARC_STEPS; j++) {
    matrix.drawLine(ax[j], ay[j], ax[j + 1], ay[j + 1], ORANGE);
  }
  if (p.r >= 9) {
    int16_t px, py, qx, qy;
    for (int j = 0; j < SLICE_ARC_STEPS; j++) {
      sliceToScreen(p, (p.r - 1) * sliceArcCos[j], (p.r - 1) * sliceArcSin[j], px, py);
      sliceToScreen(p, (p.r - 1) * sliceArcCos[j + 1], (p.r - 1) * sliceArcSin[j + 1], qx, qy);
      matrix.drawLine(px, py, qx, qy, ORANGE);
    }
  }

  // Flat edge
  matrix.drawLine(ax[0], ay[0], ax[SLICE_ARC_STEPS], ay[SLICE_ARC_STEPS], ORANGE);

  // Dividers between the segments
  for (int j = 4; j < SLICE_ARC_STEPS; j += 4) {
    matrix.drawLine(cx, cy, ax[j], ay[j], ORANGE);
  }

  // Half-circle hub in the middle of the flat edge
  float hub = p.r / 6;
  if (hub < 1) hub = 1;
  int16_t hx0, hy0, hx1, hy1;
  for (int j = 0; j < SLICE_ARC_STEPS; j += 4) {
    sliceToScreen(p, hub * sliceArcCos[j], hub * sliceArcSin[j], hx0, hy0);
    sliceToScreen(p, hub * sliceArcCos[j + 4], hub * sliceArcSin[j + 4], hx1, hy1);
    matrix.fillTriangle(cx, cy, hx0, hy0, hx1, hy1, ORANGE);
  }

  // Digits inside the segments, drawn last so the lines don't cut through them
  if (showDigits && p.r >= 12) {
    for (int k = 0; k < 4; k++) {
      int16_t dx, dy;
      sliceDigitPos(p, k, dx, dy);
      drawTinyDigit(dx, dy, "8738"[k], segColors[k] != BLACK ? WHITE : CYAN);
    }
  }
}

// A slice pose with the usual defaults (upright, normal size)
SlicePose slicePose(float cx, float cy, float r, float angle) {
  SlicePose p;
  p.cx = cx;
  p.cy = cy;
  p.r = r;
  p.angle = angle;
  p.sx = 1;
  p.sy = 1;
  return p;
}

// ---------------------------------------------------------------------------
// Animation helpers
// ---------------------------------------------------------------------------

float clamp01(float t) { return t < 0 ? 0 : (t > 1 ? 1 : t); }
float lerp(float a, float b, float t) { return a + (b - a) * t; }

// How far time t is through the window [start, end], from 0 to 1
float phase(unsigned long t, unsigned long start, unsigned long end) {
  if (t <= start) return 0;
  if (t >= end) return 1;
  return (float)(t - start) / (float)(end - start);
}

// Easing curves: take 0-1 and return 0-1, but not in a straight line
float easeInCubic(float t) { t = clamp01(t); return t * t * t; }
float easeOutCubic(float t) { t = 1 - clamp01(t); return 1 - t * t * t; }
float easeInOutCubic(float t) {
  t = clamp01(t);
  if (t < 0.5) return 4 * t * t * t;
  float f = -2 * t + 2;
  return 1 - f * f * f / 2;
}
// Overshoots past 1 and settles back, like it popped into place
float easeOutBack(float t) {
  t = clamp01(t) - 1;
  return 1 + 2.70158 * t * t * t + 1.70158 * t * t;
}

/**
 * Shared start for the logo animations: claims both the side and matrix, and
 * returns how far (ms) we are into the animation, looping every cycleMs.
 **/
unsigned long logoAnimTime(const char *name, unsigned long cycleMs) {
  startSide(name);
  startScroll(name);
  return timeInScrollSequence % cycleMs;
}

/**
 * Draw the first `count` characters of text in the small (5x7) font.
 * @param fromX only draw characters that start at or right of this x
 *              (pass -1000 to draw them all)
 **/
void drawSmallText(int x, int y, const char *text, uint16_t color, int count, int fromX) {
  for (int i = 0; text[i] && i < count; i++) {
    int cx = x + i * 6;
    if (cx >= fromX) matrix.drawChar(cx, y, text[i], color, color, 1);
  }
}

// Where the text sits next to the slice in the standard logo layout
#define LOGO_TEXT_X 34
#define LOGO_NUMBER_X 37
#define LOGO_NUMBER_Y 9

// ---------------------------------------------------------------------------
// Program: logo zoom
// ---------------------------------------------------------------------------

/**
 * The slice pops up in the middle with 8738 inside it, its segments fill in
 * one at a time, then it slides into the logo position while SLICE and 8738
 * type in beside it. Then it zooms away and starts over.
 * Side letters fade in cyan with the slice and turn orange with the segments.
 **/
void logoZoom() {
  unsigned long t = logoAnimTime("logoZoom", 6400);

  float grow = easeOutBack(phase(t, 0, 900));
  float slide = easeInOutCubic(phase(t, 2600, 3400));
  float away = easeInCubic(phase(t, 5800, 6300));

  float r = 15 * grow * (1 - away);
  SlicePose p = slicePose(lerp(32, 15, slide), lerp(8 - 0.45 * r, 0, slide), r, 0);

  // One more segment every 400ms from 1s in
  int lit = t < 1000 ? 0 : (int)((t - 1000) / 400) + 1;
  if (lit > 4) lit = 4;
  int typed = t < 3200 ? 0 : (int)((t - 3200) / 100);

  matrix.clear();
  drawSlice(p, firstLetters(lit), DIM_CYAN, slide < 0.5);
  if (t < 5800) {
    drawSmallText(LOGO_TEXT_X, 0, "SLICE", WHITE, typed, -1000);
    drawSmallText(LOGO_NUMBER_X, LOGO_NUMBER_Y, "8738", CYAN, typed - 5, -1000);
  }
  showMatrix();

  // Side letters start cyan and turn orange on the same 400ms beat as the
  // segments, E included (it doesn't wait for the text to finish typing)
  int orange = t < 1000 ? 0 : (int)((t - 1000) / 400) + 1;
  uint8_t level = 255 * clamp01(phase(t, 0, 900)) * (1 - away);
  sideLetters(ALL_LETTERS & ~firstLetters(orange), ALL_LETTERS, level);
}

// ---------------------------------------------------------------------------
// Program: logo spin
// ---------------------------------------------------------------------------

/**
 * The slice spins like a wheel and slows down to land at a 45 degree tilt,
 * like the real logo. SLICE / 8738 slide in next to it, then slide out as
 * it starts spinning again. A single cyan letter chases round the side
 * letters while it spins, and they all flash cyan when it lands.
 **/
void logoSpin() {
  unsigned long t = logoAnimTime("logoSpin", 5200);

  // Three full turns, slowing down, ending at 45 degrees.
  // (-1035 is the same as 45 degrees, so the loop is seamless.)
  float spin = easeOutCubic(phase(t, 0, 2600));
  float angle = 45 - 1080 * (1 - spin);
  SlicePose p = slicePose(12, 6.5, 9.5, angle);

  float in = easeOutCubic(phase(t, 2600, 3100));
  float out = easeInCubic(phase(t, 4700, 5200));
  int textX = (int)(lerp(64, 30, in) + 40 * out);

  // Light all segments for a moment when it lands
  bool flash = t >= 2600 && t < 3000;

  matrix.clear();
  drawSlice(p, flash ? 0x0F : 0, DIM_CYAN, false);
  drawSmallText(textX, 0, "SLICE", WHITE, 5, -1000);
  drawSmallText(textX + 3, LOGO_NUMBER_Y, "8738", CYAN, 4, -1000);
  showMatrix();

  if (t < 2600) {
    // Which letter is lit depends on how far round the slice has turned
    int letter = ((int)((angle + 1080) / 72)) % 5;
    sideLetters(1 << letter, ALL_LETTERS, 255);
  } else {
    sideLetters(flash ? ALL_LETTERS : 0, ALL_LETTERS, 255);
  }
}

// ---------------------------------------------------------------------------
// Program: logo roll
// ---------------------------------------------------------------------------

/**
 * The slice rolls in from the right like a wheel, uncovering "SLICE 8738"
 * behind it. After a pause it rolls back the other way, wiping the text off.
 * Side letters turn cyan as the text is uncovered.
 **/
void logoRoll() {
  unsigned long t = logoAnimTime("logoRoll", 6500);
  const float r = 7.5;

  // x = where the middle of the slice is
  float x;
  if (t < 3700) {
    x = lerp(64 + 10, -10, phase(t, 0, 2500));
  } else {
    x = lerp(-10, 64 + 10, phase(t, 3700, 6200));
  }
  // Rolling: turn by the distance travelled divided by the radius
  SlicePose p = slicePose(x, 7.5, r, degrees(x / r));

  // The text is visible to the right of the slice
  int fromX = (int)x;

  matrix.clear();
  drawSmallText(3, 4, "SLICE", WHITE, 5, fromX);
  drawSmallText(3 + 6 * 6, 4, "8738", CYAN, 4, fromX);
  drawSlice(p, 0, DIM_CYAN, false);
  showMatrix();

  // One letter per ~12 pixels of text uncovered
  int uncovered = (int)((64 - x) / 12);
  sideLetters(firstLetters(uncovered), ALL_LETTERS, 255);
}

// ---------------------------------------------------------------------------
// Program: logo squeeze
// ---------------------------------------------------------------------------

#define JUICE_DROPS 18

/**
 * The slice gets squashed, wobbles back into shape and sprays cyan juice
 * drops that arc up and fall. Then the chant builds up one word at a time:
 *     SLICE! SLICE!
 *       SQUEEZE!!
 * The side letters flash cyan on the squeeze and fade back to orange, then
 * flash orange on each SLICE! and go cyan on SQUEEZE!!.
 **/
// When each word of the chant appears in logoSqueeze() (ms into the loop)
#define CHANT_SLICE_1 1900
#define CHANT_SLICE_2 2550
#define CHANT_SQUEEZE 3200

/**
 * Draw text in the small font packed tighter than usual: "!" takes 2 pixels
 * instead of 6 and a space takes 1, so "SLICE! SLICE!" fits in 64 pixels.
 * Returns the x just past the end of the text.
 **/
int drawTightText(int x, int y, const char *text, uint16_t color, bool draw) {
  for (int i = 0; text[i]; i++) {
    if (text[i] == '!') {
      // The "!" glyph sits in the middle column of its 5, so shift it left
      if (draw) matrix.drawChar(x - 2, y, '!', color, color, 1);
      x += 2;
    } else if (text[i] == ' ') {
      x += 1;
    } else {
      if (draw) matrix.drawChar(x, y, text[i], color, color, 1);
      x += 6;
    }
  }
  return x;
}

// Draw tight text centered on the display
void drawTightTextCentered(int y, const char *text, uint16_t color) {
  int width = drawTightText(0, y, text, color, false) - 1;  // no gap after the last char
  drawTightText((matrix.width() - width) / 2, y, text, color, true);
}

void logoSqueeze() {
  unsigned long t = logoAnimTime("logoSqueeze", 4300);

  // How squashed it is: squash for 250ms, then wobble back
  float squash = 0;
  if (t < 250) {
    squash = easeOutCubic(t / 250.0);
  } else if (t < 900) {
    float k = (t - 250) / 650.0;
    squash = cos(k * 3 * PI) * (1 - k);
  }

  float r = 12 * (1 - easeInCubic(phase(t, 1600, 1900)));
  SlicePose p = slicePose(32, 0, r, 0);
  p.sy = 1 - 0.35 * squash;
  p.sx = 1 + 0.25 * squash;
  p.cy = 15 - r * p.sy;  // keep the bottom of the rind on the "table"

  matrix.clear();
  drawSlice(p, 0, DIM_CYAN, false);

  // The chant, building up one word at a time
  if (t >= CHANT_SLICE_2) {
    drawTightTextCentered(0, "SLICE! SLICE!", WHITE);
  } else if (t >= CHANT_SLICE_1) {
    drawTightText(0, 0, "SLICE!", WHITE, true);  // where the first word ends up
  }
  if (t >= CHANT_SQUEEZE) drawTightTextCentered(LOGO_NUMBER_Y, "SQUEEZE!!", CYAN);

  // Juice drops, launched from along the flat edge when it's squeezed
  if (t >= 250) {
    float s = (t - 250) / 1000.0;  // seconds since the squeeze
    for (int i = 0; i < JUICE_DROPS; i++) {
      float x0 = 21 + i * 22.0 / (JUICE_DROPS - 1);
      float vx = (x0 - 32) * 2.2 + ((i * 7) % 5 - 2) * 3;   // spread outwards
      float vy = -22 - ((i * 13) % 7) * 3;                  // up, some higher
      int16_t dx = (int16_t)(x0 + vx * s);
      int16_t dy = (int16_t)(6 + vy * s + 30 * s * s);      // gravity
      // A dim trail where the drop was a moment ago, then the drop itself
      float s0 = s - 0.06;
      matrix.drawPixel((int16_t)(x0 + vx * s0), (int16_t)(6 + vy * s0 + 30 * s0 * s0), DIM_CYAN);
      matrix.drawPixel(dx, dy, CYAN);
    }
  }
  showMatrix();

  // Side: cyan on the squeeze, fading back to orange, then pulsing with the chant
  float fade = phase(t, 400, 1200);
  bool sliceFlash = (t >= CHANT_SLICE_1 && t < CHANT_SLICE_1 + 300) ||
                    (t >= CHANT_SLICE_2 && t < CHANT_SLICE_2 + 300);
  if (t >= CHANT_SQUEEZE) {
    sideColor(SIDE_CYAN_R, SIDE_CYAN_G, SIDE_CYAN_B);
  } else if (t >= CHANT_SLICE_1 && !sliceFlash) {
    // Dim between the words so each SLICE! is a beat
    sideColor(scale8(SIDE_ORANGE_R, 60), scale8(SIDE_ORANGE_G, 60), scale8(SIDE_ORANGE_B, 60));
  } else if (t < 200 || sliceFlash) {
    sideColor(SIDE_ORANGE_R, SIDE_ORANGE_G, SIDE_ORANGE_B);
  } else {
    sideColor(lerp(SIDE_CYAN_R, SIDE_ORANGE_R, fade),
              lerp(SIDE_CYAN_G, SIDE_ORANGE_G, fade),
              lerp(SIDE_CYAN_B, SIDE_ORANGE_B, fade));
  }
}

// ---------------------------------------------------------------------------
// Program: logo sunrise
// ---------------------------------------------------------------------------

/**
 * The slice rises from the bottom upside down like a sunrise, with 8738 in
 * its segments. The segments light up one at a time, then the digits fly out
 * into place as the slice flips over into the logo position and SLICE types
 * in. Then it all sinks away.
 * Side letters fill cyan from the bottom as it rises, then turn orange one at
 * a time on the same 300ms beat as the segments, E included.
 **/
void logoSunrise() {
  unsigned long t = logoAnimTime("logoSunrise", 6600);

  float rise = easeOutCubic(phase(t, 0, 1200));
  float fly = easeInOutCubic(phase(t, 2600, 3600));
  float sink = easeInCubic(phase(t, 5800, 6600));
  float drop = 18 * sink;  // everything moves down this far at the end

  // The sunrise pose: upside down, mirrored so the digits still read 8 7 3 8
  SlicePose sun = slicePose(32, lerp(31, 15, rise), 15, 180);
  sun.sx = -1;
  // Fly into the normal logo pose, flipping over like a coin on the way
  // (squashing its height through zero turns the dome upside down)
  SlicePose p = sun;
  p.cx = lerp(32, 15, fly);
  p.cy = lerp(sun.cy, 0, fly) + drop;
  p.sy = cos(fly * PI);

  int lit = t < 1200 ? 0 : (int)((t - 1200) / 300) + 1;
  if (lit > 4) lit = 4;
  int typed = t < 3400 ? 0 : (int)((t - 3400) / 100);

  matrix.clear();
  if (t < 2600) {
    drawSlice(p, firstLetters(lit), DIM_CYAN, true);
  } else {
    drawSlice(p, 0, DIM_CYAN, false);
    // Each digit flies from its segment to its place in 8738, growing from
    // the tiny font to the normal one halfway there
    for (int k = 0; k < 4; k++) {
      int16_t sx, sy;
      sliceDigitPos(sun, k, sx, sy);
      int16_t x = (int16_t)lerp(sx, LOGO_NUMBER_X + k * 6, fly);
      int16_t y = (int16_t)(lerp(sy, LOGO_NUMBER_Y, fly) + drop);
      if (fly < 0.5) {
        drawTinyDigit(x, y, "8738"[k], WHITE);
      } else {
        matrix.drawChar(x, y, "8738"[k], CYAN, CYAN, 1);
      }
    }
    drawSmallText(LOGO_TEXT_X, (int)drop, "SLICE", WHITE, typed, -1000);
  }
  showMatrix();

  if (t < 1200) {
    sideRows((int)(43 * rise));
  } else {
    int orange = (int)((t - 1200) / 300) + 1;
    sideLetters(ALL_LETTERS & ~firstLetters(orange), ALL_LETTERS, 255 * (1 - sink));
  }
}

// ---------------------------------------------------------------------------
// Rainbow helpers
// ---------------------------------------------------------------------------

/**
 * Rainbow color for a hue from 0-255: 0 = red, 43 = yellow, 85 = green,
 * 128 = cyan, 170 = blue, 213 = magenta, and back round to red. Because it's
 * a byte, adding to the hue always wraps round the rainbow.
 **/
void hueToRGB(uint8_t hue, uint8_t &r, uint8_t &g, uint8_t &b) {
  uint8_t sector = hue / 43;                 // which sixth of the rainbow
  uint8_t f = (hue - sector * 43) * 6;       // 0-252 through that sixth
  switch (sector) {
    case 0:  r = 255;     g = f;       b = 0;       break;  // red -> yellow
    case 1:  r = 255 - f; g = 255;     b = 0;       break;  // yellow -> green
    case 2:  r = 0;       g = 255;     b = f;       break;  // green -> cyan
    case 3:  r = 0;       g = 255 - f; b = 255;     break;  // cyan -> blue
    case 4:  r = f;       g = 0;       b = 255;     break;  // blue -> magenta
    default: r = 255;     g = 0;       b = 255 - f; break;  // magenta -> red
  }
}

// Matrix color for a hue, at a brightness from 0-255
uint16_t hueColor(uint8_t hue, uint8_t level) {
  uint8_t r, g, b;
  hueToRGB(hue, r, g, b);
  return matrix.Color(scale8(r, level), scale8(g, level), scale8(b, level));
}

// Set one row of the side letters (0 = bottom of E, 42 = top of S) to a hue
void setRowHue(int row, uint8_t hue, uint8_t level) {
  uint8_t r, g, b;
  hueToRGB(hue, r, g, b);
  setRow(row, scale8(r, level), scale8(g, level), scale8(b, level));
}

// A smooth wave: sin8(0..255) goes 128 -> 255 -> 128 -> 0 -> 128.
// Uses a table because calling sin() 1000 times a frame is too slow.
uint8_t sinTable[256];
bool sinTableReady = false;

uint8_t sin8(uint8_t x) {
  if (!sinTableReady) {
    for (int i = 0; i < 256; i++) sinTable[i] = 128 + 127 * sin(i * 2 * PI / 256);
    sinTableReady = true;
  }
  return sinTable[x];
}

// The hue that slowly cycles round for the rainbow programs (one lap = ~3s)
uint8_t rainbowBase() {
  return (time / 12) & 0xFF;
}

// "SLICE" for 2 seconds, then "8738" for 2 seconds
const char *sliceOr8738() {
  return (time / 2000) % 2 == 0 ? "SLICE" : "8738";
}

// ---------------------------------------------------------------------------
// Program: prism logo
// ---------------------------------------------------------------------------

// A sheen sweeps across every SHEEN_PERIOD ms and takes SHEEN_SWEEP ms to cross
#define SHEEN_PERIOD 4000
#define SHEEN_SWEEP 1800
// Half the width of the rainbow band, in pixels
#define SHEEN_WIDTH 10
// Whether the plain logo is already showing (so we don't redraw it every frame)
bool prismStaticDrawn = false;

// The LOGO table's code (0-11) for a pixel; see the LOGO comment
int logoCode(int x, int y) {
  char c = pgm_read_byte(&LOGO[y][x]);
  return (c <= '9') ? c - '0' : c - 'A' + 10;
}

/**
 * How much of the sheen lands on something `d` pixels from the middle of the
 * band (0 = none, 1 = full), and the soft pastel rainbow color it adds there.
 * Returns false if it's outside the band.
 **/
bool sheenAt(float d, float &amount, uint8_t &r, uint8_t &g, uint8_t &b) {
  if (d <= -SHEEN_WIDTH || d >= SHEEN_WIDTH) return false;
  float k = 1 - fabs(d) / SHEEN_WIDTH;
  amount = k * k * (3 - 2 * k);  // smooth falloff at the edges
  // Spread the rainbow across the band, mixed halfway to white so it looks
  // like light on foil rather than paint
  hueToRGB((uint8_t)(128 + d * 10), r, g, b);
  r = r / 2 + 128;
  g = g / 2 + 128;
  b = b / 2 + 128;
  return true;
}

// Mix from color a to color b by amount (0-1)
uint8_t mix8(uint8_t a, uint8_t b, float amount) {
  return a + (b - a) * amount;
}

/**
 * The team logo in its normal colors. Every few seconds a soft rainbow sheen
 * sweeps across it diagonally, like light catching a holographic sticker,
 * lighting up the face of the slice as it passes. The side letters stay
 * orange with the same sheen sweeping up them.
 **/
void prismLogo() {
  startSide("prismLogo");
  if (startScroll("prismLogo")) prismStaticDrawn = false;

  unsigned long t = timeInScrollSequence % SHEEN_PERIOD;
  if (t >= SHEEN_SWEEP) {
    // Between sweeps: just the plain logo, drawn once
    if (!prismStaticDrawn) {
      drawLogo(0);
      setAll(SIDE_ORANGE_R, SIDE_ORANGE_G, SIDE_ORANGE_B);
      prismStaticDrawn = true;
    }
    return;
  }
  prismStaticDrawn = false;

  // Where the middle of the band is, sweeping from off the left to off the right
  float progress = easeInOutCubic(t / (float)SHEEN_SWEEP);
  float band = lerp(-SHEEN_WIDTH - 8, matrix.width() + SHEEN_WIDTH + 8, progress);

  matrix.clear();
  for (int y = 0; y < LOGO_HEIGHT; y++) {
    for (int x = 0; x < LOGO_WIDTH; x++) {
      int code = logoCode(x, y);
      if (code == 0) continue;  // background stays black

      // Normal logo color (inside of the slice is black)
      uint8_t r = 0, g = 0, b = 0;
      float strength = 0.8;
      if (code == 1) { r = 255; g = 127; b = 0; }                 // orange
      else if (code == 2) { r = 255; g = 255; b = 255; }          // white
      else if (code >= 8) { r = 0; g = 220; b = 255; }            // cyan
      else strength = 0.35;                                       // slice face: faint

      // The band leans so it sweeps diagonally
      float amount;
      uint8_t sr, sg, sb;
      if (sheenAt(x - band + (y - 8) * 0.6, amount, sr, sg, sb)) {
        amount *= strength;
        r = mix8(r, sr, amount);
        g = mix8(g, sg, amount);
        b = mix8(b, sb, amount);
      }
      if (r || g || b) matrix.drawPixel(x, y, matrix.Color(r, g, b));
    }
  }
  showMatrix();

  // Side letters: the same sheen sweeping up from the E to the S
  float rowBand = lerp(-SHEEN_WIDTH, 43 + SHEEN_WIDTH, progress);
  for (int row = 0; row < 43; row++) {
    uint8_t r = SIDE_ORANGE_R, g = SIDE_ORANGE_G, b = SIDE_ORANGE_B;
    float amount;
    uint8_t sr, sg, sb;
    if (sheenAt(row - rowBand, amount, sr, sg, sb)) {
      amount *= 0.8;
      r = mix8(r, sr, amount);
      g = mix8(g, sg, amount);
      b = mix8(b, sb, amount);
    }
    setRow(row, r, g, b);
  }
  updateLeds();
}

// ---------------------------------------------------------------------------
// Program: rainbow wave
// ---------------------------------------------------------------------------

// A 1-bit drawing surface the size of the matrix. Text is drawn here first,
// then each lit pixel is copied to the matrix in its own color.
GFXcanvas1 textMask(GRID_NUM_COLS * GRID_TILES_X, GRID_NUM_ROWS * GRID_TILES_Y);

/**
 * Big SLICE / 8738 filled with a diagonal rainbow that sweeps across it.
 * The side letters have a rainbow flowing up them.
 **/
void rainbowWave() {
  startSide("rainbowWave");
  startScroll("rainbowWave");
  uint8_t base = time / 8;

  // Draw the word into the mask, centered like showText()
  const char *word = sliceOr8738();
  textMask.fillScreen(0);
  textMask.setTextSize(2);
  textMask.setTextWrap(false);
  textMask.setCursor((matrix.width() - (int)strlen(word) * CHAR_PIX_WIDTH * 2) / 2 + 1, 1);
  textMask.print(word);

  matrix.clear();
  for (int y = 0; y < matrix.height(); y++) {
    for (int x = 0; x < matrix.width(); x++) {
      if (textMask.getPixel(x, y)) matrix.drawPixel(x, y, hueColor(base - x * 4 - y * 6, 255));
    }
  }
  showMatrix();

  for (int row = 0; row < 43; row++) setRowHue(row, base - row * 6, 255);
  updateLeds();
}

// ---------------------------------------------------------------------------
// Program: plasma
// ---------------------------------------------------------------------------

/**
 * Swirling rainbow plasma filling the whole display, made by adding a few
 * moving waves together, with SLICE / 8738 cut out of it in black.
 * The side letters swirl in the same colors.
 **/
void plasma() {
  startSide("plasma");
  startScroll("plasma");
  uint8_t t1 = time / 10, t2 = time / 15, t3 = time / 23;

  for (int y = 0; y < matrix.height(); y++) {
    for (int x = 0; x < matrix.width(); x++) {
      uint16_t v = sin8(x * 8 + t1) + sin8(y * 20 - t2) + sin8((x + y) * 5 + t3);
      matrix.drawPixel(x, y, hueColor(v / 3 + t1, 255));
    }
  }
  // Draws the text in black on top and sends it to the matrix
  showText(sliceOr8738(), BLACK, false, false);

  for (int row = 0; row < 43; row++) {
    uint16_t v = sin8(row * 12 + t2) + sin8(row * 5 - t3);
    setRowHue(row, v / 2 + t1, 255);
  }
  updateLeds();
}

// ---------------------------------------------------------------------------
// Program: confetti
// ---------------------------------------------------------------------------

#define MATRIX_SPARKLES 110
#define SIDE_SPARKLES 60
// How long each sparkle lasts (ms): fades in quickly, then out
#define SPARKLE_LIFE 700
#define SPARKLE_RISE 120

// Each sparkle has a position, a hue and when it started. The start time is
// only kept to 16 bits (it wraps every 65s) to save RAM; that's fine because
// a sparkle only lives for SPARKLE_LIFE ms.
int8_t sparkX[MATRIX_SPARKLES], sparkY[MATRIX_SPARKLES];
uint8_t sparkHue[MATRIX_SPARKLES];
uint16_t sparkBorn[MATRIX_SPARKLES];
int sideSparkLed[SIDE_SPARKLES];
uint8_t sideSparkHue[SIDE_SPARKLES];
uint16_t sideSparkBorn[SIDE_SPARKLES];

// How old a sparkle born at `born` is, in ms
uint16_t sparkleAge(uint16_t born) {
  return (uint16_t)time - born;
}

// Brightness (0-255) of a sparkle that is `age` ms old
uint8_t sparkleLevel(uint16_t age) {
  if (age >= SPARKLE_LIFE) return 0;
  if (age < SPARKLE_RISE) return (unsigned long)age * 255 / SPARKLE_RISE;
  return (unsigned long)(SPARKLE_LIFE - age) * 255 / (SPARKLE_LIFE - SPARKLE_RISE);
}

// A random side LED, skipping the always-white internal illuminator (128-170)
int randomSideLed() {
  int i = random(NUM_LEDS - 43 - 1);
  return i < 128 ? i : i + 43;
}

/**
 * Rainbow confetti twinkling all over the display and the side letters,
 * with big white SLICE / 8738 in front.
 **/
void confetti() {
  startSide("confetti");
  if (startScroll("confetti")) {
    // Start every sparkle at a different point in its life so they don't
    // all appear at once
    for (int i = 0; i < MATRIX_SPARKLES; i++) {
      sparkX[i] = random(matrix.width());
      sparkY[i] = random(matrix.height());
      sparkHue[i] = random(256);
      sparkBorn[i] = (uint16_t)time - random(SPARKLE_LIFE);
    }
    for (int i = 0; i < SIDE_SPARKLES; i++) {
      sideSparkLed[i] = randomSideLed();
      sideSparkHue[i] = random(256);
      sideSparkBorn[i] = (uint16_t)time - random(SPARKLE_LIFE);
    }
  }

  matrix.clear();
  for (int i = 0; i < MATRIX_SPARKLES; i++) {
    if (sparkleAge(sparkBorn[i]) >= SPARKLE_LIFE) {
      // This one's finished: start a new one somewhere else
      sparkX[i] = random(matrix.width());
      sparkY[i] = random(matrix.height());
      sparkHue[i] = random(256);
      sparkBorn[i] = time;
    }
    // Each piece of confetti is 2 pixels wide
    matrix.drawFastHLine(sparkX[i], sparkY[i], 2, hueColor(sparkHue[i], sparkleLevel(sparkleAge(sparkBorn[i]))));
  }
  showText(sliceOr8738(), WHITE, false, false);

  strip.clear();
  for (int i = 0; i < SIDE_SPARKLES; i++) {
    if (sparkleAge(sideSparkBorn[i]) >= SPARKLE_LIFE) {
      sideSparkLed[i] = randomSideLed();
      sideSparkHue[i] = random(256);
      sideSparkBorn[i] = time;
    }
    uint8_t r, g, b;
    uint8_t level = sparkleLevel(sparkleAge(sideSparkBorn[i]));
    hueToRGB(sideSparkHue[i], r, g, b);
    strip.setPixelColor(sideSparkLed[i], scale8(r, level), scale8(g, level), scale8(b, level));
  }
  updateLeds();
}

// ---------------------------------------------------------------------------
// Program: rainbow beat
// ---------------------------------------------------------------------------

#define BEAT_BARS 16
// Bar heights and their peak dots, in quarter pixels (0-64) so they can fall smoothly
uint8_t beatBar[BEAT_BARS];
uint8_t beatPeak[BEAT_BARS];
// Height of the rainbow sound meter on the side letters (0-43 rows)
int beatRows = 0;

/**
 * The microphone volume from 0-255, using the two pots the same way the
 * sound meter program does: MIN_PIN sets the noise floor, MAX_PIN the gain.
 **/
int micLevel() {
  int minPot = analogRead(MIN_PIN) / 4;
  int maxPot = analogRead(MAX_PIN) / 4;
  long level = micVolume - minPot;
  if (level < 0) level = 0;
  int range = (255 - maxPot) - minPot;
  if (range < 1) range = 1;
  level = level * (255 / range);
  return level > 255 ? 255 : level;
}

/**
 * Sound-reactive rainbow equalizer: 16 bars that jump with the music and
 * fall back down, with white peak dots that fall more slowly. The side
 * letters are a rainbow sound meter.
 **/
void rainbowBeat() {
  startSide("rainbowBeat");
  if (startScroll("rainbowBeat")) {
    for (int i = 0; i < BEAT_BARS; i++) beatBar[i] = beatPeak[i] = 0;
    beatRows = 0;
  }
  int level = micLevel();

  matrix.clear();
  for (int i = 0; i < BEAT_BARS; i++) {
    // Give each bar its own share of the volume (50-100%), changing over
    // time, so they move like a spectrum analyzer
    long share = 128 + sin8(i * 37 + time / 4) / 2;
    // Full height at a medium-loud level (160), so the bars use the whole display
    long target = (long)level * share / 255 * 64 / 160;
    if (target > 64) target = 64;

    if (target > beatBar[i]) beatBar[i] = target;      // jump up
    else if (beatBar[i] >= 3) beatBar[i] -= 3;         // fall back
    else beatBar[i] = 0;
    if (beatBar[i] > beatPeak[i]) beatPeak[i] = beatBar[i];
    else if (beatPeak[i] > 0) beatPeak[i]--;           // peaks fall slower

    int h = beatBar[i] / 4;
    int x = i * 4;
    if (h > 0) matrix.fillRect(x, 16 - h, 3, h, hueColor(i * 16 + time / 20, 255));
    int peakY = 15 - beatPeak[i] / 4;
    if (beatPeak[i] > 0 && peakY >= 0) matrix.drawFastHLine(x, peakY, 3, WHITE);
  }
  showMatrix();

  int rows = (long)level * 43 / 160;
  if (rows > 43) rows = 43;
  if (rows > beatRows) beatRows = rows;
  else if (beatRows > 0) beatRows--;
  strip.clear();
  for (int row = 0; row < beatRows; row++) setRowHue(row, row * 6, 255);
  updateLeds();
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
