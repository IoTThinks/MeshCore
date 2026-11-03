#pragma once

#include <math.h>
#include "RXPowerSaving.h"

// SX1262's post-preamble timer does not follow the datasheet's 2*R+S. Measured,
// undocumented behaviour:
//   (S + 2*(R & 0xFFFF) + ((R & 63) << 18)) & 0xFFFFFF
// Start at R%64=1, which waits about 4 s, and increase the remainder only when
// needed to cover a P32 sender's header.
inline bool adjustSX1262RxPowerSavingPeriod(uint32_t rx_us, uint32_t sleep_us,
                                          uint8_t sf, float bw, uint32_t transition_us,
                                          uint32_t* effective_rx_us) {
  constexpr uint32_t max_ticks = 0xFFFFFFUL;
  if (effective_rx_us == nullptr || sf < 5 || sf > 12 || !(bw > 0.0f) ||
      sleep_us <= transition_us) return false;

  const uint64_t rx_ticks = (uint64_t)rx_us * 8 / 125;
  const uint64_t sleep_ticks = (uint64_t)(sleep_us - transition_us) * 8 / 125;
  if (rx_ticks == 0 || rx_ticks > max_ticks || sleep_ticks == 0 ||
      sleep_ticks > max_ticks) return false;

  const double sync = sf <= 6 ? RX_POWERSAVING_SYNC_SYMBOLS_LOW_SF
                             : RX_POWERSAVING_SYNC_SYMBOLS;
  const double need = ceil((32.0 + sync + RX_POWERSAVING_HEADER_SYMBOLS +
                           RX_POWERSAVING_TIMER_SLACK_SYMBOLS) * 64.0 * (1UL << sf) / bw);
  if (!(need > 0.0 && need <= max_ticks)) return false;

  uint32_t candidate = (((uint32_t)rx_ticks + 62) & ~63UL) + 1;
  for (; candidate <= max_ticks && (candidate & 63) <= 62; candidate++) {
    const uint32_t reload = ((uint32_t)sleep_ticks + 2 * (candidate & 0xFFFF) +
                             ((candidate & 63) << 18)) & max_ticks;
    if (reload >= (uint32_t)need) {
      // Round up so RadioLib's truncating us-to-ticks conversion preserves R.
      *effective_rx_us = (uint32_t)(((uint64_t)candidate * 125 + 7) / 8);
      return true;
    }
  }
  return false;
}
