// Minimal stand-in for the Arduino core so the sketch can run on a PC.
// Only implements what SliceCartWithTimeManager and Adafruit_GFX use.
// Time is simulated: it only moves forward when the sketch calls delay()
// or shows LEDs (see sim_advance_us), so runs are fast and repeatable.
#pragma once

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdio.h>

#ifndef ARDUINO
#define ARDUINO 10819
#endif

#define PROGMEM
#define PSTR(s) (s)
#define pgm_read_byte(addr) (*(const unsigned char *)(addr))
#define pgm_read_word(addr) (*(const unsigned short *)(addr))
#define pgm_read_dword(addr) (*(const unsigned long *)(addr))
#define pgm_read_pointer(addr) (*(void *const *)(addr))

#ifndef PI
#define PI 3.14159265358979323846
#endif
#define radians(deg) ((deg) * PI / 180.0)
#define degrees(rad) ((rad) * 180.0 / PI)
#ifdef _MSC_VER
#define __builtin_bswap16 _byteswap_ushort
#endif

typedef bool boolean;
typedef uint8_t byte;

#define HIGH 1
#define LOW 0
#define INPUT 0
#define OUTPUT 1
#define LED_BUILTIN 13
#define A0 54
#define A1 55
#define A2 56

// ---- Simulation hooks (implemented in sim.cpp) ----
// sim_micros is real (wall clock) time. sim_clock_us is what millis() sees.
// They differ because, like a real AVR (Mega/Uno), interrupts are off while
// LEDs are being sent, so millis() only gains ~1ms per show() no matter how
// long the show() actually took.
extern unsigned long sim_micros;
extern unsigned long sim_clock_us;
inline void sim_advance_us(unsigned long us) { sim_micros += us; sim_clock_us += us; }
inline void sim_show_us(unsigned long us) { sim_micros += us; sim_clock_us += us < 1024 ? us : 1024; }
int sim_analogRead(int pin);

inline unsigned long millis() { return sim_clock_us / 1000; }
inline unsigned long micros() { return sim_clock_us; }
inline void delay(unsigned long ms) { sim_advance_us(ms * 1000); }
inline int analogRead(int pin) { return sim_analogRead(pin); }
inline int digitalRead(int) { return LOW; }  // buttons never pressed
inline void pinMode(int, int) {}
inline void digitalWrite(int, int) {}

// ---- Flash strings ----
class __FlashStringHelper;
#define F(s) (reinterpret_cast<const __FlashStringHelper *>(s))

// ---- String (just the parts the sketch uses) ----
class String {
public:
  String(const char *s = "") { set(s); }
  String(const __FlashStringHelper *s) { set(reinterpret_cast<const char *>(s)); }
  String(const String &o) { set(o.buf); }
  ~String() { free(buf); }
  String &operator=(const String &o) {
    if (this != &o) { free(buf); set(o.buf); }
    return *this;
  }
  String &operator=(const char *s) { free(buf); set(s); return *this; }
  bool equals(const char *s) const { return strcmp(buf, s) == 0; }
  bool equals(const String &s) const { return equals(s.buf); }
  unsigned int length() const { return (unsigned int)strlen(buf); }
  char charAt(unsigned int i) const { return i < length() ? buf[i] : 0; }
  const char *c_str() const { return buf; }

private:
  char *buf = nullptr;
  void set(const char *s) {
    size_t n = strlen(s);
    buf = (char *)malloc(n + 1);
    memcpy(buf, s, n + 1);
  }
};

#include "Print.h"

// Serial output is thrown away
class SimSerial : public Print {
public:
  void begin(long) {}
  size_t write(uint8_t) override { return 1; }
};
extern SimSerial Serial;
