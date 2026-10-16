#pragma once
#include <stdint.h>

// Longest single ESP32 light sleep of the Room Server and Sensor, as the Repeater's
// sleep(30) and PowerSaving-v17. A LoRa packet or the user button wakes earlier.
#ifndef IDLE_SLEEP_MAX_MILLIS
  #define IDLE_SLEEP_MAX_MILLIS 30000
#endif

// Sleep at most IDLE_SLEEP_MAX_MILLIS, shortened to the next role deadline.
class IdleSleepBudget {
  uint32_t _now, _budget;
public:
  explicit IdleSleepBudget(uint32_t now, uint32_t max_ms = IDLE_SLEEP_MAX_MILLIS) : _now(now), _budget(max_ms) {}
  void until(uint32_t deadline) {
    if (!deadline) return;
    int32_t remaining = (int32_t)(deadline - _now);
    if (remaining <= 0) _budget = 0;
    else if ((uint32_t)remaining < _budget) _budget = remaining;
  }
  uint32_t millis() const { return _budget; }
};
