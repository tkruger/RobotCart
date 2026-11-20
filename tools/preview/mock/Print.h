// Minimal stand-in for Arduino's Print class.
#pragma once
#include "Arduino.h"

class Print {
public:
  virtual ~Print() {}
  virtual size_t write(uint8_t c) = 0;
  size_t write(const char *s) { size_t n = 0; while (*s) n += write((uint8_t)*s++); return n; }

  size_t print(const char *s) { return write(s); }
  size_t print(const String &s) { return write(s.c_str()); }
  size_t print(const __FlashStringHelper *s) { return write(reinterpret_cast<const char *>(s)); }
  size_t print(char c) { return write((uint8_t)c); }
  size_t print(int v) { char b[16]; snprintf(b, sizeof b, "%d", v); return write(b); }
  size_t println(int v) { return print(v) + write("\n"); }
  size_t println(const char *s) { return print(s) + write("\n"); }
  size_t println() { return write("\n"); }
};
