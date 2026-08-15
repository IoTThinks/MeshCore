#pragma once

#include <stdint.h>

static constexpr uint32_t RX_POWERSAVING_DEFAULT_RX_US = 65625UL;
static constexpr uint32_t RX_POWERSAVING_DEFAULT_SLEEP_US = 60000UL;
static constexpr uint32_t RX_POWERSAVING_MIN_PERIOD_US = 1000UL;
static constexpr uint32_t RX_POWERSAVING_MAX_PERIOD_US = 30000000UL;

static constexpr uint8_t RX_POWERSAVING_CONSERVATIVE_LEVEL = 1;
static constexpr uint8_t RX_POWERSAVING_BALANCED_LEVEL = 5;
static constexpr uint8_t RX_POWERSAVING_PROFILE_PREAMBLE = 16;
static constexpr uint8_t RX_POWERSAVING_MAX_CONSEC_ARM_FAILURES = 3;

class RxPowerSavingArmRetryState {
  uint8_t _consecutive_failures = 0;

public:
  bool canAttempt() const {
    return _consecutive_failures < RX_POWERSAVING_MAX_CONSEC_ARM_FAILURES;
  }
  void recordSuccess() { _consecutive_failures = 0; }
  void recordFailure() {
    if (_consecutive_failures < RX_POWERSAVING_MAX_CONSEC_ARM_FAILURES) {
      _consecutive_failures++;
    }
  }
  void reset() { _consecutive_failures = 0; }
  uint8_t consecutiveFailures() const { return _consecutive_failures; }
};

// Runs the part of a hardware arm sequence that starts by changing persistent
// radio configuration. Every failing step rolls that configuration back, while
// the original arm error remains the result returned to the caller.
template <typename Configure, typename Stage, typename Arm, typename Rollback>
int16_t runRxPowerSavingArmTransaction(Configure configure, Stage stage,
                                       Arm arm, Rollback rollback) {
  int16_t state = configure();
  if (state != 0) {
    rollback();
    return state;
  }

  state = stage();
  if (state != 0) {
    rollback();
    return state;
  }

  state = arm();
  if (state != 0) rollback();
  return state;
}

struct RxPowerSavingConfig {
  uint8_t enabled = 0;
  uint32_t rx_us = RX_POWERSAVING_DEFAULT_RX_US;
  uint32_t sleep_us = RX_POWERSAVING_DEFAULT_SLEEP_US;
  uint8_t level = RX_POWERSAVING_BALANCED_LEVEL;
  uint8_t preamble = RX_POWERSAVING_PROFILE_PREAMBLE;
};

struct RxPowerSavingStatus {
  bool supported = false;
  bool armed = false;
  int16_t last_error = 0;
  uint32_t arm_failures = 0;
  // Periods the radio actually runs with. A driver may clamp the requested
  // values to satisfy a hardware constraint, so these can differ from the
  // configured rx_us/sleep_us. Zero means "nothing armed yet".
  uint32_t effective_rx_us = 0;
  uint32_t effective_sleep_us = 0;
};

class RxPowerSavingControl {
public:
  virtual ~RxPowerSavingControl() = default;

  virtual bool setRxPowerSaving(bool enabled, uint32_t rx_us, uint32_t sleep_us) {
    (void)rx_us;
    (void)sleep_us;
    return !enabled;
  }
  virtual RxPowerSavingStatus getRxPowerSavingStatus() const { return {}; }
  virtual bool supportsRxPowerSavingRfRxDisable() const { return false; }
  virtual bool setRxPowerSavingRfRxDisabled(bool disabled) {
    (void)disabled;
    return false;
  }
  virtual bool isRxPowerSavingRfRxDisabled() const { return false; }
  virtual bool isRxPowerSavingCalibrationActive() const { return false; }
};

bool isValidRxPowerSavingPeriod(uint32_t us);
uint8_t rxPowerSavingPreambleForSF(uint8_t sf);
bool isRxPowerSavingNumeric(const char* value);
bool calcRxPowerSavingLevel(uint8_t level, uint8_t sf, float bw, uint8_t preamble,
                            uint32_t* rx_us, uint32_t* sleep_us);
void ensureRxPowerSavingDefaults(uint32_t* rx_us, uint32_t* sleep_us);
bool recalcRxPowerSavingFromLevel(uint8_t level, uint8_t sf, float bw, uint8_t preamble,
                                  uint32_t* rx_us, uint32_t* sleep_us);
void normalizeRxPowerSavingConfig(RxPowerSavingConfig* config, uint8_t sf, float bw);
