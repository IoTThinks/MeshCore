#include <gtest/gtest.h>

#include <cstring>

#include "helpers/radiolib/RXPowerSaving.h"
#include "helpers/radiolib/RXPowerSavingCLI.h"

class FakeRxPowerSavingControl : public RxPowerSavingControl {
public:
  bool accept = true;
  bool set_called = false;
  bool requested_enabled = false;
  uint32_t requested_rx_us = 0;
  uint32_t requested_sleep_us = 0;
  bool rf_rx_supported = false;
  bool rf_rx_disabled = false;
  RxPowerSavingStatus status;

  bool setRxPowerSaving(bool enabled, uint32_t rx_us, uint32_t sleep_us) override {
    set_called = true;
    requested_enabled = enabled;
    requested_rx_us = rx_us;
    requested_sleep_us = sleep_us;
    return accept;
  }

  RxPowerSavingStatus getRxPowerSavingStatus() const override { return status; }
  bool supportsRxPowerSavingRfRxDisable() const override { return rf_rx_supported; }
  bool setRxPowerSavingRfRxDisabled(bool disabled) override {
    if (!rf_rx_supported) return false;
    rf_rx_disabled = disabled;
    return true;
  }
  bool isRxPowerSavingRfRxDisabled() const override { return rf_rx_disabled; }
};

TEST(RxPowerSaving, DefaultsKeepRepeaterDisabledWithBalancedIntent) {
  const RxPowerSavingConfig config;

  EXPECT_EQ(config.enabled, 0);
  EXPECT_EQ(config.level, RX_POWERSAVING_BALANCED_LEVEL);
  EXPECT_EQ(config.preamble, RX_POWERSAVING_PROFILE_PREAMBLE);
  EXPECT_EQ(config.rx_us, RX_POWERSAVING_DEFAULT_RX_US);
  EXPECT_EQ(config.sleep_us, RX_POWERSAVING_DEFAULT_SLEEP_US);
}

TEST(RxPowerSaving, BaseControlRejectsEnableAndAcceptsContinuousRx) {
  RxPowerSavingControl control;

  EXPECT_FALSE(control.setRxPowerSaving(true, 12345, 23456));
  EXPECT_TRUE(control.setRxPowerSaving(false, 12345, 23456));

  const RxPowerSavingStatus status = control.getRxPowerSavingStatus();
  EXPECT_FALSE(status.supported);
  EXPECT_FALSE(status.armed);
  EXPECT_EQ(status.arm_failures, 0U);
  EXPECT_EQ(status.effective_rx_us, 0U);
  EXPECT_EQ(status.effective_sleep_us, 0U);
}

TEST(RxPowerSaving, ArmRetryStopsAfterThreeFailuresAndSuccessResetsIt) {
  RxPowerSavingArmRetryState retry;

  EXPECT_TRUE(retry.canAttempt());
  for (uint8_t expected = 1; expected <= RX_POWERSAVING_MAX_CONSEC_ARM_FAILURES; expected++) {
    retry.recordFailure();
    EXPECT_EQ(retry.consecutiveFailures(), expected);
  }
  EXPECT_FALSE(retry.canAttempt());

  // Saturate instead of wrapping if a caller records another failure.
  retry.recordFailure();
  EXPECT_EQ(retry.consecutiveFailures(), RX_POWERSAVING_MAX_CONSEC_ARM_FAILURES);

  retry.recordSuccess();
  EXPECT_EQ(retry.consecutiveFailures(), 0);
  EXPECT_TRUE(retry.canAttempt());
}

TEST(RxPowerSaving, ClearingRetryStateGrantsThreeFreshAttempts) {
  RxPowerSavingArmRetryState retry;
  for (uint8_t i = 0; i < RX_POWERSAVING_MAX_CONSEC_ARM_FAILURES; i++) {
    retry.recordFailure();
  }
  ASSERT_FALSE(retry.canAttempt());

  retry.reset();
  EXPECT_TRUE(retry.canAttempt());
  EXPECT_EQ(retry.consecutiveFailures(), 0);
}

TEST(RxPowerSaving, RejectsInvalidProfileInputs) {
  uint32_t rx_us = 0;
  uint32_t sleep_us = 0;

  EXPECT_FALSE(calcRxPowerSavingLevel(0, 10, 250.0f, 16, &rx_us, &sleep_us));
  EXPECT_FALSE(calcRxPowerSavingLevel(11, 10, 250.0f, 16, &rx_us, &sleep_us));
  EXPECT_FALSE(calcRxPowerSavingLevel(5, 4, 250.0f, 16, &rx_us, &sleep_us));
  EXPECT_FALSE(calcRxPowerSavingLevel(5, 10, 0.0f, 16, &rx_us, &sleep_us));
  EXPECT_FALSE(calcRxPowerSavingLevel(5, 10, 250.0f, 24, &rx_us, &sleep_us));
}

TEST(RxPowerSaving, AcceptsPeriodBoundariesOnly) {
  EXPECT_FALSE(isValidRxPowerSavingPeriod(RX_POWERSAVING_MIN_PERIOD_US - 1));
  EXPECT_TRUE(isValidRxPowerSavingPeriod(RX_POWERSAVING_MIN_PERIOD_US));
  EXPECT_TRUE(isValidRxPowerSavingPeriod(RX_POWERSAVING_MAX_PERIOD_US));
  EXPECT_FALSE(isValidRxPowerSavingPeriod(RX_POWERSAVING_MAX_PERIOD_US + 1));
}

TEST(RxPowerSaving, RepairsInvalidPersistedPeriodsIndependently) {
  uint32_t rx_us = 0;
  uint32_t sleep_us = 12345;

  ensureRxPowerSavingDefaults(&rx_us, &sleep_us);
  EXPECT_EQ(rx_us, RX_POWERSAVING_DEFAULT_RX_US);
  EXPECT_EQ(sleep_us, 12345U);

  rx_us = 23456;
  sleep_us = RX_POWERSAVING_MAX_PERIOD_US + 1;
  ensureRxPowerSavingDefaults(&rx_us, &sleep_us);
  EXPECT_EQ(rx_us, 23456U);
  EXPECT_EQ(sleep_us, RX_POWERSAVING_DEFAULT_SLEEP_US);
}

TEST(RxPowerSaving, NormalizesPersistedConfigBeforeApplying) {
  RxPowerSavingConfig config;
  config.enabled = 7;
  config.level = 255;
  config.preamble = 24;
  config.rx_us = 0;
  config.sleep_us = 0;

  normalizeRxPowerSavingConfig(&config, 10, 250.0f);

  EXPECT_EQ(config.enabled, 1);
  EXPECT_EQ(config.level, RX_POWERSAVING_BALANCED_LEVEL);
  EXPECT_EQ(config.preamble, RX_POWERSAVING_PROFILE_PREAMBLE);
  EXPECT_EQ(config.rx_us, 41871U);
  EXPECT_EQ(config.sleep_us, 26851U);
}

TEST(RxPowerSaving, NumericInputIsStrictDecimal) {
  EXPECT_TRUE(isRxPowerSavingNumeric("0"));
  EXPECT_TRUE(isRxPowerSavingNumeric("123456"));
  EXPECT_FALSE(isRxPowerSavingNumeric(nullptr));
  EXPECT_FALSE(isRxPowerSavingNumeric(""));
  EXPECT_FALSE(isRxPowerSavingNumeric("-1"));
  EXPECT_FALSE(isRxPowerSavingNumeric("1.5"));
  EXPECT_FALSE(isRxPowerSavingNumeric("12x"));
  EXPECT_FALSE(isRxPowerSavingNumeric(" 12"));
}

TEST(RxPowerSaving, CompanionProfileIsLevelFivePreambleSixteen) {
  uint32_t rx_us = 0;
  uint32_t sleep_us = 0;

  ASSERT_TRUE(calcRxPowerSavingLevel(5, 10, 250.0f, 16, &rx_us, &sleep_us));
  EXPECT_EQ(rx_us, 41871U);
  EXPECT_EQ(sleep_us, 26851U);
}

TEST(RxPowerSaving, NamedProfileConstantsRemainStable) {
  EXPECT_EQ(RX_POWERSAVING_CONSERVATIVE_LEVEL, 1);
  EXPECT_EQ(RX_POWERSAVING_BALANCED_LEVEL, 5);
  EXPECT_EQ(RX_POWERSAVING_PROFILE_PREAMBLE, 16);
}

TEST(RxPowerSaving, HigherLevelTradesListenTimeForSleepTime) {
  uint32_t conservative_rx_us = 0;
  uint32_t conservative_sleep_us = 0;
  uint32_t aggressive_rx_us = 0;
  uint32_t aggressive_sleep_us = 0;

  ASSERT_TRUE(calcRxPowerSavingLevel(1, 10, 250.0f, 16,
                                     &conservative_rx_us, &conservative_sleep_us));
  ASSERT_TRUE(calcRxPowerSavingLevel(10, 10, 250.0f, 16,
                                     &aggressive_rx_us, &aggressive_sleep_us));
  EXPECT_GT(conservative_rx_us, aggressive_rx_us);
  EXPECT_LT(conservative_sleep_us, aggressive_sleep_us);
}

TEST(RxPowerSaving, AutoPreambleTracksSpreadingFactor) {
  EXPECT_EQ(rxPowerSavingPreambleForSF(7), 32);
  EXPECT_EQ(rxPowerSavingPreambleForSF(8), 32);
  EXPECT_EQ(rxPowerSavingPreambleForSF(9), 16);
  EXPECT_EQ(rxPowerSavingPreambleForSF(12), 16);
}

TEST(RxPowerSaving, LevelIntentRetunesAfterRadioChange) {
  uint32_t rx_us = 0;
  uint32_t sleep_us = 0;

  ASSERT_TRUE(recalcRxPowerSavingFromLevel(5, 8, 62.5f, 16, &rx_us, &sleep_us));
  uint32_t old_rx_us = rx_us;
  uint32_t old_sleep_us = sleep_us;

  ASSERT_TRUE(recalcRxPowerSavingFromLevel(5, 9, 62.5f, 16, &rx_us, &sleep_us));
  EXPECT_NEAR((double)rx_us, (double)old_rx_us * 2.0, 1.0);
  EXPECT_NEAR((double)sleep_us, (double)old_sleep_us * 2.0, 1.0);
}

TEST(RxPowerSaving, AutomaticPreambleRetunesAcrossSfBoundary) {
  uint32_t automatic_rx_us = 0;
  uint32_t automatic_sleep_us = 0;
  uint32_t explicit_rx_us = 0;
  uint32_t explicit_sleep_us = 0;

  ASSERT_TRUE(recalcRxPowerSavingFromLevel(5, 8, 250.0f, 0,
                                           &automatic_rx_us, &automatic_sleep_us));
  ASSERT_TRUE(calcRxPowerSavingLevel(5, 8, 250.0f, 32,
                                     &explicit_rx_us, &explicit_sleep_us));
  EXPECT_EQ(automatic_rx_us, explicit_rx_us);
  EXPECT_EQ(automatic_sleep_us, explicit_sleep_us);

  ASSERT_TRUE(recalcRxPowerSavingFromLevel(5, 9, 250.0f, 0,
                                           &automatic_rx_us, &automatic_sleep_us));
  ASSERT_TRUE(calcRxPowerSavingLevel(5, 9, 250.0f, 16,
                                     &explicit_rx_us, &explicit_sleep_us));
  EXPECT_EQ(automatic_rx_us, explicit_rx_us);
  EXPECT_EQ(automatic_sleep_us, explicit_sleep_us);
}

TEST(RxPowerSaving, ManualTimingsAreNotRetuned) {
  uint32_t rx_us = 12345;
  uint32_t sleep_us = 23456;

  EXPECT_FALSE(recalcRxPowerSavingFromLevel(0, 10, 250.0f, 16, &rx_us, &sleep_us));
  EXPECT_EQ(rx_us, 12345U);
  EXPECT_EQ(sleep_us, 23456U);
}

TEST(RxPowerSavingCLI, AppliesNamedAndManualProfiles) {
  RxPowerSavingConfig config;
  FakeRxPowerSavingControl control;
  char reply[192];

  ASSERT_TRUE(RXPowerSavingCLI::set("balanced", 10, 250.0f, &config, &control,
                                    reply, sizeof(reply)));
  EXPECT_TRUE(control.set_called);
  EXPECT_TRUE(control.requested_enabled);
  EXPECT_EQ(config.enabled, 1);
  EXPECT_EQ(config.level, RX_POWERSAVING_BALANCED_LEVEL);
  EXPECT_EQ(config.preamble, RX_POWERSAVING_PROFILE_PREAMBLE);
  EXPECT_EQ(config.rx_us, 41871U);
  EXPECT_EQ(config.sleep_us, 26851U);

  control.set_called = false;
  ASSERT_TRUE(RXPowerSavingCLI::set("12345 23456", 10, 250.0f, &config, &control,
                                    reply, sizeof(reply)));
  EXPECT_TRUE(control.set_called);
  EXPECT_EQ(config.level, 0);
  EXPECT_EQ(config.preamble, 0);
  EXPECT_EQ(config.rx_us, 12345U);
  EXPECT_EQ(config.sleep_us, 23456U);
}

TEST(RxPowerSavingCLI, StoresAutomaticPreambleIntentForLevel) {
  RxPowerSavingConfig config;
  FakeRxPowerSavingControl control;
  char reply[192];

  ASSERT_TRUE(RXPowerSavingCLI::set("level 5", 8, 250.0f, &config, &control,
                                    reply, sizeof(reply)));
  EXPECT_EQ(config.level, 5);
  EXPECT_EQ(config.preamble, 0);

  uint32_t expected_rx_us = 0;
  uint32_t expected_sleep_us = 0;
  ASSERT_TRUE(calcRxPowerSavingLevel(5, 8, 250.0f, 32,
                                     &expected_rx_us, &expected_sleep_us));
  EXPECT_EQ(config.rx_us, expected_rx_us);
  EXPECT_EQ(config.sleep_us, expected_sleep_us);
}

TEST(RxPowerSavingCLI, DoesNotPersistRejectedOrInvalidChanges) {
  RxPowerSavingConfig config;
  RxPowerSavingConfig original = config;
  FakeRxPowerSavingControl control;
  char reply[192];

  EXPECT_FALSE(RXPowerSavingCLI::set("level 0", 10, 250.0f, &config, &control,
                                     reply, sizeof(reply)));
  EXPECT_EQ(config.enabled, original.enabled);
  EXPECT_EQ(config.level, original.level);
  EXPECT_STREQ(reply, "ERROR: level range is 1-10; preamble is 16 or 32");

  control.accept = false;
  EXPECT_FALSE(RXPowerSavingCLI::set("balanced", 10, 250.0f, &config, &control,
                                     reply, sizeof(reply)));
  EXPECT_EQ(config.enabled, original.enabled);
  EXPECT_EQ(config.level, original.level);
  EXPECT_STREQ(reply, "ERROR: RX powersaving unsupported");
}

TEST(RxPowerSavingCLI, RejectsValuesBeforeNarrowingOrOverflow) {
  RxPowerSavingConfig config;
  const RxPowerSavingConfig original = config;
  FakeRxPowerSavingControl control;
  char reply[192];

  const char* invalid_values[] = {
    "261",                         // used to alias to level 5 after uint8_t truncation
    "level 266",                   // used to alias to level 10
    "level 5 preamble 272",        // used to alias to preamble 16
    "4294967296 1000",             // one above UINT32_MAX
    "999999999999999999999999999", // must not wrap during parsing
  };

  for (const char* value : invalid_values) {
    control.set_called = false;
    EXPECT_FALSE(RXPowerSavingCLI::set(value, 10, 250.0f, &config, &control,
                                       reply, sizeof(reply))) << value;
    EXPECT_FALSE(control.set_called) << value;
    EXPECT_EQ(config.enabled, original.enabled) << value;
    EXPECT_EQ(config.level, original.level) << value;
    EXPECT_EQ(config.preamble, original.preamble) << value;
    EXPECT_EQ(config.rx_us, original.rx_us) << value;
    EXPECT_EQ(config.sleep_us, original.sleep_us) << value;
  }
}

TEST(RxPowerSavingCLI, DisablingKeepsTimingIntent) {
  RxPowerSavingConfig config;
  config.enabled = 1;
  config.level = 7;
  config.preamble = 32;
  config.rx_us = 34567;
  config.sleep_us = 45678;
  FakeRxPowerSavingControl control;
  char reply[192];

  ASSERT_TRUE(RXPowerSavingCLI::set("off", 10, 250.0f, &config, &control,
                                    reply, sizeof(reply)));
  EXPECT_FALSE(control.requested_enabled);
  EXPECT_EQ(config.enabled, 0);
  EXPECT_EQ(config.level, 7);
  EXPECT_EQ(config.preamble, 32);
  EXPECT_EQ(config.rx_us, 34567U);
  EXPECT_EQ(config.sleep_us, 45678U);
}

TEST(RxPowerSavingCLI, FormatsDesiredAndEffectiveStateSeparately) {
  RxPowerSavingConfig config;
  config.enabled = 1;
  FakeRxPowerSavingControl control;
  control.status = {true, false, -706, 2, 0, 0};
  char reply[192];

  RXPowerSavingCLI::get(&config, &control, reply, sizeof(reply));

  EXPECT_NE(std::strstr(reply, "desired=on,effective=continuous,supported=yes"), nullptr);
  EXPECT_NE(std::strstr(reply, "err=-706,fail=2"), nullptr);
  EXPECT_EQ(std::strstr(reply, "erx="), nullptr);   // nothing armed, nothing to report
}

TEST(RxPowerSavingCLI, ReportsClampedPeriodsOnlyWhenTheyDiffer) {
  RxPowerSavingConfig config;
  config.enabled = 1;
  FakeRxPowerSavingControl control;
  char reply[192];

  // driver armed exactly what was asked for -> no extra fields
  control.status = {true, true, 0, 0, config.rx_us, config.sleep_us};
  RXPowerSavingCLI::get(&config, &control, reply, sizeof(reply));
  EXPECT_NE(std::strstr(reply, "effective=armed"), nullptr);
  EXPECT_EQ(std::strstr(reply, "erx="), nullptr);

  // driver had to stretch the RX window -> surface the real values
  control.status = {true, true, 0, 0, config.rx_us + 4000, config.sleep_us};
  RXPowerSavingCLI::get(&config, &control, reply, sizeof(reply));
  EXPECT_NE(std::strstr(reply, "erx=69625,eslp=60000"), nullptr);
}

TEST(RxPowerSavingCLI, GetDoesNotMutateStoredConfig) {
  RxPowerSavingConfig config;
  config.rx_us = 0;         // invalid, e.g. from an older persisted prefs file
  config.sleep_us = 999;
  FakeRxPowerSavingControl control;
  char reply[192];

  RXPowerSavingCLI::get(&config, &control, reply, sizeof(reply));

  EXPECT_EQ(config.rx_us, 0U);
  EXPECT_EQ(config.sleep_us, 999U);
}

TEST(RxPowerSavingCLI, RfRxDiagnosticIsRuntimeOnlyAndCapabilityGated) {
  FakeRxPowerSavingControl control;
  char reply[192];

  RXPowerSavingCLI::setRfRxDisabled("on", &control, reply, sizeof(reply));
  EXPECT_STREQ(reply, "Error: unsupported");

  control.rf_rx_supported = true;
  RXPowerSavingCLI::setRfRxDisabled("on", &control, reply, sizeof(reply));
  EXPECT_TRUE(control.rf_rx_disabled);
  EXPECT_STREQ(reply, "OK - radio.rxps.rfrx_disabled on");

  RXPowerSavingCLI::getRfRxDisabled(&control, reply, sizeof(reply));
  EXPECT_STREQ(reply, "> on");
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
