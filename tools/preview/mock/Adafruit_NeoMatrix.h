// Simulated NeoMatrix: a plain 2D framebuffer in logical (x, y) coordinates.
// The tile/zigzag wiring flags are ignored since they only affect which
// physical LED a pixel goes to, not what the display looks like.
// Drawing (text, lines, fills) uses the real Adafruit_GFX library.
#pragma once
#include "Arduino.h"
#include "Adafruit_GFX.h"
#include "Adafruit_NeoPixel.h"

#define NEO_MATRIX_TOP 0
#define NEO_MATRIX_BOTTOM 0
#define NEO_MATRIX_LEFT 0
#define NEO_MATRIX_RIGHT 0
#define NEO_MATRIX_ROWS 0
#define NEO_MATRIX_COLUMNS 0
#define NEO_MATRIX_PROGRESSIVE 0
#define NEO_MATRIX_ZIGZAG 0
#define NEO_TILE_TOP 0
#define NEO_TILE_BOTTOM 0
#define NEO_TILE_LEFT 0
#define NEO_TILE_RIGHT 0
#define NEO_TILE_ROWS 0
#define NEO_TILE_COLUMNS 0
#define NEO_TILE_PROGRESSIVE 0
#define NEO_TILE_ZIGZAG 0

class Adafruit_NeoMatrix : public Adafruit_GFX {
public:
  Adafruit_NeoMatrix(int w, int h, uint8_t tX, uint8_t tY, uint8_t, uint8_t, int)
      : Adafruit_GFX(w * tX, h * tY), W(w * tX), H(h * tY) {
    pixels = (uint16_t *)calloc(W * H, sizeof(uint16_t));
    shown = (uint16_t *)calloc(W * H, sizeof(uint16_t));
  }
  Adafruit_NeoMatrix(int w, int h, uint8_t pin, uint8_t mt, int pt)
      : Adafruit_NeoMatrix(w, h, 1, 1, pin, mt, pt) {}

  void drawPixel(int16_t x, int16_t y, uint16_t c) override {
    if (x < 0 || y < 0 || x >= W || y >= H) return;
    pixels[y * W + x] = c;
  }
  void fillScreen(uint16_t c) override {
    for (int i = 0; i < W * H; i++) pixels[i] = c;
  }
  void begin() {}
  void show() {
    memcpy(shown, pixels, W * H * sizeof(uint16_t));
    sim_advance_us(30UL * W * H + 300);
  }
  void clear() { fillScreen(0); }
  void setBrightness(uint8_t) {}
  static uint16_t Color(uint8_t r, uint8_t g, uint8_t b) {
    return ((uint16_t)(r & 0xF8) << 8) | ((uint16_t)(g & 0xFC) << 3) | (b >> 3);
  }

  int W, H;
  uint16_t *pixels;
  uint16_t *shown;
};
