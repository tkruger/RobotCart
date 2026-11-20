// PC simulator for SliceCartWithTimeManager.
//
// Compiles the real sketch against the mock Arduino / NeoPixel / NeoMatrix
// headers in mock/ and the real Adafruit_GFX library, runs one LED program,
// and records what the LEDs show at a fixed frame rate.
//
// Usage: sim <program> <seconds> <fps> <out.bin>
//
// Output: for each frame, 300 x RGB888 (side strip) then 64 x 16 x RGB888
// (matrix, row by row). make_gifs.py turns this into a GIF.

// Pull in all the system headers *before* renaming the sketch's global `time`,
// which would otherwise clash with the C library's time() function.
#include "Arduino.h"
#include "Adafruit_GFX.h"
#include "Adafruit_NeoPixel.h"
#include "Adafruit_NeoMatrix.h"

unsigned long sim_micros = 0;
unsigned long sim_clock_us = 0;
SimSerial Serial;

#define time sketch_time
// On the Mega an `int` is 16 bits, on a PC it's 32. Make the sketch's ints
// 16-bit so overflow bugs show up here the same way they do on the cart.
#define int short
// Function prototypes for the sketch (the Arduino IDE generates these
// automatically; make_gifs.py writes them to build/prototypes.h)
#include "build/prototypes.h"
#include "../../SliceCartWithTimeManager/SliceCartWithTimeManager.ino"
#undef int
#undef time

// Fake inputs: a beat at 120 BPM for the microphone, fixed pot positions
int sim_analogRead(int pin) {
  if (pin == MIN_PIN) return 100;
  if (pin == MAX_PIN) return 400;
  if (pin == MIC_PIN) {
    double t = sim_micros / 1e6;
    int beat = (int)(t * 2);                 // which beat we're on
    double since = t * 2 - beat;             // 0..1 through the beat
    double peak = 60 + (beat * 137 % 5) * 45;  // vary how loud each beat is
    return (int)(peak * exp(-since * 4) + 20 + rand() % 20);
  }
  return 0;
}

int main(int argc, char **argv) {
  if (argc < 5) {
    fprintf(stderr, "usage: sim <program> <seconds> <fps> <out.bin>\n");
    return 1;
  }
  int program = atoi(argv[1]);
  double seconds = atof(argv[2]);
  double fps = atof(argv[3]);
  FILE *out = fopen(argv[4], "wb");
  if (!out) { perror(argv[4]); return 1; }

  srand(1);
  setup();
  sequence = program;

  unsigned long end = (unsigned long)(seconds * 1e6);
  unsigned long frameUs = (unsigned long)(1e6 / fps);
  unsigned long nextFrame = 0;
  int frames = 0;

  while (sim_micros < end) {
    loop();
    sim_advance_us(200);  // rough cost of the sketch's own code per loop

    // Record however many frames have elapsed during this loop
    while (nextFrame <= sim_micros && nextFrame < end) {
      for (int i = 0; i < NUM_LEDS; i++) {
        uint32_t c = strip.shown[i];
        uint8_t rgb[3] = {(uint8_t)(c >> 16), (uint8_t)(c >> 8), (uint8_t)c};
        fwrite(rgb, 1, 3, out);
      }
      for (int i = 0; i < matrix.W * matrix.H; i++) {
        uint16_t c = matrix.shown[i];
        // Expand RGB565 back to 8 bits per channel
        uint8_t rgb[3] = {(uint8_t)(((c >> 11) & 0x1F) * 255 / 31),
                          (uint8_t)(((c >> 5) & 0x3F) * 255 / 63),
                          (uint8_t)((c & 0x1F) * 255 / 31)};
        fwrite(rgb, 1, 3, out);
      }
      nextFrame += frameUs;
      frames++;
    }
  }
  fclose(out);
  printf("program %d: %d frames\n", program, frames);
  return 0;
}
