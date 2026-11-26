#include <gtest/gtest.h>

#include <cstring>

#include "helpers/radiolib/RXPowerSaving.h"
#include "helpers/radiolib/RXPowerSavingCLI.h"
#include "helpers/radiolib/SmartRxPowerSaving.h"

namespace {

constexpr uint32_t MIN = 60UL * 1000UL;
constexpr uint32_t HOUR = 60UL * MIN;

// Symbol time at the US preset (SF7, BW62.5).
constexpr float SYM_US = 1000.0f * 128.0f / 62.5f;

class RecordingControl : public RxPowerSavingControl {
public:
  int configure_calls = 0;
  bool last_enabled = false, last_smart = false;
  uint8_t last_level = 0, last_hold = 0;
  bool setRxPowerSaving(bool, uint32_t, uint32_t) override { return true; }
  void configureSmartRxPowerSaving(bool enabled, bool smart, uint8_t level, uint8_t,
                                   float, uint8_t hold_s) override {
    configure_calls++;
    last_enabled = enabled;
    last_smart = smart;
    last_level = level;
    last_hold = hold_s;
  }
};

void feedLong(RxPsSmartEngine& e, uint32_t now, int n) {
  for (int i = 0; i < n; i++) e.onMeasurement(now, 28.0f, RXPS_SMART_SHORT_BELOW_CLEAN);
}

}  // namespace

TEST(SmartRxps, ClassifiesCleanMeasurements) {
  EXPECT_EQ(classifyRxPsPreamble(13.5f, RXPS_SMART_SHORT_BELOW_CLEAN), RxPsPreambleClass::Short);
  EXPECT_EQ(classifyRxPsPreamble(16.9f, RXPS_SMART_SHORT_BELOW_CLEAN), RxPsPreambleClass::Short);
  EXPECT_EQ(classifyRxPsPreamble(18.0f, RXPS_SMART_SHORT_BELOW_CLEAN), RxPsPreambleClass::Ambiguous);
  EXPECT_EQ(classifyRxPsPreamble(29.0f, RXPS_SMART_SHORT_BELOW_CLEAN), RxPsPreambleClass::Long);
  EXPECT_EQ(classifyRxPsPreamble(-5.0f, RXPS_SMART_SHORT_BELOW_CLEAN), RxPsPreambleClass::Invalid);
  EXPECT_EQ(classifyRxPsPreamble(55.0f, RXPS_SMART_SHORT_BELOW_CLEAN), RxPsPreambleClass::Invalid);
}

TEST(SmartRxps, LongProfileOnlyTrustsLowValuesAsShort) {
  // Balanced P32 on SX1262: sleeps 21.8 symbols, capture cost 6.
  const float bound = rxPsSmartShortBelowForSleep(21.8f, 6.0f);
  EXPECT_LT(bound, 5.0f);
  // A 32-symbol sender caught late looks like 12 symbols: must not count as old.
  EXPECT_EQ(classifyRxPsPreamble(12.0f, bound), RxPsPreambleClass::Ambiguous);
  EXPECT_EQ(classifyRxPsPreamble(2.0f, bound), RxPsPreambleClass::Short);
  // The bound never exceeds the clean threshold.
  EXPECT_FLOAT_EQ(rxPsSmartShortBelowForSleep(0.0f, 6.0f), RXPS_SMART_SHORT_BELOW_CLEAN);
}

TEST(SmartRxps, RemainingSymbolsFromTimestamps) {
  // 20 symbols of preamble left plus 30 symbols of sync/header/payload.
  const float post = 30.0f * SYM_US;
  const uint32_t elapsed = (uint32_t)(50.0f * SYM_US);
  EXPECT_NEAR(rxPsSmartRemainingSymbols(elapsed, post, SYM_US), 20.0f, 0.01f);
  EXPECT_LT(rxPsSmartRemainingSymbols(elapsed, post, 0.0f), -50.0f);
}

TEST(SmartRxps, StartsSafeAndNeedsEvidenceBeforeLongProfile) {
  RxPsSmartEngine e;
  e.start(0);
  EXPECT_EQ(e.phase(), RxPsSmartPhase::Safe);
  EXPECT_FALSE(e.wantsLongProfile());

  // Enough time but no evidence: stay safe.
  e.tick(2 * HOUR);
  EXPECT_EQ(e.phase(), RxPsSmartPhase::Safe);

  // Evidence but too early: stay safe.
  RxPsSmartEngine early;
  early.start(0);
  feedLong(early, 1 * MIN, 50);
  early.tick(5 * MIN);
  EXPECT_EQ(early.phase(), RxPsSmartPhase::Safe);

  feedLong(e, 2 * HOUR, 10);
  EXPECT_TRUE(e.tick(2 * HOUR + 1));
  EXPECT_EQ(e.phase(), RxPsSmartPhase::Efficient);
  EXPECT_TRUE(e.wantsLongProfile());
}

TEST(SmartRxps, OldSenderForcesSafeAndHoldsForClearTime) {
  RxPsSmartEngine e;
  e.start(0);
  feedLong(e, 1 * HOUR, 20);
  e.tick(1 * HOUR);
  ASSERT_EQ(e.phase(), RxPsSmartPhase::Efficient);

  EXPECT_EQ(e.onMeasurement(1 * HOUR + MIN, 14.0f, RXPS_SMART_SHORT_BELOW_CLEAN),
            RxPsPreambleClass::Short);
  EXPECT_EQ(e.phase(), RxPsSmartPhase::Safe);
  EXPECT_FALSE(e.wantsLongProfile());

  // Plenty of new senders, but the old one was heard 3 h ago: stay safe.
  feedLong(e, 2 * HOUR, 30);
  e.tick(4 * HOUR);
  EXPECT_EQ(e.phase(), RxPsSmartPhase::Safe);

  // Six quiet hours later it may save power again.
  e.tick(7 * HOUR + 2 * MIN);
  EXPECT_EQ(e.phase(), RxPsSmartPhase::Efficient);
}

TEST(SmartRxps, LongProfileIsAuditedPeriodically) {
  RxPsSmartEngine e;
  e.start(0);
  feedLong(e, 0, 20);
  e.tick(31 * MIN);
  ASSERT_EQ(e.phase(), RxPsSmartPhase::Efficient);
  e.tick(31 * MIN + 50 * MIN);
  EXPECT_EQ(e.phase(), RxPsSmartPhase::Audit);
  EXPECT_FALSE(e.wantsLongProfile());
  e.tick(31 * MIN + 61 * MIN);
  EXPECT_EQ(e.phase(), RxPsSmartPhase::Efficient);
}

TEST(SmartRxps, AmbiguousAndInvalidChangeNothing) {
  RxPsSmartEngine e;
  e.start(0);
  feedLong(e, 0, 20);
  e.tick(31 * MIN);
  ASSERT_EQ(e.phase(), RxPsSmartPhase::Efficient);
  e.onMeasurement(32 * MIN, 18.0f, RXPS_SMART_SHORT_BELOW_CLEAN);
  e.onMeasurement(32 * MIN, 99.0f, RXPS_SMART_SHORT_BELOW_CLEAN);
  EXPECT_EQ(e.phase(), RxPsSmartPhase::Efficient);
  EXPECT_EQ(e.ambiguousCount(), 1u);
  EXPECT_EQ(e.invalidCount(), 1u);
}

TEST(SmartRxps, SurvivesMillisWrap) {
  RxPsSmartEngine e;
  const uint32_t start = 0xFFFFFFFFUL - 10UL * MIN;
  e.start(start);
  feedLong(e, start, 20);
  e.tick(start + 31 * MIN);   // wraps past zero
  EXPECT_EQ(e.phase(), RxPsSmartPhase::Efficient);
}

TEST(SmartRxps, RearmBackoffGrowsAndCaps) {
  EXPECT_EQ(rxPsRearmBackoffMs(0), 5 * MIN);
  EXPECT_EQ(rxPsRearmBackoffMs(1), 10 * MIN);
  EXPECT_EQ(rxPsRearmBackoffMs(3), 40 * MIN);
  EXPECT_EQ(rxPsRearmBackoffMs(4), 60 * MIN);
  EXPECT_EQ(rxPsRearmBackoffMs(9), 60 * MIN);
}

TEST(SmartRxps, BothProfilesExistAtUsPreset) {
  // The engine needs both geometries for every guarded level at SF7/BW62.5.
  for (uint8_t level = 1; level <= RX_POWERSAVING_GUARDED_LEVELS; level++) {
    uint32_t rx16, sl16, rx32, sl32;
    EXPECT_TRUE(calcRxPowerSavingLevel(level, 7, 62.5f, 16, &rx16, &sl16,
                                       RX_POWERSAVING_CAPTURE_COST_SYMBOLS_LR11X0, 2600));
    EXPECT_TRUE(calcRxPowerSavingLevel(level, 7, 62.5f, 32, &rx32, &sl32,
                                       RX_POWERSAVING_CAPTURE_COST_SYMBOLS_LR11X0, 2600));
    EXPECT_GT(sl32, sl16);
    // The 16-symbol profile can still tell a typical old sender (<= 14) apart;
    // the 32-symbol one only trusts much lower readings.
    const float b16 = rxPsSmartShortBelowForSleep((float)sl16 / SYM_US, 8.0f);
    const float b32 = rxPsSmartShortBelowForSleep((float)sl32 / SYM_US, 8.0f);
    EXPECT_GE(b16, 14.0f);
    EXPECT_LT(b32, b16);
  }
}

TEST(SmartRxps, DefaultsAreOn) {
  const RxPowerSavingConfig c;
  EXPECT_EQ(c.smart, 1);
  EXPECT_EQ(c.hold_s, RX_POWERSAVING_DEFAULT_HOLD_S);
}

TEST(SmartRxps, CliSetsSmartAndHold) {
  RxPowerSavingConfig c;
  c.enabled = 1;
  RecordingControl control;
  char reply[160];

  EXPECT_TRUE(RXPowerSavingCLI::set("smart off", 7, 62.5f, &c, &control, reply, sizeof(reply)));
  EXPECT_EQ(c.smart, 0);
  EXPECT_FALSE(control.last_smart);

  EXPECT_TRUE(RXPowerSavingCLI::set("hold 30", 7, 62.5f, &c, &control, reply, sizeof(reply)));
  EXPECT_EQ(c.hold_s, 30);
  EXPECT_EQ(control.last_hold, 30);

  EXPECT_FALSE(RXPowerSavingCLI::set("hold 500", 7, 62.5f, &c, &control, reply, sizeof(reply)));
  EXPECT_EQ(c.hold_s, 30);
  EXPECT_FALSE(RXPowerSavingCLI::set("smart maybe", 7, 62.5f, &c, &control, reply, sizeof(reply)));

  EXPECT_TRUE(RXPowerSavingCLI::set("level 8", 7, 62.5f, &c, &control, reply, sizeof(reply)));
  EXPECT_EQ(control.last_level, 8);
  EXPECT_TRUE(control.last_enabled);
}
