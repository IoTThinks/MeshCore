#pragma once
#include <stdint.h>
// Poll at least once per 32-bit millis() wrap, including when the screen is off.
class Uptime {
  uint32_t _last = 0;
  uint64_t _total = 0;
public:
  void tick(uint32_t now) { _total += (uint32_t)(now - _last); _last = now; }
  uint64_t milliseconds() const { return _total; }
};
