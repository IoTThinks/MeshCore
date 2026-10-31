#include <gtest/gtest.h>

#include <limits>
#include "helpers/radiolib/SX1262RxPowerSaving.h"

namespace {
uint32_t toUs(uint32_t ticks) {
  return (uint64_t(ticks) * 125 + 7) / 8;
}

uint32_t toTicks(uint32_t us) {
  // The exact 32-bit conversion used by RadioLib, including its overflow risk.
  return (us * 8U) / 125U;
}

uint32_t observedTimer(uint32_t r, uint32_t s) {
  return (uint64_t(s) + 2 * (r % 65536) + 262144ULL * (r % 64)) % 16777216;
}

double requiredTicks(uint8_t sf, float bw) {
  const double symbols = 32 + (sf <= 6 ? 6.25 : 4.25) + 8 +
                         RX_POWERSAVING_TIMER_SLACK_SYMBOLS;
  return ceil(symbols * (1000.0 * (1U << sf) / bw) / 15.625);
}
}

TEST(SX1262RxPowerSaving, MatchesMeasuredAndPreviouslyModelledProfiles) {
  const struct { uint32_t r, s, chosen, reload; uint8_t sf; float bw; } cases[] = {
    {3200, 1354, 3201, 269900, 8, 62.5f},
    {3201, 1354, 3201, 269900, 8, 62.5f},
    {3157, 1354, 3201, 269900, 8, 62.5f},
    {2895, 6073, 2945, 274107, 8, 62.5f},
    {501, 548, 513, 263718, 5, 62.5f},
    {73792, 36295, 73793, 314953, 12, 41.7f},
    {98440, 48487, 98498, 638699, 12, 31.25f},
  };
  for (auto c : cases) {
    SCOPED_TRACE(c.r);
    uint32_t effective = 0;
    ASSERT_TRUE(adjustSX1262RxPowerSavingPeriod(toUs(c.r), toUs(c.s) + 2600,
                                               c.sf, c.bw, 2600, &effective));
    EXPECT_EQ(toTicks(effective), c.chosen);
    EXPECT_EQ(observedTimer(toTicks(effective), c.s), c.reload);
    EXPECT_GE(c.reload, requiredTicks(c.sf, c.bw));
  }
}

TEST(SX1262RxPowerSaving, CorrectsManualPeriodsAndUsesActualTcxoDelay) {
  for (uint32_t transition : {1000U, 2600U, 6000U}) {
    for (uint32_t r : {320U, 640U, 3200U}) {
      uint32_t effective = 0;
      ASSERT_TRUE(adjustSX1262RxPowerSavingPeriod(toUs(r), toUs(1354) + transition,
                                                 8, 62.5f, transition, &effective));
      EXPECT_EQ(toTicks(effective), r + 1);
    }
  }
  // With S=111041 and SF12/BW31.25, R=3201 is only two ticks above need.
  uint32_t effective = 0;
  ASSERT_TRUE(adjustSX1262RxPowerSavingPeriod(50000, toUs(111041) + 2600,
                                             12, 31.25f, 2600, &effective));
  EXPECT_EQ(toTicks(effective), 3201U);
  ASSERT_TRUE(adjustSX1262RxPowerSavingPeriod(50000, toUs(111041) + 2600,
                                             12, 31.25f, 6000, &effective));
  // A slower TCXO shortens programmed S and requires remainder 2.
  EXPECT_EQ(toTicks(effective), 3202U);
}

TEST(SX1262RxPowerSaving, HandlesAllResiduesAndRegisterBoundaries) {
  for (uint32_t base : {0U, 3200U, 65472U, 65536U, 131008U, 131072U, 0xFFFF80U}) {
    for (uint32_t offset = 0; offset < 64; ++offset) {
      const uint32_t r = base + offset;
      if (!r) continue;
      uint32_t effective = 0;
      ASSERT_TRUE(adjustSX1262RxPowerSavingPeriod(toUs(r), toUs(1354) + 2600,
                                                 8, 62.5f, 2600, &effective));
      const uint32_t chosen = toTicks(effective);
      EXPECT_GE(chosen, r);
      EXPECT_LE(chosen - r, 63U);
      EXPECT_EQ(chosen % 64, 1U);
      EXPECT_GE(observedTimer(chosen, 1354), requiredTicks(8, 62.5f));
      EXPECT_EQ(effective, toUs(chosen));
    }
  }
}

TEST(SX1262RxPowerSaving, EvaluatesReloadAfter24BitWrap) {
  uint32_t effective = 0;
  // R=1 reloads to four ticks after wrapping; R=2 leaves 262150 ticks.
  ASSERT_TRUE(adjustSX1262RxPowerSavingPeriod(toUs(1), toUs(16515074) + 2600,
                                             8, 62.5f, 2600, &effective));
  EXPECT_EQ(toTicks(effective), 2U);
  EXPECT_EQ(observedTimer(1, 16515074), 4U);
  EXPECT_EQ(observedTimer(2, 16515074), 262150U);
}

TEST(SX1262RxPowerSaving, RejectsWhenNoRemainderCanCoverTheDeadline) {
  // Need fits in 24 bits, but exceeds every candidate's modelled reload.
  ASSERT_LT(requiredTicks(12, 0.714f), 0xFFFFFFU);
  uint32_t effective = 123;
  EXPECT_FALSE(adjustSX1262RxPowerSavingPeriod(toUs(1), toUs(1354) + 2600,
                                              12, 0.714f, 2600, &effective));
  EXPECT_EQ(effective, 123U);
}

TEST(SX1262RxPowerSaving, RejectsInvalidInputsWithoutPublishingAPeriod) {
  const float nan = std::numeric_limits<float>::quiet_NaN();
  const float inf = std::numeric_limits<float>::infinity();
  const struct { uint32_t rx, sleep, transition; uint8_t sf; float bw; } cases[] = {
    {0, 60000, 2600, 8, 62.5f}, {15, 60000, 2600, 8, 62.5f},
    {50000, 2599, 2600, 8, 62.5f}, {50000, 2600, 2600, 8, 62.5f},
    {50000, 2615, 2600, 8, 62.5f},
    {UINT32_MAX, 60000, 2600, 8, 62.5f}, {50000, UINT32_MAX, 2600, 8, 62.5f},
    {toUs(0xFFFFFF), 60000, 2600, 8, 62.5f},
    {toUs(0x1000000), 60000, 2600, 8, 62.5f},
    {50000, toUs(0x1000000) + 2600, 2600, 8, 62.5f},
    {50000, 60000, UINT32_MAX, 8, 62.5f},
    {50000, 60000, 2600, 4, 62.5f}, {50000, 60000, 2600, 32, 62.5f},
    {50000, 60000, 2600, 8, 0}, {50000, 60000, 2600, 8, -1},
    {50000, 60000, 2600, 8, nan}, {50000, 60000, 2600, 8, inf},
    {50000, 60000, 2600, 12, 0.001f},
  };
  for (auto c : cases) {
    uint32_t effective = 123;
    EXPECT_FALSE(adjustSX1262RxPowerSavingPeriod(c.rx, c.sleep, c.sf, c.bw,
                                                c.transition, &effective));
    EXPECT_EQ(effective, 123U);
  }
  EXPECT_FALSE(adjustSX1262RxPowerSavingPeriod(50000, 60000, 8, 62.5f, 2600, nullptr));
}

TEST(SX1262RxPowerSaving, CoversP32AcrossTheProfileGrid) {
  uint32_t valid = 0, remainder_two = 0;
  for (float bw : {31.25f, 41.7f, 62.5f, 125.0f, 250.0f, 500.0f}) {
    for (uint8_t sf = 5; sf <= 12; ++sf) {
      for (uint8_t p : {16, 32}) {
        for (uint8_t level = 1; level <= 10; ++level) {
          uint32_t rx, sleep, effective;
          if (!calcRxPowerSavingLevel(level, sf, bw, p, &rx, &sleep,
                                     RX_POWERSAVING_CAPTURE_COST_SYMBOLS, 2600)) continue;
          ASSERT_TRUE(adjustSX1262RxPowerSavingPeriod(rx, sleep, sf, bw, 2600, &effective));
          const uint32_t chosen = toTicks(effective);
          EXPECT_GE(chosen, toTicks(rx));
          EXPECT_GE(observedTimer(chosen, toTicks(sleep - 2600)), requiredTicks(sf, bw));
          EXPECT_LE(chosen % 64, 2U);
          EXPECT_NE(chosen % 64, 0U);
          remainder_two += chosen % 64 == 2;
          valid++;
        }
      }
    }
  }
  EXPECT_EQ(valid, 890U);
  EXPECT_EQ(remainder_two, 10U);
}

TEST(SX1262RxPowerSaving, CoversNarrowBandwidthsAndManualPeriodLimits) {
  for (float bw : {7.8f, 10.4f, 15.6f, 20.8f, 31.25f, 41.7f}) {
    for (uint8_t sf = 5; sf <= 12; ++sf) {
      for (uint32_t rx : {1000U, 10000U, 50000U, 1000000U, 30000000U}) {
        for (uint32_t sleep : {2850U, 60000U, 1000000U, 30000000U}) {
          uint32_t effective;
          ASSERT_TRUE(adjustSX1262RxPowerSavingPeriod(rx, sleep, sf, bw, 2600, &effective));
          EXPECT_GE(toTicks(effective), toTicks(rx));
          EXPECT_GE(observedTimer(toTicks(effective), toTicks(sleep - 2600)), requiredTicks(sf, bw));
          EXPECT_EQ(effective, toUs(toTicks(effective)));
        }
      }
    }
  }
}
