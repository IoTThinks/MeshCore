#pragma once
#include <Arduino.h>
struct TwoWire { inline static int error=0; void beginTransmission(uint8_t) {} uint8_t endTransmission() { return error; } };
inline TwoWire Wire;
