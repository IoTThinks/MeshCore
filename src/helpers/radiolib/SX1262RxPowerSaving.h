// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Jarosław Domański, https://github.com/Dom4n
//
// RXPS: SX1262 timer rule and false-preamble timeout.
// Written for Kevin Le's (IoTThinks) MeshCore PowerSaving firmware.
// Overview, usage and version history: src/helpers/radiolib/RXPowerSaving.h

#pragma once

#include <math.h>
#include "RXPowerSaving.h"

// Symbols from the start of a P32 sender's preamble to the end of its header,
// plus the timer slack.
inline double sx1262RxPowerSavingP32HeaderSymbols(uint8_t sf) {
  const double sync = sf <= 6 ? RX_POWERSAVING_SYNC_SYMBOLS_LOW_SF
                             : RX_POWERSAVING_SYNC_SYMBOLS;
  return 32.0 + sync + RX_POWERSAVING_HEADER_SYMBOLS + RX_POWERSAVING_TIMER_SLACK_SYMBOLS;
}

// SX1262's post-preamble timer does not follow the datasheet's 2*R+S. Measured,
// undocumented behaviour:
//   (S + 2*(R & 0xFFFF) + ((R & 63) << 18)) & 0xFFFFFF
// Start at the R%64=1 at or above R, or at or below it with round_down, which
// waits about 4 s, and increase the remainder only when needed to cover a P32
// sender's header.
inline bool adjustSX1262RxPowerSavingPeriod(uint32_t rx_us, uint32_t sleep_us,
                                          uint8_t sf, float bw, uint32_t transition_us,
                                          uint32_t* effective_rx_us, bool round_down = false) {
  constexpr uint32_t max_ticks = 0xFFFFFFUL;
  if (effective_rx_us == nullptr || sf < 5 || sf > 12 || !(bw > 0.0f) ||
      sleep_us <= transition_us) return false;

  const uint64_t rx_ticks = (uint64_t)rx_us * 8 / 125;
  const uint64_t sleep_ticks = (uint64_t)(sleep_us - transition_us) * 8 / 125;
  if (rx_ticks == 0 || rx_ticks > max_ticks || sleep_ticks == 0 ||
      sleep_ticks > max_ticks) return false;

  const double need = ceil(sx1262RxPowerSavingP32HeaderSymbols(sf) * 64.0 * (1UL << sf) / bw);
  if (!(need > 0.0 && need <= max_ticks)) return false;

  // At 64 ticks or less, rounding down would leave a one-tick window.
  uint32_t candidate = round_down && rx_ticks > 64
                           ? (((uint32_t)rx_ticks - 1) & ~63UL) + 1
                           : (((uint32_t)rx_ticks + 62) & ~63UL) + 1;
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

// A preamble that no packet follows otherwise keeps RX on for the whole
// post-preamble timer above. With a symbol timeout, RX ends together with such
// a preamble and the cycle goes on without an IRQ (measured on T096 and
// Tracker V2 at SF8/BW62.5). 96 is three P32 preambles.
static constexpr uint8_t SX1262_RX_POWERSAVING_SYMBOL_TIMEOUT = 96;

// Symbol timeout for one arming, or 0 to leave it off. If it counts from the
// start of each RX window, as in single RX, a P32 frame that starts at the end
// of the window must still lock before it expires. Every level window fits;
// long manual windows run without it, as before.
inline uint8_t sx1262RxPowerSavingSymbolTimeout(uint32_t rx_us, uint8_t sf, float bw) {
  const double window = rx_us * (double)bw / (1000.0 * (1UL << sf));
  return window + sx1262RxPowerSavingP32HeaderSymbols(sf) <= SX1262_RX_POWERSAVING_SYMBOL_TIMEOUT
             ? SX1262_RX_POWERSAVING_SYMBOL_TIMEOUT : 0;
}

// SetLoRaSymbNumTimeout argument and sync timeout register, encoded as
// LoRaMac-node does: count = mantissa * 2^(2 * exponent + 1), rounded up and
// capped at 248 symbols.
struct SX1262SymbolTimeout {
  uint8_t count;
  uint8_t reg;   // mantissa << 3 | exponent
};

inline SX1262SymbolTimeout sx1262SymbolTimeout(uint8_t symbols) {
  uint8_t mantissa = ((symbols > 248 ? 248 : symbols) + 1) >> 1;
  uint8_t exponent = 0;
  while (mantissa > 31) {
    mantissa = (mantissa + 3) >> 2;
    ++exponent;
  }
  return {(uint8_t)(mantissa << (2 * exponent + 1)), (uint8_t)((mantissa << 3) | exponent)};
}
