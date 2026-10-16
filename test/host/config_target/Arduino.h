#pragma once
#include_next <Arduino.h>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#define constrain(x, lo, hi) std::min(std::max((x), (lo)), (hi))
inline char* ltoa(long value, char* out, int) { std::sprintf(out,"%ld",value);return out; }
