#pragma once

// SmartRXPS: lets RX power saving use the cheaper 32-symbol profile only while
// every sender in range is known to use a 32-symbol preamble, and keeps the
// fully compatible 16-symbol profile otherwise.
//
// Why: the 32-symbol profile sleeps longer than a 16-symbol preamble lasts, so
// a node on MeshCore < 1.16 (or PowerSaving < 16) is missed outright. Nothing in
// a MeshCore packet says which firmware sent it, but the preamble itself does:
// the radio raises PREAMBLE_DETECTED part-way through it and RX_DONE at the end
// of the payload. Subtracting the known header+payload airtime from that gap
// leaves the part of the sender's preamble still on air after detection.
//
//   remaining = (t_rx_done - t_preamble_detected) - airtime(sync+header+payload)
//
// A 16-symbol sender can never leave more than 16 symbols. A 32-symbol sender
// heard while listening continuously, or while duty cycling on the 16-symbol
// profile (sleep < 8 symbols), always leaves more than 20. Those measurements
// are clean. On the 32-symbol profile a long sender may be detected late, so
// only a value below the geometric floor of a long sender proves a short one.
//
// The engine below is pure logic (no Arduino calls) so it is unit tested
// natively. RadioLibWrapper feeds it measurements and applies its decisions.

#include <stdint.h>

// Clean measurement: anything under this is a 16-symbol sender.
static constexpr float RXPS_SMART_SHORT_BELOW_CLEAN = 17.0f;
// Any measurement at or above this is a 32-symbol sender, in every mode.
static constexpr float RXPS_SMART_LONG_FROM = 20.0f;
// Sanity window; outside it the timing was disturbed (missed edge, ISR delay).
static constexpr float RXPS_SMART_MAX_REMAINING = 40.0f;
// Extra symbols allowed for ISR/wake latency when bounding a long sender.
static constexpr float RXPS_SMART_TIMING_SLACK = 1.5f;

enum class RxPsPreambleClass : uint8_t { Invalid = 0, Short, Long, Ambiguous };
enum class RxPsSmartPhase : uint8_t { Off = 0, Safe, Efficient, Audit };

inline RxPsPreambleClass classifyRxPsPreamble(float remaining_symbols, float short_below) {
  if (!(remaining_symbols > -1.0f) || remaining_symbols > RXPS_SMART_MAX_REMAINING) {
    return RxPsPreambleClass::Invalid;
  }
  if (remaining_symbols >= RXPS_SMART_LONG_FROM) return RxPsPreambleClass::Long;
  if (remaining_symbols < short_below) return RxPsPreambleClass::Short;
  return RxPsPreambleClass::Ambiguous;
}

// Lower bound of what a 32-symbol sender can leave when we sleep `sleep_symbols`
// and the radio needs `capture_cost_symbols` to latch. Below it the sender must
// have been short. Never above the clean threshold.
inline float rxPsSmartShortBelowForSleep(float sleep_symbols, float capture_cost_symbols) {
  float bound = 32.0f - sleep_symbols - capture_cost_symbols - RXPS_SMART_TIMING_SLACK;
  if (bound > RXPS_SMART_SHORT_BELOW_CLEAN) bound = RXPS_SMART_SHORT_BELOW_CLEAN;
  if (bound < 0.0f) bound = 0.0f;
  return bound;
}

// Remaining preamble symbols from the two IRQ timestamps.
// post_preamble_us = airtime(len) with our preamble minus our preamble symbols,
// i.e. sync word + header + payload, identical for every sender on the preset.
inline float rxPsSmartRemainingSymbols(uint32_t elapsed_us, float post_preamble_us,
                                       float symbol_us) {
  if (!(symbol_us > 0.0f)) return -100.0f;
  return ((float)elapsed_us - post_preamble_us) / symbol_us;
}

struct RxPsSmartParams {
  uint32_t observe_ms = 30UL * 60UL * 1000UL;     // min time on the safe profile
  uint32_t clear_ms = 6UL * 60UL * 60UL * 1000UL; // no short sender for this long
  uint16_t min_long = 10;                         // long senders seen since last short
  uint32_t efficient_ms = 50UL * 60UL * 1000UL;   // run on the 32 profile this long
  uint32_t audit_ms = 10UL * 60UL * 1000UL;       // then re-check on the 16 profile
};

class RxPsSmartEngine {
  RxPsSmartParams _p;
  RxPsSmartPhase _phase = RxPsSmartPhase::Off;
  uint32_t _phase_since = 0;
  uint32_t _last_short = 0;
  bool _short_seen = false;
  uint16_t _long_since_short = 0;
  uint32_t _n_short = 0, _n_long = 0, _n_ambiguous = 0, _n_invalid = 0;
  float _last_remaining = 0.0f;

  void enter(RxPsSmartPhase phase, uint32_t now) {
    _phase = phase;
    _phase_since = now;
  }

public:
  RxPsSmartEngine() {}
  explicit RxPsSmartEngine(const RxPsSmartParams& p) : _p(p) {}

  void setParams(const RxPsSmartParams& p) { _p = p; }
  const RxPsSmartParams& params() const { return _p; }

  void start(uint32_t now) {
    if (_phase == RxPsSmartPhase::Off) enter(RxPsSmartPhase::Safe, now);
  }
  void stop() { _phase = RxPsSmartPhase::Off; }

  RxPsSmartPhase phase() const { return _phase; }
  bool active() const { return _phase != RxPsSmartPhase::Off; }
  bool wantsLongProfile() const { return _phase == RxPsSmartPhase::Efficient; }
  uint32_t phaseSince() const { return _phase_since; }
  bool shortSeen() const { return _short_seen; }
  uint32_t lastShort() const { return _last_short; }
  uint16_t longSinceShort() const { return _long_since_short; }
  uint32_t shortCount() const { return _n_short; }
  uint32_t longCount() const { return _n_long; }
  uint32_t ambiguousCount() const { return _n_ambiguous; }
  uint32_t invalidCount() const { return _n_invalid; }
  float lastRemaining() const { return _last_remaining; }

  RxPsPreambleClass onMeasurement(uint32_t now, float remaining_symbols, float short_below) {
    const RxPsPreambleClass c = classifyRxPsPreamble(remaining_symbols, short_below);
    if (c != RxPsPreambleClass::Invalid) _last_remaining = remaining_symbols;
    switch (c) {
      case RxPsPreambleClass::Short:
        _n_short++;
        _short_seen = true;
        _last_short = now;
        _long_since_short = 0;
        // An old-firmware node is in range: back to the profile that hears it.
        if (_phase != RxPsSmartPhase::Off) enter(RxPsSmartPhase::Safe, now);
        break;
      case RxPsPreambleClass::Long:
        _n_long++;
        if (_long_since_short < 0xFFFF) _long_since_short++;
        break;
      case RxPsPreambleClass::Ambiguous:
        _n_ambiguous++;
        break;
      default:
        _n_invalid++;
        break;
    }
    return c;
  }

  // Returns true when the phase changed.
  bool tick(uint32_t now) {
    const RxPsSmartPhase before = _phase;
    const uint32_t in_phase = now - _phase_since;
    switch (_phase) {
      case RxPsSmartPhase::Safe: {
        const bool quiet = !_short_seen || (uint32_t)(now - _last_short) >= _p.clear_ms;
        if (in_phase >= _p.observe_ms && quiet && _long_since_short >= _p.min_long) {
          enter(RxPsSmartPhase::Efficient, now);
        }
        break;
      }
      case RxPsSmartPhase::Efficient:
        if (in_phase >= _p.efficient_ms) enter(RxPsSmartPhase::Audit, now);
        break;
      case RxPsSmartPhase::Audit:
        if (in_phase >= _p.audit_ms) enter(RxPsSmartPhase::Efficient, now);
        break;
      default:
        break;
    }
    return _phase != before;
  }
};

inline const char* rxPsSmartPhaseName(RxPsSmartPhase p) {
  switch (p) {
    case RxPsSmartPhase::Safe: return "safe16";
    case RxPsSmartPhase::Efficient: return "eff32";
    case RxPsSmartPhase::Audit: return "audit16";
    default: return "off";
  }
}

// Backoff for re-arming RXPS after the stock three-strikes fallback, instead of
// staying on continuous RX until reboot: 5, 10, 20, 40, then every 60 minutes.
inline uint32_t rxPsRearmBackoffMs(uint8_t step) {
  uint32_t minutes = 5UL << (step > 4 ? 4 : step);
  if (minutes > 60) minutes = 60;
  return minutes * 60UL * 1000UL;
}
