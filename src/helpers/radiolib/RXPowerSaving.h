#pragma once

#include <stdint.h>

static constexpr uint32_t RX_POWERSAVING_DEFAULT_RX_US = 65625UL;
static constexpr uint32_t RX_POWERSAVING_DEFAULT_SLEEP_US = 60000UL;
static constexpr uint32_t RX_POWERSAVING_MIN_PERIOD_US = 1000UL;
static constexpr uint32_t RX_POWERSAVING_MAX_PERIOD_US = 30000000UL;

// The level scale is a catch dial, in symbols of the sender's preamble.
//
// Level N means: "at least this many symbols of the sender's preamble land
// inside an open RX window." That is the quantity the radio actually needs -
// a preamble is latched once enough of it has been heard - so the level states
// the requirement directly instead of stating a distance from an edge.
//
// The arithmetic follows from that in one step. Worst case, a preamble starts
// the instant an RX window closes, so what survives into the next window is
// `preamble - sleep`. Requiring that to be at least the level's catch gives
//
//     sleep = preamble - catch
//
// and the chip's capture cost never enters the geometry. It becomes a
// validation instead: a level is usable when its catch is at least the cost.
// The floor of the scale is 8 symbols precisely because that is what the
// slower of the two families needs (LR11x0 8, SX126x 6), so **every level
// means the same geometry on both radios** - which the previous margin-based
// scale could not do, because the cost sat inside its formula.
//
// The two profiles get their own ladders. On a 16-symbol preamble there are
// only 8 symbols to spend between the floor and the whole preamble, so the
// steps are single symbols; on a 32-symbol preamble there are 24, so the low
// levels can take much larger strides where reception is guaranteed anyway.
static constexpr uint8_t RX_POWERSAVING_GUARDED_LEVELS = 8;
static constexpr float RX_POWERSAVING_LEVEL_CATCH_P16[RX_POWERSAVING_GUARDED_LEVELS] = {
  15.0f, 14.0f, 13.0f, 12.0f, 11.0f, 10.0f, 9.0f, 8.0f
};
static constexpr float RX_POWERSAVING_LEVEL_CATCH_P32[RX_POWERSAVING_GUARDED_LEVELS] = {
  24.0f, 20.0f, 16.0f, 14.0f, 12.0f, 10.0f, 9.0f, 8.0f
};

// Smallest catch any guarded level asks for, and the reason it is 8: the
// LR11x0 needs 8 symbols to latch where the SX126x needs 6.
static constexpr float RX_POWERSAVING_MIN_CATCH_SYMBOLS = 8.0f;

// On top of the ladder, every guarded level sleeps this much less than the
// arithmetic demands - so it catches 0.2 symbols more than its level promises.
// The reason is not capture safety: it is that specific register pairs the
// calculation lands on are defective (preamble latched, header never
// validated) in the same way isolated rxPeriod ticks 320 and 640 are. Nudging
// every generated geometry off those points cost 0.05 mA and took the worst
// measured cell from 3.33% packet loss to 0.11% across the whole ladder.
// Levels 9 and 10 are exempt - their geometries are pinned to bench
// measurements and shifting them would invalidate that data.
static constexpr float RX_POWERSAVING_MARGIN_PAD_SYMBOLS = 0.2f;

// Past the guarded scale sit two settings that break the datasheet's
// SetRxDutyCycle timer condition on purpose. Overdrive is lossless and 4-12
// percentage points cheaper in duty cycle; `riskyWorkingMax` is the measured
// working edge and drops ~2% of packets by design.
static constexpr uint8_t RX_POWERSAVING_OVERDRIVE_LEVEL = 9;
static constexpr uint8_t RX_POWERSAVING_RISKY_WORKING_MAX_LEVEL = 10;
static constexpr float RX_POWERSAVING_RISKY_WORKING_MAX_VIRTUAL_P32 = 11.0f;
static constexpr float RX_POWERSAVING_RISKY_WORKING_MAX_VIRTUAL_P16 = 10.25f;

// Top of the guarded scale: catches the ladder floor of 8 symbols (8.2 with
// the pad), the cheapest setting that still meets the datasheet condition and
// still works on both radio families. The CLI calls it `max`.
static constexpr uint8_t RX_POWERSAVING_MAX_LEVEL = RX_POWERSAVING_GUARDED_LEVELS;

static constexpr uint8_t RX_POWERSAVING_CONSERVATIVE_LEVEL = 3;   // catches 13 (P16) / 16 (P32)
static constexpr uint8_t RX_POWERSAVING_BALANCED_LEVEL = 6;       // catches 10 (P16) / 10 (P32)
static constexpr uint8_t RX_POWERSAVING_PROFILE_PREAMBLE = 16;
static constexpr uint8_t RX_POWERSAVING_MAX_CONSEC_ARM_FAILURES = 3;

// Symbols of a sender's preamble consumed before a packet can be latched:
// ~2 to latch the preamble plus ~2 lost to RX-window startup, measured on
// SX1262 at SF8/BW62.5. Capture needs sleep <= sender_preamble - this.
// Note this is well below the 8 symbols RadioLib quotes (SX126x.h) - that
// figure is the datasheet's margin for *reliable* latching, not the floor.
//
// Chip dependent, so it belongs with the driver rather than with each board:
// a wrapper overrides rxPowerSavingCaptureCostSymbols() and every variant using
// that radio picks it up. LR1110 needs about one symbol more - on the bench it
// held 300/300 at a margin of 7.78 symbols and started dropping packets at
// 6.89, for both profiles, where SX1262 is clean down to 6.
static constexpr float RX_POWERSAVING_CAPTURE_COST_SYMBOLS = 6.0f;
static constexpr float RX_POWERSAVING_CAPTURE_COST_SYMBOLS_LR11X0 = 8.0f;

// TCXO startup delay handed to SetDIO3AsTCXOCtrl (SX126x DS 13.3.6) /
// SetTcxoMode (LR11x0 UM 6.3.2). RadioLib defaults it to 5000 us and nothing
// overrode it, so every board paid 5 ms on every duty-cycle wake whatever
// crystal was fitted. Measured lowest working value on five modules: T096 400,
// Tracker V2 300, Waveshare 150, ThinkNode M3 200, T1000-E 200 us - so 1600
// keeps 4x margin over the worst of them.
//
// One knob for the whole tree, in `arduino_base`, because it is a property of
// the parts MeshCore is built with rather than of any one board. A variant that
// needs its own value defines it in its own build_flags and the later -D wins.
#ifndef MC_TCXO_DELAY_US
  #define MC_TCXO_DELAY_US 1600
#endif

// Sleep -> RX transition: RadioLib subtracts tcxoDelay + 1000 us from the
// requested sleep before writing the register, and the hardware spends that
// long saving context, restarting the XTAL and locking the PLL. Everything
// below derives from it, so a board that knows its real TCXO delay overrides
// one method and both the sleep floor and the timer guard follow.
static constexpr uint32_t RX_POWERSAVING_TRANSITION_US = MC_TCXO_DELAY_US + 1000;

// Slack the timer guard leaves on top of the datasheet condition, in symbols.
//
// Closing the condition exactly is not enough. The calculator lands on register
// pairs that satisfy it by a hair and still lose packets: rx=2476/sleep=2455
// ticks clears it by 22 us at 0.12 symbol and drops 2-6% on every SX1262 tried,
// while moving any one component clears the fault. It is the same family as the
// isolated bad ticks 320 and 640 - the condition does not predict which pairs
// misbehave, so the answer is to stop generating geometries that sit on its
// edge. One whole symbol of slack is what the bench evidence supports.
#ifndef MC_RXPS_TIMER_SLACK_SYMBOLS
  #define MC_RXPS_TIMER_SLACK_SYMBOLS 1.0f
#endif
static constexpr float RX_POWERSAVING_TIMER_SLACK_SYMBOLS = MC_RXPS_TIMER_SLACK_SYMBOLS;

// Both radio families program their duty cycle in these units, and truncate the
// microsecond value on the way in. Everything the timer guard compares has to
// be expressed in whole ticks or it comes out short by a fraction of one.
static constexpr float RX_POWERSAVING_TICK_US = 15.625f;

// Margin above the transition before a duty cycle actually works; see below.
static constexpr uint32_t RX_POWERSAVING_MIN_SLEEP_MARGIN_US = 250;

// Symbols between the end of the sender's preamble and the end of the header:
// the LoRa sync word plus the 8-symbol explicit header. SF5/SF6 use a longer
// sync word than SF7 and above. Used by the SetRxDutyCycle timer guard.
static constexpr float RX_POWERSAVING_SYNC_SYMBOLS_LOW_SF = 6.25f;
static constexpr float RX_POWERSAVING_SYNC_SYMBOLS = 4.25f;
static constexpr float RX_POWERSAVING_HEADER_SYMBOLS = 8.0f;

// Shortest sleep that actually produces a working duty cycle.
//
// RadioLib subtracts tcxoDelay + 1000 us from the requested sleep before
// writing the register. At or below that the register underflows and arming
// fails with RADIOLIB_ERR_INVALID_SLEEP_PERIOD (-708), after which the wrapper
// falls back to continuous RX while the config still says power saving is on.
// Measured on SX1262: the P16 profile fails to arm on levels 1-5 at SF6 and on
// levels 1-2 at SF7, exactly where that bound predicts.
//
// But arming successfully is NOT the boundary. Just above it the SX1262 accepts
// the command, returns ERR_NONE, and then detects **zero** preambles - a
// silently deaf receiver, which is worse than the honest -708. Measured twice,
// on two different SF and RX windows: sleep 6050-6100 us is dead, 6200 us and
// everything above it is lossless (40/40 at every value up to 21617 us). The
// chip needs roughly 200 us of *programmed* sleep, i.e. ~12 register ticks, so
// this leaves 250 us of margin on top of the transition time.
//
// The default assumes the 5000 us TCXO delay RadioLib installs; a wrapper that
// can read its radio overrides this with the board's real value.
static constexpr uint32_t RX_POWERSAVING_MIN_SLEEP_US =
    RX_POWERSAVING_TRANSITION_US + RX_POWERSAVING_MIN_SLEEP_MARGIN_US;

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
  virtual bool isRxPowerSavingCalibrationActive() const { return false; }

  // Symbols of the sender's preamble consumed before a packet can be latched.
  // Overridden per radio family; see RX_POWERSAVING_CAPTURE_COST_SYMBOLS.
  virtual float rxPowerSavingCaptureCostSymbols() const {
    return RX_POWERSAVING_CAPTURE_COST_SYMBOLS;
  }

  // Sleep -> RX transition of this radio, in microseconds. The sleep floor and
  // the SetRxDutyCycle timer guard are both derived from it, so a wrapper that
  // can read its own tcxoDelay only needs to override this one method.
  virtual uint32_t rxPowerSavingTransitionUs() const {
    return RX_POWERSAVING_TRANSITION_US;
  }
};

// Margin this level asks for, in symbols. Only meaningful on the guarded scale.
// Symbols of the sender's preamble this level undertakes to catch. The two
// profiles have separate ladders, so the profile has to be passed in; anything
// other than 16 or 32 is treated as the 16-symbol profile, matching the
// validation in calcRxPowerSavingLevel().
inline float rxPowerSavingLevelCatch(uint8_t level, uint8_t preamble) {
  if (level < 1 || level > RX_POWERSAVING_GUARDED_LEVELS) return 0.0f;
  return preamble == 32 ? RX_POWERSAVING_LEVEL_CATCH_P32[level - 1]
                        : RX_POWERSAVING_LEVEL_CATCH_P16[level - 1];
}

inline bool isRxPowerSavingOverdriveLevel(uint8_t level) {
  return level == RX_POWERSAVING_OVERDRIVE_LEVEL;
}

inline bool isRxPowerSavingRiskyWorkingMaxLevel(uint8_t level) {
  return level == RX_POWERSAVING_RISKY_WORKING_MAX_LEVEL;
}

// Both levels past the guarded scale run outside the datasheet's timer
// condition. Worth surfacing wherever a level is reported.
inline bool isRxPowerSavingUnguardedLevel(uint8_t level) {
  return level >= RX_POWERSAVING_OVERDRIVE_LEVEL;
}

bool isValidRxPowerSavingPeriod(uint32_t us);
uint8_t rxPowerSavingPreambleForSF(uint8_t sf);
bool isRxPowerSavingNumeric(const char* value);
// capture_cost_symbols defaults to the SX126x figure so callers without a radio
// handy still compile; anything holding a RxPowerSavingControl should pass
// rxPowerSavingCaptureCost(control) instead.
float rxPowerSavingCaptureCost(const RxPowerSavingControl* control);
uint32_t rxPowerSavingTransition(const RxPowerSavingControl* control);
bool calcRxPowerSavingLevel(uint8_t level, uint8_t sf, float bw, uint8_t preamble,
                            uint32_t* rx_us, uint32_t* sleep_us,
                            float capture_cost_symbols = RX_POWERSAVING_CAPTURE_COST_SYMBOLS,
                            uint32_t transition_us = RX_POWERSAVING_TRANSITION_US);
void ensureRxPowerSavingDefaults(uint32_t* rx_us, uint32_t* sleep_us);
bool recalcRxPowerSavingFromLevel(uint8_t level, uint8_t sf, float bw, uint8_t preamble,
                                  uint32_t* rx_us, uint32_t* sleep_us,
                                  float capture_cost_symbols = RX_POWERSAVING_CAPTURE_COST_SYMBOLS,
                                  uint32_t transition_us = RX_POWERSAVING_TRANSITION_US);
void normalizeRxPowerSavingConfig(RxPowerSavingConfig* config, uint8_t sf, float bw,
                                  float capture_cost_symbols = RX_POWERSAVING_CAPTURE_COST_SYMBOLS,
                                  uint32_t transition_us = RX_POWERSAVING_TRANSITION_US);
