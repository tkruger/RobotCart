// Simulated NeoPixel strip: keeps a pixel buffer, and show() copies it to the
// "displayed" buffer that the preview recorder reads.
#pragma once
#include "Arduino.h"

#define NEO_GRB 0
#define NEO_KHZ800 0

class Adafruit_NeoPixel {
public:
  Adafruit_NeoPixel(uint16_t n, int16_t, int) : n(n) {
    pixels = (uint32_t *)calloc(n, sizeof(uint32_t));
    shown = (uint32_t *)calloc(n, sizeof(uint32_t));
  }
  void begin() {}
  void show() {
    memcpy(shown, pixels, n * sizeof(uint32_t));
    sim_advance_us(30UL * n + 300);  // WS2812: ~30us per pixel + latch
  }
  void clear() { memset(pixels, 0, n * sizeof(uint32_t)); }
  void setBrightness(uint8_t) {}  // previews are shown at full brightness
  void setPixelColor(uint16_t i, uint8_t r, uint8_t g, uint8_t b) {
    if (i < n) pixels[i] = ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
  }
  void setPixelColor(uint16_t i, uint32_t c) { if (i < n) pixels[i] = c; }
  uint16_t numPixels() const { return n; }

  uint16_t n;
  uint32_t *pixels;
  uint32_t *shown;
};
