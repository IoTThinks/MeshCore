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
  RxPowerSavingStatus status;

  bool setRxPowerSaving(bool enabled, uint32_t rx_us, uint32_t sleep_us) override {
    set_called = true;
    requested_enabled = enabled;
    requested_rx_us = rx_us;
    requested_sleep_us = sleep_us;
    return accept;
  }

  RxPowerSavingStatus getRxPowerSavingStatus() const override { return status; }
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
  // 8 is the top of the guarded scale, 9 overdrive, 10 the practical maximum.
  EXPECT_TRUE(calcRxPowerSavingLevel(8, 10, 250.0f, 16, &rx_us, &sleep_us));
  EXPECT_TRUE(calcRxPowerSavingLevel(9, 10, 250.0f, 16, &rx_us, &sleep_us));
  EXPECT_TRUE(calcRxPowerSavingLevel(10, 10, 250.0f, 16, &rx_us, &sleep_us));
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
  EXPECT_EQ(config.rx_us, 49329U);
  EXPECT_EQ(config.sleep_us, 23757U);
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

TEST(RxPowerSaving, CompanionProfileIsBalancedPreambleSixteen) {
  uint32_t rx_us = 0;
  uint32_t sleep_us = 0;

  ASSERT_TRUE(calcRxPowerSavingLevel(RX_POWERSAVING_BALANCED_LEVEL, 10, 250.0f, 16,
                                     &rx_us, &sleep_us));
  EXPECT_EQ(rx_us, 49329U);
  EXPECT_EQ(sleep_us, 23757U);
}

TEST(RxPowerSaving, NamedProfilesSitWhereTheScaleSaysTheyDo) {
  EXPECT_EQ(RX_POWERSAVING_CONSERVATIVE_LEVEL, 3);
  EXPECT_EQ(rxPowerSavingLevelCatch(RX_POWERSAVING_CONSERVATIVE_LEVEL, 16), 13.0f);
  EXPECT_EQ(rxPowerSavingLevelCatch(RX_POWERSAVING_CONSERVATIVE_LEVEL, 32), 16.0f);
  EXPECT_EQ(RX_POWERSAVING_BALANCED_LEVEL, 6);
  EXPECT_EQ(rxPowerSavingLevelCatch(RX_POWERSAVING_BALANCED_LEVEL, 16), 10.0f);
  EXPECT_EQ(rxPowerSavingLevelCatch(RX_POWERSAVING_BALANCED_LEVEL, 32), 10.0f);
  // The floor is shared by both profiles, and is what makes a level mean the
  // same geometry on an SX126x and an LR11x0.
  EXPECT_EQ(rxPowerSavingLevelCatch(RX_POWERSAVING_MAX_LEVEL, 16),
            RX_POWERSAVING_MIN_CATCH_SYMBOLS);
  EXPECT_EQ(rxPowerSavingLevelCatch(RX_POWERSAVING_MAX_LEVEL, 32),
            RX_POWERSAVING_MIN_CATCH_SYMBOLS);
  EXPECT_EQ(RX_POWERSAVING_PROFILE_PREAMBLE, 16);

  // Margins fall monotonically and end at zero; nothing outside the guarded
  // scale reports one.
  for (uint8_t lv = 2; lv <= RX_POWERSAVING_GUARDED_LEVELS; lv++) {
    EXPECT_LT(rxPowerSavingLevelCatch(lv, 16), rxPowerSavingLevelCatch(lv - 1, 16));
    EXPECT_LT(rxPowerSavingLevelCatch(lv, 32), rxPowerSavingLevelCatch(lv - 1, 32));
  }
  EXPECT_EQ(rxPowerSavingLevelCatch(RX_POWERSAVING_OVERDRIVE_LEVEL, 16), 0.0f);
  EXPECT_EQ(rxPowerSavingLevelCatch(0, 16), 0.0f);
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
  // Sleep is pure symbols, so it doubles with the symbol time. The listen
  // window no longer does: the SetRxDutyCycle timer guard adds half of the
  // sleep->RX transition, which is a fixed number of microseconds and does not
  // scale with SF. So the window grows, but by less than a factor of two.
  EXPECT_NEAR((double)sleep_us, (double)old_sleep_us * 2.0, 32.0);
  EXPECT_GT(rx_us, old_rx_us);
  EXPECT_LT((double)rx_us, (double)old_rx_us * 2.0);
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

  ASSERT_TRUE(RXPowerSavingCLI::set("conservative", 10, 250.0f, &config, &control,
                                    reply, sizeof(reply)));
  EXPECT_EQ(config.level, 3);
  EXPECT_EQ(config.preamble, 16);

  ASSERT_TRUE(RXPowerSavingCLI::set("balanced", 10, 250.0f, &config, &control,
                                    reply, sizeof(reply)));
  EXPECT_TRUE(control.set_called);
  EXPECT_TRUE(control.requested_enabled);
  EXPECT_EQ(config.enabled, 1);
  EXPECT_EQ(config.level, RX_POWERSAVING_BALANCED_LEVEL);
  EXPECT_EQ(config.level, 6);
  EXPECT_EQ(config.preamble, RX_POWERSAVING_PROFILE_PREAMBLE);
  EXPECT_EQ(config.rx_us, 49329U);
  EXPECT_EQ(config.sleep_us, 23757U);

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
  EXPECT_STREQ(reply, "ERROR: level range is 1-10 (or max|overdrive|riskyWorkingMax); preamble is 16 or 32");

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

  RXPowerSavingCLI::get(&config, &control, 10, 250.0f, reply, sizeof(reply));

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
  RXPowerSavingCLI::get(&config, &control, 10, 250.0f, reply, sizeof(reply));
  EXPECT_NE(std::strstr(reply, "effective=armed"), nullptr);
  EXPECT_EQ(std::strstr(reply, "erx="), nullptr);

  // driver had to stretch the RX window -> surface the real values
  control.status = {true, true, 0, 0, config.rx_us + 4000, config.sleep_us};
  RXPowerSavingCLI::get(&config, &control, 10, 250.0f, reply, sizeof(reply));
  EXPECT_NE(std::strstr(reply, "erx=69625,eslp=60000"), nullptr);
}

TEST(RxPowerSavingCLI, GetDoesNotMutateStoredConfig) {
  RxPowerSavingConfig config;
  config.rx_us = 0;         // invalid, e.g. from an older persisted prefs file
  config.sleep_us = 999;
  FakeRxPowerSavingControl control;
  char reply[192];

  RXPowerSavingCLI::get(&config, &control, 10, 250.0f, reply, sizeof(reply));

  EXPECT_EQ(config.rx_us, 0U);
  EXPECT_EQ(config.sleep_us, 999U);
}


int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

// Measured on SX1262 at SF6/BW62.5: the P16 profile's own sleep is below the
// driver's arming floor on levels 1-5 (and on levels 1-2 at SF7), where
// startReceiveDutyCycle answers -708 and the wrapper falls back to continuous
// RX while the config still reports power saving as on.
TEST(RxPowerSaving, ShortSleepIsRaisedToTheArmingFloor) {
  uint32_t rx_us = 0;
  uint32_t sleep_us = 0;

  // SF6/BW62.5: level 1 of the P16 profile asks for 2 symbols = 2048 us.
  ASSERT_TRUE(calcRxPowerSavingLevel(1, 6, 62.5f, 16, &rx_us, &sleep_us));
  EXPECT_EQ(sleep_us, RX_POWERSAVING_MIN_SLEEP_US);
  // Raising it must not break capture: the budget is preamble - cost symbols.
  EXPECT_LE(sleep_us, (uint32_t)((16.0f - RX_POWERSAVING_CAPTURE_COST_SYMBOLS) * 1024.0f));

  // Only level 2 still lands under the floor. Before MC_TCXO_DELAY_US dropped
  // the transition from 6000 to 2600 us the floor was 6250 us and swallowed
  // levels 2-6 as well, so most of the 16-symbol ladder collapsed onto one
  // point at SF6 - the node quietly slept longer, and caught fewer symbols,
  // than the level it was set to promised.
  for (uint8_t lv = 2; lv <= 2; lv++) {
    uint32_t rx_n = 0;
    uint32_t sleep_n = 0;
    ASSERT_TRUE(calcRxPowerSavingLevel(lv, 6, 62.5f, 16, &rx_n, &sleep_n)) << (int)lv;
    EXPECT_EQ(sleep_n, RX_POWERSAVING_MIN_SLEEP_US) << (int)lv;
    EXPECT_EQ(rx_n, rx_us) << (int)lv;
  }

  // Level 4 sleeps 3.8 symbols, which now fits above the floor, so it gets its
  // own point and saves more power than the collapsed ones.
  uint32_t rx5_us = 0;
  uint32_t sleep5_us = 0;
  ASSERT_TRUE(calcRxPowerSavingLevel(4, 6, 62.5f, 16, &rx5_us, &sleep5_us));
  EXPECT_GT(sleep5_us, RX_POWERSAVING_MIN_SLEEP_US);
  EXPECT_LT((double)rx5_us / (rx5_us + sleep5_us),
            (double)rx_us / (rx_us + sleep_us));
}

TEST(RxPowerSaving, RejectsProfilesWhoseFloorWouldBreakCapture) {
  uint32_t rx_us = 0;
  uint32_t sleep_us = 0;

  // SF6/BW250: the symbol is 256 us, so the whole P16 capture budget is
  // 10 symbols = 2560 us - under the arming floor. No level can duty cycle
  // here, and the caller has to hear that rather than get a silent fallback.
  //
  // This used to be SF7/BW250, which the lower transition has since brought
  // within reach: its budget is 5120 us against a floor that fell from 6250 to
  // 2850, so the whole P16 ladder became armable there. The refusal path still
  // needs a setting that is genuinely out of reach, so the example moved down
  // one SF rather than away.
  EXPECT_FALSE(calcRxPowerSavingLevel(1, 6, 250.0f, 16, &rx_us, &sleep_us));
  EXPECT_FALSE(calcRxPowerSavingLevel(RX_POWERSAVING_MAX_LEVEL, 6, 250.0f, 16,
                                     &rx_us, &sleep_us));

  // The P32 profile has 26 symbols of budget at the same setting and survives.
  EXPECT_TRUE(calcRxPowerSavingLevel(1, 6, 250.0f, 32, &rx_us, &sleep_us));
  EXPECT_GE(sleep_us, RX_POWERSAVING_MIN_SLEEP_US);

  // And the setting that moved: SF7/BW250 with P16 now arms on every level.
  EXPECT_TRUE(calcRxPowerSavingLevel(1, 7, 250.0f, 16, &rx_us, &sleep_us));
  EXPECT_TRUE(calcRxPowerSavingLevel(RX_POWERSAVING_MAX_LEVEL, 7, 250.0f, 16,
                                     &rx_us, &sleep_us));
}

TEST(RxPowerSaving, TransitionTimeComesFromTheRadioLikeCaptureCost) {
  class SlowTcxoControl : public RxPowerSavingControl {
  public:
    uint32_t rxPowerSavingTransitionUs() const override { return 11750; }
  } slow;

  EXPECT_EQ(rxPowerSavingTransition(nullptr), RX_POWERSAVING_TRANSITION_US);
  EXPECT_EQ(rxPowerSavingTransition(&slow), 11750u);

  // SF7 rather than SF6: a 12 ms floor does not fit inside the 10-symbol
  // capture budget of a 1024 us symbol, which is the rejection case above.
  uint32_t rx_us = 0;
  uint32_t sleep_us = 0;
  ASSERT_TRUE(calcRxPowerSavingLevel(1, 7, 62.5f, 16, &rx_us, &sleep_us,
                                     RX_POWERSAVING_CAPTURE_COST_SYMBOLS,
                                     rxPowerSavingTransition(&slow)));
  // Floor is transition + margin, snapped to a whole 15.625 us tick.
  EXPECT_GE(sleep_us, 12000u);
  EXPECT_LT(sleep_us, 12020u);
}

TEST(RxPowerSavingCLI, ReportsWhenNoLevelCanDutyCycleAtThisRadioSetting) {
  RxPowerSavingConfig config;
  FakeRxPowerSavingControl control;
  char reply[160];

  // SF6/BW250 with the P16 profile: nothing between the arming floor and the
  // capture budget, so the level must be refused with a message that names the
  // real reason instead of blaming the level range.
  EXPECT_FALSE(RXPowerSavingCLI::set("level 5 preamble 16", 6, 250.0f, &config, &control,
                                     reply, sizeof(reply)));
  EXPECT_NE(strstr(reply, "SF/BW"), nullptr);
  EXPECT_FALSE(control.set_called);
  EXPECT_EQ(config.enabled, 0);
}

// SX1261/2 datasheet, SetRxDutyCycle: on preamble detection the radio restarts
// its timer with 2*rxPeriod + sleepPeriod and requires
//     Tpreamble + Theader <= 2 * rxPeriod + sleepPeriod
// Measured on two SX1262 boards: when that holds nothing goes wrong, and when
// it is broken the chip usually gets away with it - except at isolated register
// values one tick wide (rxPeriod 320 and 640) where it loses 35-100% of the
// packets it has already latched. Satisfying the condition is the only defence
// that does not depend on knowing every bad tick.
TEST(RxPowerSaving, EveryLevelSatisfiesTheDutyCycleTimerCondition) {
  const uint8_t sfs[] = {6, 7, 8, 10, 12};
  const float bws[] = {62.5f, 250.0f};
  const uint8_t preambles[] = {16, 32};

  for (uint8_t sf : sfs) {
    for (float bw : bws) {
      for (uint8_t preamble : preambles) {
        for (uint8_t level = 1; level <= RX_POWERSAVING_GUARDED_LEVELS; level++) {
          uint32_t rx_us = 0;
          uint32_t sleep_us = 0;
          if (!calcRxPowerSavingLevel(level, sf, bw, preamble, &rx_us, &sleep_us)) {
            continue;   // rejected outright; nothing is armed, nothing to check
          }
          const float symbol_us = (1000.0f * (float)(1UL << sf)) / bw;
          const float sync = sf <= 6 ? RX_POWERSAVING_SYNC_SYMBOLS_LOW_SF
                                     : RX_POWERSAVING_SYNC_SYMBOLS;
          const float need =
              ((float)preamble + sync + RX_POWERSAVING_HEADER_SYMBOLS) * symbol_us;
          // Compare on the register values the radio really runs, not on the
          // microseconds we asked for: the driver truncates to 15.625 us ticks.
          const uint32_t rx_ticks = (rx_us * 8) / 125;
          const uint32_t sleep_ticks =
              ((sleep_us - RX_POWERSAVING_TRANSITION_US) * 8) / 125;
          const float restarted = (2.0f * rx_ticks + sleep_ticks) * 15.625f;
          EXPECT_GE(restarted, need)
              << "SF" << (int)sf << " BW" << bw << " P" << (int)preamble
              << " level " << (int)level;
          // And the periods handed out must themselves be whole ticks, so the
          // CLI reports what the hardware runs.
          EXPECT_EQ((rx_ticks * 125 + 7) / 8, rx_us);
        }
      }
    }
  }
}

// Level 11 is the measured maximum: the geometry the bench ran before the timer
// guard existed, kept because the guard costs 4 to 12 percentage points of duty
// cycle. 8 symbols of listening, sleep right at the capture budget.
TEST(RxPowerSaving, OverdriveIsTheMeasuredGeometryAtEverySf) {
  const uint8_t sfs[] = {6, 7, 8, 10};
  for (uint8_t sf : sfs) {
    for (uint8_t preamble : {16, 32}) {
      uint32_t rx_us = 0;
      uint32_t sleep_us = 0;
      ASSERT_TRUE(calcRxPowerSavingLevel(RX_POWERSAVING_OVERDRIVE_LEVEL, sf, 62.5f,
                                         (uint8_t)preamble, &rx_us, &sleep_us))
          << "SF" << (int)sf << " P" << preamble;
      const float symbol_us = (1000.0f * (float)(1UL << sf)) / 62.5f;
      EXPECT_NEAR(rx_us / symbol_us, 8.0f  /* rx_edge_symbols */, 0.02f);
      EXPECT_NEAR(sleep_us / symbol_us,
                  preamble - RX_POWERSAVING_CAPTURE_COST_SYMBOLS, 0.02f);
      // The whole point: 23.5% duty on P32, 44.4% on P16, at every SF.
      const double duty = 100.0 * rx_us / (rx_us + sleep_us);
      EXPECT_NEAR(duty, preamble == 32 ? 23.5 : 44.4, 0.2);
    }
  }
}

TEST(RxPowerSaving, OverdriveDeliberatelyBreaksTheTimerCondition) {
  // Stated as a test so nobody later "fixes" it into compliance by accident:
  // level 11 exists precisely because it sits outside the datasheet rule, and
  // it was measured lossless there on three boards across SF6, SF7 and SF8.
  uint32_t rx_us = 0;
  uint32_t sleep_us = 0;
  ASSERT_TRUE(calcRxPowerSavingLevel(RX_POWERSAVING_OVERDRIVE_LEVEL, 6, 62.5f, 32,
                                     &rx_us, &sleep_us));
  const float symbol_us = (1000.0f * 64.0f) / 62.5f;
  const float need = (32.0f + RX_POWERSAVING_SYNC_SYMBOLS_LOW_SF +
                      RX_POWERSAVING_HEADER_SYMBOLS) * symbol_us;
  const uint32_t rx_ticks = (rx_us * 8) / 125;
  const uint32_t sleep_ticks = ((sleep_us - RX_POWERSAVING_TRANSITION_US) * 8) / 125;
  EXPECT_LT((2.0f * rx_ticks + sleep_ticks) * RX_POWERSAVING_TICK_US, need);

  // and the top of the guarded scale must still satisfy it
  uint32_t g_rx = 0;
  uint32_t g_sleep = 0;
  ASSERT_TRUE(calcRxPowerSavingLevel(RX_POWERSAVING_MAX_LEVEL, 6, 62.5f, 32, &g_rx, &g_sleep));
  const uint32_t g_rx_ticks = (g_rx * 8) / 125;
  const uint32_t g_sleep_ticks = ((g_sleep - RX_POWERSAVING_TRANSITION_US) * 8) / 125;
  EXPECT_GE((2.0f * g_rx_ticks + g_sleep_ticks) * RX_POWERSAVING_TICK_US, need);
  EXPECT_LT(rx_us, g_rx);            // and it really does listen less
}

TEST(RxPowerSaving, OverdriveStepsOffTheKnownBadRegisterTicks) {
  // 8 symbols land on rxPeriod tick 320 when the symbol is 625 us. No standard
  // bandwidth produces that, which is why production never hit it - but the
  // unguarded path has no other protection, so the check has to work.
  uint32_t rx_us = 0;
  uint32_t sleep_us = 0;
  ASSERT_TRUE(calcRxPowerSavingLevel(RX_POWERSAVING_OVERDRIVE_LEVEL, 6, 102.4f, 32,
                                     &rx_us, &sleep_us));
  EXPECT_EQ((rx_us * 8) / 125, 321u);   // nudged one tick clear of 320
  EXPECT_EQ(rx_us, 5016u);

  // The guarded scale needs no such help and must be left alone.
  uint32_t g_rx = 0;
  uint32_t g_sleep = 0;
  ASSERT_TRUE(calcRxPowerSavingLevel(10, 6, 102.4f, 32, &g_rx, &g_sleep));
  EXPECT_NE((g_rx * 8) / 125, 320u);
}

TEST(RxPowerSavingCLI, OverdriveAndMaxPresetsSelectTheRightLevels) {
  RxPowerSavingConfig config;
  FakeRxPowerSavingControl control;
  char reply[160];

  // `max` is the top of the guarded scale, and like every named preset it
  // assumes a 16-symbol sender, so at SF<=8 it is deliberately less economical
  // than `level 8`, which follows the SF onto the 32-symbol profile.
  ASSERT_TRUE(RXPowerSavingCLI::set("max", 8, 62.5f, &config, &control,
                                    reply, sizeof(reply)));
  EXPECT_EQ(config.level, RX_POWERSAVING_MAX_LEVEL);
  EXPECT_EQ(config.level, 8);
  EXPECT_EQ(rxPowerSavingLevelCatch(config.level, 16), RX_POWERSAVING_MIN_CATCH_SYMBOLS);
  EXPECT_EQ(config.preamble, 16);
  EXPECT_EQ(strstr(reply, "overdrive"), nullptr);
  const uint32_t max_rx = config.rx_us;
  const uint32_t max_sleep = config.sleep_us;

  ASSERT_TRUE(RXPowerSavingCLI::set("level 8", 8, 62.5f, &config, &control,
                                    reply, sizeof(reply)));
  EXPECT_EQ(config.preamble, 0);
  EXPECT_LT((double)config.rx_us / (config.rx_us + config.sleep_us),
            (double)max_rx / (max_rx + max_sleep));

  // `overdrive` is one step past it, on the same worst-case preamble.
  ASSERT_TRUE(RXPowerSavingCLI::set("overdrive", 8, 62.5f, &config, &control,
                                    reply, sizeof(reply)));
  EXPECT_EQ(config.level, RX_POWERSAVING_OVERDRIVE_LEVEL);
  EXPECT_EQ(config.preamble, 16);
  EXPECT_NE(strstr(reply, "overdrive"), nullptr);
  EXPECT_LT(config.rx_us, max_rx);        // and really does listen less

  control.status.supported = true;
  control.status.armed = true;
  RXPowerSavingCLI::get(&config, &control, 10, 250.0f, reply, sizeof(reply));
  EXPECT_NE(strstr(reply, "(overdrive)"), nullptr);

  // Both names take an explicit preamble, which is how the 32-symbol profile
  // is reached without giving up the preset.
  ASSERT_TRUE(RXPowerSavingCLI::set("overdrive preamble 32", 8, 62.5f, &config,
                                    &control, reply, sizeof(reply)));
  EXPECT_EQ(config.level, RX_POWERSAVING_OVERDRIVE_LEVEL);
  EXPECT_EQ(config.preamble, 32);
  ASSERT_TRUE(RXPowerSavingCLI::set("max preamble 32", 8, 62.5f, &config,
                                    &control, reply, sizeof(reply)));
  EXPECT_EQ(config.level, RX_POWERSAVING_MAX_LEVEL);
  EXPECT_EQ(config.preamble, 32);

  EXPECT_FALSE(RXPowerSavingCLI::set("overdrive preamble 24", 8, 62.5f, &config,
                                     &control, reply, sizeof(reply)));
  EXPECT_FALSE(RXPowerSavingCLI::set("max preamble", 8, 62.5f, &config,
                                     &control, reply, sizeof(reply)));
  EXPECT_FALSE(RXPowerSavingCLI::set("level 11", 8, 62.5f, &config, &control,
                                     reply, sizeof(reply)));
}

TEST(RxPowerSaving, RiskyWorkingMaxIsTheMeasuredEdgeAndCostsDelivery) {
  // The bench walked the profile past level 10 until delivery came back:
  // virtual 11.0 for P32, 10.25 for P16. Both put the sleep beyond the capture
  // budget, which is the whole reason they lose packets - 196/200 and 197/200
  // at SF8 with an LR1110 witnessing every transmission.
  const float symbol_us = (1000.0f * 256.0f) / 62.5f;   // SF8 / BW62.5

  uint32_t rx32 = 0;
  uint32_t sleep32 = 0;
  ASSERT_TRUE(calcRxPowerSavingLevel(RX_POWERSAVING_RISKY_WORKING_MAX_LEVEL, 8, 62.5f, 32,
                                     &rx32, &sleep32));
  EXPECT_NEAR(rx32 / symbol_us, 7.111f, 0.02f);
  EXPECT_NEAR(sleep32 / symbol_us, 27.222f, 0.02f);
  EXPECT_GT(sleep32 / symbol_us, 32.0f - RX_POWERSAVING_CAPTURE_COST_SYMBOLS);

  uint32_t rx16 = 0;
  uint32_t sleep16 = 0;
  ASSERT_TRUE(calcRxPowerSavingLevel(RX_POWERSAVING_RISKY_WORKING_MAX_LEVEL, 8, 62.5f, 16,
                                     &rx16, &sleep16));
  EXPECT_NEAR(rx16 / symbol_us, 7.889f, 0.02f);
  EXPECT_NEAR(sleep16 / symbol_us, 10.222f, 0.02f);
  EXPECT_GT(sleep16 / symbol_us, 16.0f - RX_POWERSAVING_CAPTURE_COST_SYMBOLS);

  // It must sleep more than overdrive, or it would have no reason to exist.
  uint32_t o_rx = 0;
  uint32_t o_sleep = 0;
  ASSERT_TRUE(calcRxPowerSavingLevel(RX_POWERSAVING_OVERDRIVE_LEVEL, 8, 62.5f, 32,
                                     &o_rx, &o_sleep));
  EXPECT_GT((double)sleep32 / (rx32 + sleep32), (double)o_sleep / (o_rx + o_sleep));

  // Both levels past the guarded scale skip the timer guard.
  EXPECT_TRUE(isRxPowerSavingUnguardedLevel(RX_POWERSAVING_RISKY_WORKING_MAX_LEVEL));
  EXPECT_TRUE(isRxPowerSavingUnguardedLevel(RX_POWERSAVING_OVERDRIVE_LEVEL));
  EXPECT_FALSE(isRxPowerSavingUnguardedLevel(RX_POWERSAVING_MAX_LEVEL));
}

TEST(RxPowerSavingCLI, RiskyWorkingMaxUsesOneNameEverywhere) {
  RxPowerSavingConfig config;
  FakeRxPowerSavingControl control;
  char reply[160];

  ASSERT_TRUE(RXPowerSavingCLI::set("riskyWorkingMax", 8, 62.5f, &config,
                                    &control, reply, sizeof(reply)));
  EXPECT_EQ(config.level, RX_POWERSAVING_RISKY_WORKING_MAX_LEVEL);
  EXPECT_EQ(config.preamble, 16);
  EXPECT_NE(strstr(reply, "(riskyWorkingMax)"), nullptr);

  ASSERT_TRUE(RXPowerSavingCLI::set("riskyWorkingMax preamble 32", 8, 62.5f,
                                    &config, &control, reply, sizeof(reply)));
  EXPECT_EQ(config.preamble, 32);

  control.status.supported = true;
  control.status.armed = true;
  RXPowerSavingCLI::get(&config, &control, 10, 250.0f, reply, sizeof(reply));
  EXPECT_NE(strstr(reply, "(riskyWorkingMax)"), nullptr);

  // The name remains case-sensitive and has no shortcut.
  EXPECT_FALSE(RXPowerSavingCLI::set("riskyworkingmax", 8, 62.5f, &config, &control,
                                     reply, sizeof(reply)));
}

// The whole point of the scale: level N means "a sender's preamble may be N
// symbols shorter than this profile assumes and still be caught". That has to
// hold on every SF, bandwidth and profile, because being dimensionless is the
// reason the scale is expressed this way rather than in microseconds or in
// milliamperes - both of which differ per board.
TEST(RxPowerSaving, EveryGuardedLevelDeliversTheMarginItPromises) {
  const uint8_t sfs[] = {7, 8, 9, 10, 12};
  const float bws[] = {62.5f, 125.0f, 250.0f};
  const uint8_t preambles[] = {16, 32};

  for (uint8_t sf : sfs) {
    for (float bw : bws) {
      for (uint8_t preamble : preambles) {
        for (uint8_t level = 1; level <= RX_POWERSAVING_GUARDED_LEVELS; level++) {
          uint32_t rx_us = 0;
          uint32_t sleep_us = 0;
          if (!calcRxPowerSavingLevel(level, sf, bw, preamble, &rx_us, &sleep_us)) {
            continue;   // rejected outright, nothing to check
          }
          if (sleep_us == RX_POWERSAVING_MIN_SLEEP_US) {
            continue;   // the floor ate the margin; reported by the CLI, not a bug
          }
          const float symbol_us = (1000.0f * (float)(1UL << sf)) / bw;
          const float caught = (float)preamble - (float)sleep_us / symbol_us;
          // The delivered catch is the ladder value plus the pad that keeps
          // every generated geometry off the defective register pairs.
          EXPECT_NEAR(caught,
                      rxPowerSavingLevelCatch(level, preamble) + RX_POWERSAVING_MARGIN_PAD_SYMBOLS,
                      0.05f)
              << "SF" << (int)sf << " BW" << bw << " P" << (int)preamble
              << " level " << (int)level;
        }
      }
    }
  }
}

TEST(RxPowerSaving, ALevelMeansTheSameGeometryOnBothRadioFamilies) {
  // This is what the catch scale buys that the old margin scale could not: the
  // capture cost is no longer part of the arithmetic, so a level produces the
  // same periods whether the driver declares 6 symbols (SX126x) or 8 (LR11x0).
  // Before, the same level number meant two different sleeps on the two chips.
  for (uint8_t level = 1; level <= RX_POWERSAVING_GUARDED_LEVELS; level++) {
    for (uint8_t preamble : {16, 32}) {
      uint32_t rx_sx = 0, sleep_sx = 0, rx_lr = 0, sleep_lr = 0;
      ASSERT_TRUE(calcRxPowerSavingLevel(level, 8, 62.5f, preamble, &rx_sx, &sleep_sx,
                                         RX_POWERSAVING_CAPTURE_COST_SYMBOLS));
      ASSERT_TRUE(calcRxPowerSavingLevel(level, 8, 62.5f, preamble, &rx_lr, &sleep_lr,
                                         RX_POWERSAVING_CAPTURE_COST_SYMBOLS_LR11X0));
      EXPECT_EQ(sleep_sx, sleep_lr) << "level " << (int)level << " P" << (int)preamble;
      EXPECT_EQ(rx_sx, rx_lr) << "level " << (int)level << " P" << (int)preamble;
    }
  }
}
