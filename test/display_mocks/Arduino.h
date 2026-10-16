#pragma once
#include_next <Arduino.h>
#define HIGH 1
#define LOW 0
#define OUTPUT 1
inline int pin_levels[64] = {};
inline void pinMode(int, int) {}
inline void digitalWrite(int pin, int level) { pin_levels[pin] = level; }
