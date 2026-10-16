#pragma once
#include "Uptime.h"
class PeriodicReboot {
  Uptime _uptime;
public:
  bool due(uint8_t hours, uint32_t now, bool transmitting, bool ota_active) {
    _uptime.tick(now);
    return hours != 0 && _uptime.milliseconds() >= (uint64_t)hours * 3600000ULL
           && !transmitting && !ota_active;
  }
  static bool parseHours(const char* text, uint8_t& hours) {
    if (!text || !*text) return false;
    unsigned value = 0;
    for (; *text; ++text) {
      if (*text < '0' || *text > '9') return false;
      value = value * 10 + (*text - '0');
      if (value > 255) return false;
    }
    hours = value;
    return true;
  }
};
