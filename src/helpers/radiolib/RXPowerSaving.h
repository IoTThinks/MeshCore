#pragma once
#include <Arduino.h>

#define RX_POWERSAVING_DEFAULT_RX_US      65625UL
#define RX_POWERSAVING_DEFAULT_SLEEP_US   60000UL
#define RX_POWERSAVING_MIN_PERIOD_US      1000UL
#define RX_POWERSAVING_MAX_PERIOD_US      30000000UL

// The named profiles are level presets pinned to a 16-symbol preamble: most
// deployed senders still transmit 16-symbol preambles regardless of the newer
// SF-based rule (32 for SF <= 8). Revisit once the field has largely migrated.
#define RX_POWERSAVING_CONSERVATIVE_LEVEL 1
#define RX_POWERSAVING_BALANCED_LEVEL     5
#define RX_POWERSAVING_PROFILE_PREAMBLE   16

// Fixed settings for companions
#define RXPS_FIXED_ENABLED                1
#define RXPS_FIXED_LEVEL                  RX_POWERSAVING_BALANCED_LEVEL
#define RXPS_FIXED_PREAMBLE               RX_POWERSAVING_PROFILE_PREAMBLE

inline bool isValidRxPowerSavingPeriod(uint32_t us) {
  return us >= RX_POWERSAVING_MIN_PERIOD_US && us <= RX_POWERSAVING_MAX_PERIOD_US;
}

// MeshCore preamble convention used for the RX powersaving timing calculation.
// Must stay in sync with RadioLibWrapper::preambleLengthForSF() (the value the
// radio actually transmits); kept local here because CommonCLI is radio-agnostic.
inline uint8_t rxPowerSavingPreambleForSF(uint8_t sf) {
  return sf <= 8 ? 32 : 16;
}

inline bool isNumeric(const char *sp) {
  if (!sp || !*sp) return false;
  while (*sp) {
    if (*sp < '0' || *sp > '9') return false;
    sp++;
  }
  return true;
}

inline uint32_t ceilPositiveFloat(float value) {
  uint32_t rounded = (uint32_t)value;
  return value > (float)rounded ? rounded + 1 : rounded;
}

inline bool calcRxPowerSavingLevel(uint8_t level, uint8_t sf, float bw, uint8_t preamble, uint32_t *rx_us,
                                   uint32_t *sleep_us) {
  if (level < 1 || level > 10 || sf < 5 || sf > 12 || bw <= 0.0f || (preamble != 16 && preamble != 32)) {
    return false;
  }

  const float symbol_us = (1000.0f * (float)(1UL << sf)) / bw;
  const float amount = (float)(level - 1) / 9.0f;
  const float rx_start_symbols = preamble == 16 ? 12.0f : 16.0f;
  const float sleep_start_symbols = preamble == 16 ? 2.0f : 15.0f;
  const float rx_edge_symbols = 8.0f;
  const float sleep_edge_symbols = (float)preamble + 4.25f - 8.0f;

  const float rx_symbols = rx_start_symbols + amount * (rx_edge_symbols - rx_start_symbols);
  const float sleep_symbols = sleep_start_symbols + amount * (sleep_edge_symbols - sleep_start_symbols);

  *rx_us = ceilPositiveFloat(rx_symbols * symbol_us);
  *sleep_us = (uint32_t)(sleep_symbols * symbol_us);
  return true;
}

inline void ensureRxPowerSavingDefaults(uint32_t *rx_ps_rx_us, uint32_t *rx_ps_sleep_us) {
  if (!isValidRxPowerSavingPeriod(*rx_ps_rx_us)) {
    *rx_ps_rx_us = RX_POWERSAVING_DEFAULT_RX_US;
  }

  if (!isValidRxPowerSavingPeriod(*rx_ps_sleep_us)) {
    *rx_ps_sleep_us = RX_POWERSAVING_DEFAULT_SLEEP_US;
  }
}

// Recomputes rx_ps_rx_us/rx_ps_sleep_us from the stored level and the current
// radio SF/BW. No-op (returns false) for manual timings (rx_ps_level == 0).
// Lets level-based RX powersaving auto-retune when SF/BW change.
inline bool recalcRxPowerSavingFromLevel(uint8_t level, uint8_t sf, float bw, uint8_t preamble,
                                         uint32_t *rx_ps_rx_us, uint32_t *rx_ps_sleep_us) {
  if (level < 1 || level > 10) {
    return false; // manual: nothing to recompute
  }

  if (preamble == 0) {
    preamble = rxPowerSavingPreambleForSF(sf);
  }

  uint32_t rx_us, sleep_us;
  if (!calcRxPowerSavingLevel(level, sf, bw, preamble, &rx_us, &sleep_us)) {
    return false;
  }

  if (!isValidRxPowerSavingPeriod(rx_us) || !isValidRxPowerSavingPeriod(sleep_us)) {
    return false;
  }

  *rx_ps_rx_us = rx_us;
  *rx_ps_sleep_us = sleep_us;

  return true;
}