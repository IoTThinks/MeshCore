#include "RXPowerSaving.h"

static uint32_t ceilRxPowerSavingValue(float value) {
  uint32_t rounded = (uint32_t)value;
  return value > (float)rounded ? rounded + 1 : rounded;
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

bool calcRxPowerSavingLevel(uint8_t level, uint8_t sf, float bw, uint8_t preamble,
                            uint32_t* rx_us, uint32_t* sleep_us) {
  if (rx_us == nullptr || sleep_us == nullptr || level < 1 || level > 10 || sf < 5 || sf > 12 ||
      bw <= 0.0f || (preamble != 16 && preamble != 32)) {
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

  *rx_us = ceilRxPowerSavingValue(rx_symbols * symbol_us);
  *sleep_us = (uint32_t)(sleep_symbols * symbol_us);
  return true;
}

void ensureRxPowerSavingDefaults(uint32_t* rx_us, uint32_t* sleep_us) {
  if (!isValidRxPowerSavingPeriod(*rx_us)) *rx_us = RX_POWERSAVING_DEFAULT_RX_US;
  if (!isValidRxPowerSavingPeriod(*sleep_us)) *sleep_us = RX_POWERSAVING_DEFAULT_SLEEP_US;
}

bool recalcRxPowerSavingFromLevel(uint8_t level, uint8_t sf, float bw, uint8_t preamble,
                                  uint32_t* rx_us, uint32_t* sleep_us) {
  if (level < 1 || level > 10) return false;
  if (preamble == 0) preamble = rxPowerSavingPreambleForSF(sf);

  uint32_t calculated_rx_us = 0;
  uint32_t calculated_sleep_us = 0;
  if (!calcRxPowerSavingLevel(level, sf, bw, preamble, &calculated_rx_us, &calculated_sleep_us) ||
      !isValidRxPowerSavingPeriod(calculated_rx_us) ||
      !isValidRxPowerSavingPeriod(calculated_sleep_us)) {
    return false;
  }

  *rx_us = calculated_rx_us;
  *sleep_us = calculated_sleep_us;
  return true;
}

void normalizeRxPowerSavingConfig(RxPowerSavingConfig* config, uint8_t sf, float bw) {
  if (config == nullptr) return;

  config->enabled = config->enabled ? 1 : 0;
  if (config->level > 10) config->level = RX_POWERSAVING_BALANCED_LEVEL;
  if (config->preamble != 0 && config->preamble != 16 && config->preamble != 32) {
    config->preamble = RX_POWERSAVING_PROFILE_PREAMBLE;
  }
  ensureRxPowerSavingDefaults(&config->rx_us, &config->sleep_us);
  recalcRxPowerSavingFromLevel(config->level, sf, bw, config->preamble,
                               &config->rx_us, &config->sleep_us);
}
