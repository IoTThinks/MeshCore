#pragma once

#include <Arduino.h>


// ── Limits ────────────────────────────────────────────────────────────────────
#define MAX_SCRIPT_RULES          8
#define SCRIPT_MIN_INTERVAL_SECS  1800   // 30 min absolute minimum
#define SCRIPT_RULES_FILE         "/report_rules"
#define SCRIPT_ENGINE_VERSION     1      // bump on struct changes

// ── Trigger types ─────────────────────────────────────────────────────────────
enum ScriptTriggerType : uint8_t {
  TRIGGER_BAT_BELOW   = 0,   // bat<XXXX   (mV)
  TRIGGER_BAT_ABOVE   = 1,   // bat>XXXX   (mV)
  TRIGGER_TEMP_BELOW  = 2,   // temp<XX    (°C, int16)
  TRIGGER_TEMP_ABOVE  = 3,   // temp>XX    (°C, int16)
  TRIGGER_NOISE_ABOVE = 4,   // noise>XX   (dBm, int16 — typically negative)
  TRIGGER_NOISE_BELOW = 5,   // noise<XX   (dBm, int16 — typically negative)
  TRIGGER_PERIODIC    = 6,   // report:bat / report:temp / report:noise
};

// What is reported when trigger is TRIGGER_PERIODIC
enum ScriptReportVar : uint8_t {
  REPORT_BAT   = 0,
  REPORT_TEMP  = 1,
  REPORT_NOISE = 2,
};

// ── Single rule ───────────────────────────────────────────────────────────────
struct ScriptRule {
  bool              enabled;
  ScriptTriggerType trigger;
  int16_t           trigger_value;      // threshold: mV, °C or dBm
  ScriptReportVar   report_var;         // only used for TRIGGER_PERIODIC
  uint32_t          interval_secs;      // desired interval (>= SCRIPT_MIN_INTERVAL_SECS)
  bool              chan_is_hash;       // false = private/key channel, true = hash channel
  char              chan_name[36];      // channel key (private, 32 hex chars) or channel name (hash)
  char              message[64];        // template with {value} and {unit} placeholders
  char              scope_name[32];     // optional region scope name (empty = use default_scope)
  int8_t            at_hour;            // -1 = not set, 0-23 = fire at this hour (UTC)
  int8_t            at_minute;          // 0-59, only used when at_hour != -1
  uint32_t          last_fired;         // RTC timestamp of last send (0 = never)
};

// ── Persistent file header ────────────────────────────────────────────────────
struct ScriptRulesFile {
  uint8_t    version;
  uint8_t    count;
  ScriptRule rules[MAX_SCRIPT_RULES];
};