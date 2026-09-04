#include "RXPowerSaving.h"

static uint32_t ceilRxPowerSavingValue(float value) {
  uint32_t rounded = (uint32_t)value;
  return value > (float)rounded ? rounded + 1 : rounded;
}

// Both radios program their duty cycle in 15.625 us ticks and truncate the
// microsecond value on the way in, so a period of 10013 us is really 10000 us
// on air while the CLI happily reports 10013. Snapping to the nearest tick and
// returning the smallest microsecond value that still lands on it makes the
// reported periods the ones the hardware runs - which matters here, because the
// register value turned out to be what a duty-cycle defect keys on, not the
// microseconds we asked for.
static uint32_t rxPowerSavingTickToUs(uint64_t tick) {
  if (tick == 0) tick = 1;
  return (uint32_t)((tick * 125 + 7) / 8);            // ceil(tick * 15.625)
}

static uint32_t snapRxPowerSavingToTick(uint32_t us) {
  return rxPowerSavingTickToUs(((uint64_t)us * 8 + 62) / 125);   // nearest
}

// Rounding the listen window to the *nearest* tick can shave a few microseconds
// off, and the timer guard below has no room to give: it must hold with the
// register values the radio actually runs. So that one rounds up.
static uint32_t snapRxPowerSavingUpToTick(uint32_t us) {
  return rxPowerSavingTickToUs(((uint64_t)us * 8 + 124) / 125);  // ceil
}

bool isValidRxPowerSavingPeriod(uint32_t us) {
  return us >= RX_POWERSAVING_MIN_PERIOD_US && us <= RX_POWERSAVING_MAX_PERIOD_US;
}

uint8_t rxPowerSavingPreambleForSF(uint8_t sf) {
  return sf <= 8 ? 32 : 16;
}

bool isRxPowerSavingNumeric(const char* value) {
  if (value == nullptr || *value == 0) return false;
  while (*value) {
    if (*value < '0' || *value > '9') return false;
    value++;
  }
  return true;
}

float rxPowerSavingCaptureCost(const RxPowerSavingControl* control) {
  return control != nullptr ? control->rxPowerSavingCaptureCostSymbols()
                            : RX_POWERSAVING_CAPTURE_COST_SYMBOLS;
}

uint32_t rxPowerSavingTransition(const RxPowerSavingControl* control) {
  return control != nullptr ? control->rxPowerSavingTransitionUs()
                            : RX_POWERSAVING_TRANSITION_US;
}

// rxPeriod register values measured to break SetRxDutyCycle on SX1262: the
// radio latches the preamble and then never validates a header, losing 35% of
// packets at SF6 and all of them at SF7. Both were reproduced on two boards
// with an LR1110 witnessing 100% of the same transmissions, and both bands are
// exactly one tick wide - 639 and 641 are lossless.
//
// This list is certainly incomplete: about thirty values were sampled out of
// the thousands a profile can generate. Levels 1-10 do not need it because the
// timer guard makes any value safe; it exists only for the unguarded maximum,
// where it is the one protection left.
static const uint32_t RX_POWERSAVING_BAD_RX_TICKS[] = {320, 640};

static uint32_t avoidBadRxPowerSavingTick(uint32_t rx_us) {
  const uint32_t tick = (rx_us * 8) / 125;
  for (uint32_t bad : RX_POWERSAVING_BAD_RX_TICKS) {
    // One tick longer costs 15.625 us of listening and steps clear of the band.
    if (tick == bad) return (uint32_t)((((uint64_t)tick + 1) * 125 + 7) / 8);
  }
  return rx_us;
}

bool calcRxPowerSavingLevel(uint8_t level, uint8_t sf, float bw, uint8_t preamble,
                            uint32_t* rx_us, uint32_t* sleep_us,
                            float capture_cost_symbols, uint32_t transition_us) {
  if (rx_us == nullptr || sleep_us == nullptr || level < 1 ||
      level > RX_POWERSAVING_RISKY_WORKING_MAX_LEVEL || sf < 5 || sf > 12 ||
      bw <= 0.0f || (preamble != 16 && preamble != 32)) {
    return false;
  }
  const bool unguarded = isRxPowerSavingUnguardedLevel(level);

  const float symbol_us = (1000.0f * (float)(1UL << sf)) / bw;
  // Worst case a preamble starts the instant an RX window closes, so the part
  // that survives into the next window is `preamble - sleep`. The absolute
  // limit is therefore reached when that equals the chip's capture cost; the
  // unguarded levels are pinned to geometries measured at or past that point,
  // which is why they still need it.
  const float sleep_edge_symbols = (float)preamble - capture_cost_symbols;

  float sleep_symbols;
  float rx_symbols = 0.0f;      // only used by the unguarded levels
  if (unguarded) {
    // Kept on the old interpolation because that is the geometry the bench
    // measured: overdrive is the top of the profile, and `riskyWorkingMax` is
    // the point past it where delivery started to fall.
    float amount = 1.0f;
    if (isRxPowerSavingRiskyWorkingMaxLevel(level)) {
      const float virtual_level = preamble == 16 ? RX_POWERSAVING_RISKY_WORKING_MAX_VIRTUAL_P16
                                                 : RX_POWERSAVING_RISKY_WORKING_MAX_VIRTUAL_P32;
      amount = (virtual_level - 1.0f) / 9.0f;
    }
    const float rx_start_symbols = preamble == 16 ? 12.0f : 16.0f;
    const float sleep_start_symbols = preamble == 16 ? 2.0f : 15.0f;
    rx_symbols = rx_start_symbols + amount * (8.0f - rx_start_symbols);
    sleep_symbols = sleep_start_symbols + amount * (sleep_edge_symbols - sleep_start_symbols);
  } else {
    // Guarded levels state their catch directly, so the sleep is just what is
    // left of the preamble - no capture cost involved. The pad makes the node
    // catch a fifth of a symbol more than promised, which is what keeps the
    // generated register pairs off the defective ones.
    const float catch_symbols = rxPowerSavingLevelCatch(level, preamble);
    // A level that undertakes to catch less than this radio needs to latch is
    // not usable: say so rather than return a geometry that cannot work.
    if (catch_symbols < capture_cost_symbols) return false;
    sleep_symbols = (float)preamble - catch_symbols - RX_POWERSAVING_MARGIN_PAD_SYMBOLS;
    if (sleep_symbols < 0.0f) sleep_symbols = 0.0f;
  }

  *sleep_us = (uint32_t)(sleep_symbols * symbol_us);

  // Below the hardware floor the duty cycle cannot be armed at all: the driver
  // returns -708 and the wrapper quietly runs continuous RX, so the node keeps
  // reporting power saving while spending full RX current. Just above that the
  // SX1262 arms with ERR_NONE and then detects no preambles at all, which is
  // worse. Measured floor: 6050-6100 us dead, 6200 us and up lossless.
  const uint32_t min_sleep_us = transition_us + RX_POWERSAVING_MIN_SLEEP_MARGIN_US;
  if (*sleep_us < min_sleep_us) {
    if ((float)min_sleep_us > sleep_edge_symbols * symbol_us) {
      // Even zero margin would not fit above the floor: no level of this
      // profile can duty cycle at this SF/BW. Say so instead of returning
      // something that will silently fall back.
      return false;
    }
    // The floor eats into the margin. Levels below it collapse onto one point;
    // the reply still reports the periods, so the loss of margin is visible.
    *sleep_us = min_sleep_us;
  }
  *sleep_us = transition_us + snapRxPowerSavingToTick(*sleep_us - transition_us);

  // SetRxDutyCycle timer guard, straight out of the SX1261/2 datasheet: on
  // preamble detection the radio restarts its timer with 2*rxPeriod +
  // sleepPeriod (register values), and the packet dies if the header does not
  // arrive inside it. The datasheet states the requirement as
  //     Tpreamble + Theader <= 2 * rxPeriod + sleepPeriod
  // Measured: when it holds, no period value causes trouble. When it is broken
  // the SX1262 usually gets away with it - but not always, and the exceptions
  // are a single register tick wide. rxPeriod 640 (exactly 10.000 ms) loses
  // 60-65% of packets at SF6 and everything at SF7, on two different boards,
  // while 639 and 641 are lossless; rxPeriod 320 behaves the same way. Nothing
  // in the register value predicts which ticks misbehave, so satisfy the
  // condition instead of dodging the values. The listen window is therefore not
  // a free parameter on the guarded scale - it is whatever the condition needs.
  const float sync_symbols =
      sf <= 6 ? RX_POWERSAVING_SYNC_SYMBOLS_LOW_SF : RX_POWERSAVING_SYNC_SYMBOLS;
  const float need_us =
      ((float)preamble + sync_symbols + RX_POWERSAVING_HEADER_SYMBOLS +
       RX_POWERSAVING_TIMER_SLACK_SYMBOLS) * symbol_us;
  // Work in the radio's own units: the driver truncates (sleep - transition) to
  // 15.625 us ticks, so the microsecond difference overstates the programmed
  // sleep by up to one tick - enough to leave the condition short on every
  // level of a profile.
  const uint32_t sleep_ticks = ((*sleep_us - transition_us) * 8) / 125;
  const float programmed_sleep_us = (float)sleep_ticks * RX_POWERSAVING_TICK_US;

  if (unguarded) {
    *rx_us = ceilRxPowerSavingValue(rx_symbols * symbol_us);
  } else {
    const float min_rx_us = (need_us - programmed_sleep_us) / 2.0f;
    *rx_us = ceilRxPowerSavingValue(min_rx_us > 0.0f ? min_rx_us : symbol_us);
  }

  *rx_us = snapRxPowerSavingUpToTick(*rx_us);
  if (unguarded) *rx_us = avoidBadRxPowerSavingTick(*rx_us);
  return true;
}

void ensureRxPowerSavingDefaults(uint32_t* rx_us, uint32_t* sleep_us) {
  if (!isValidRxPowerSavingPeriod(*rx_us)) *rx_us = RX_POWERSAVING_DEFAULT_RX_US;
  if (!isValidRxPowerSavingPeriod(*sleep_us)) *sleep_us = RX_POWERSAVING_DEFAULT_SLEEP_US;
}

bool recalcRxPowerSavingFromLevel(uint8_t level, uint8_t sf, float bw, uint8_t preamble,
                                  uint32_t* rx_us, uint32_t* sleep_us,
                                  float capture_cost_symbols, uint32_t transition_us) {
  if (level < 1 || level > RX_POWERSAVING_RISKY_WORKING_MAX_LEVEL) return false;
  if (preamble == 0) preamble = rxPowerSavingPreambleForSF(sf);

  uint32_t calculated_rx_us = 0;
  uint32_t calculated_sleep_us = 0;
  if (!calcRxPowerSavingLevel(level, sf, bw, preamble, &calculated_rx_us, &calculated_sleep_us,
                              capture_cost_symbols, transition_us) ||
      !isValidRxPowerSavingPeriod(calculated_rx_us) ||
      !isValidRxPowerSavingPeriod(calculated_sleep_us)) {
    return false;
  }

  *rx_us = calculated_rx_us;
  *sleep_us = calculated_sleep_us;
  return true;
}

void normalizeRxPowerSavingConfig(RxPowerSavingConfig* config, uint8_t sf, float bw,
                                  float capture_cost_symbols, uint32_t transition_us) {
  if (config == nullptr) return;

  config->enabled = config->enabled ? 1 : 0;
  if (config->level > RX_POWERSAVING_RISKY_WORKING_MAX_LEVEL) {
    config->level = RX_POWERSAVING_BALANCED_LEVEL;
  }
  if (config->preamble != 0 && config->preamble != 16 && config->preamble != 32) {
    config->preamble = RX_POWERSAVING_PROFILE_PREAMBLE;
  }
  ensureRxPowerSavingDefaults(&config->rx_us, &config->sleep_us);
  recalcRxPowerSavingFromLevel(config->level, sf, bw, config->preamble,
                               &config->rx_us, &config->sleep_us, capture_cost_symbols,
                               transition_us);
}
