#include "RXPowerSavingCLI.h"

#include <Utils.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>

// Suffix printed after the level so an unguarded profile is never mistaken for
// an ordinary one, in both the `set` echo and the `get` report.
static const char* rxPowerSavingLevelTag(uint8_t level) {
  if (isRxPowerSavingRiskyWorkingMaxLevel(level)) return "(riskyWorkingMax)";
  if (isRxPowerSavingOverdriveLevel(level)) return "(overdrive)";
  return "";
}

static bool parseRxPowerSavingUint32(const char* value, uint32_t* parsed) {
  if (parsed == nullptr || !isRxPowerSavingNumeric(value)) return false;

  uint32_t result = 0;
  while (*value) {
    uint32_t digit = (uint32_t)(*value++ - '0');
    if (result > (UINT32_MAX - digit) / 10U) return false;
    result = result * 10U + digit;
  }
  *parsed = result;
  return true;
}

bool RXPowerSavingCLI::set(const char* value, uint8_t sf, float bw,
                           RxPowerSavingConfig* config, RxPowerSavingControl* control,
                           char* reply, size_t reply_size) {
  if (value == nullptr || config == nullptr || reply == nullptr || reply_size == 0) return false;

  RxPowerSavingConfig proposed = *config;
  uint8_t level = 0;
  uint8_t preamble = rxPowerSavingPreambleForSF(sf);
  bool level_requested = false;
  bool preamble_overridden = false;

  ensureRxPowerSavingDefaults(&proposed.rx_us, &proposed.sleep_us);

  if (strcmp(value, "off") == 0) {
    proposed.enabled = 0;
  } else if (strcmp(value, "on") == 0 || strcmp(value, "conservative") == 0) {
    proposed.enabled = 1;
    level = RX_POWERSAVING_CONSERVATIVE_LEVEL;
    preamble = RX_POWERSAVING_PROFILE_PREAMBLE;
    level_requested = true;
    preamble_overridden = true;
  } else if (strcmp(value, "max") == 0) {
    // Top of the guarded scale. Like every named preset it assumes the worst
    // case a mixed network can present - a sender using a 16-symbol preamble -
    // so at SF<=8 it is markedly less economical than `level 10`, which follows
    // the SF and picks the 32-symbol profile. `max preamble 32` gets that back.
    proposed.enabled = 1;
    level = RX_POWERSAVING_MAX_LEVEL;
    preamble = RX_POWERSAVING_PROFILE_PREAMBLE;
    level_requested = true;
    preamble_overridden = true;
  } else if (strcmp(value, "overdrive") == 0) {
    // One step past the guarded scale: measured lossless, outside the
    // datasheet's timer condition. Same worst-case preamble as the presets above.
    proposed.enabled = 1;
    level = RX_POWERSAVING_OVERDRIVE_LEVEL;
    preamble = RX_POWERSAVING_PROFILE_PREAMBLE;
    level_requested = true;
    preamble_overridden = true;
  } else if (strcmp(value, "riskyWorkingMax") == 0) {
    // This one is not lossless, so the risk is explicit in its name. Same
    // worst-case preamble as the presets above.
    proposed.enabled = 1;
    level = RX_POWERSAVING_RISKY_WORKING_MAX_LEVEL;
    preamble = RX_POWERSAVING_PROFILE_PREAMBLE;
    level_requested = true;
    preamble_overridden = true;
  } else if (strcmp(value, "balanced") == 0) {
    proposed.enabled = 1;
    level = RX_POWERSAVING_BALANCED_LEVEL;
    preamble = RX_POWERSAVING_PROFILE_PREAMBLE;
    level_requested = true;
    preamble_overridden = true;
  } else {
    char input[128];
    if (strlen(value) >= sizeof(input)) {
      snprintf(reply, reply_size,
               "ERROR: use off|on|conservative|balanced|max|overdrive|riskyWorkingMax|level <1-10>|<rx_us> <sleep_us>");
      return false;
    }
    strcpy(input, value);

    const char* parts[4];
    int count = mesh::Utils::parseTextParts(input, parts, 4, ' ');
    uint32_t first = 0;
    uint32_t second = 0;
    if (count == 1 && parseRxPowerSavingUint32(parts[0], &first)) {
      if (first < 1 || first > RX_POWERSAVING_RISKY_WORKING_MAX_LEVEL) {
        snprintf(reply, reply_size, "ERROR: level range is 1-%u (or max|overdrive|riskyWorkingMax); preamble is 16 or 32",
                 RX_POWERSAVING_RISKY_WORKING_MAX_LEVEL);
        return false;
      }
      level = (uint8_t)first;
      level_requested = true;
      proposed.enabled = 1;
    } else if (count >= 2 && strcmp(parts[1], "preamble") == 0 &&
               (strcmp(parts[0], "max") == 0 || strcmp(parts[0], "overdrive") == 0 ||
                strcmp(parts[0], "riskyWorkingMax") == 0)) {
      if (count != 3 || !parseRxPowerSavingUint32(parts[2], &first) ||
          (first != 16 && first != 32)) {
        snprintf(reply, reply_size, "ERROR: use %s preamble <16|32>", parts[0]);
        return false;
      }
      level = strcmp(parts[0], "overdrive") == 0 ? RX_POWERSAVING_OVERDRIVE_LEVEL
            : strcmp(parts[0], "max") == 0       ? RX_POWERSAVING_MAX_LEVEL
                                                 : RX_POWERSAVING_RISKY_WORKING_MAX_LEVEL;
      preamble = (uint8_t)first;
      level_requested = true;
      preamble_overridden = true;
      proposed.enabled = 1;
    } else if (count == 2 && strcmp(parts[0], "level") == 0 &&
               parseRxPowerSavingUint32(parts[1], &first)) {
      if (first < 1 || first > RX_POWERSAVING_RISKY_WORKING_MAX_LEVEL) {
        snprintf(reply, reply_size, "ERROR: level range is 1-%u (or max|overdrive|riskyWorkingMax); preamble is 16 or 32",
                 RX_POWERSAVING_RISKY_WORKING_MAX_LEVEL);
        return false;
      }
      level = (uint8_t)first;
      level_requested = true;
      proposed.enabled = 1;
    } else if (count == 4 && strcmp(parts[0], "level") == 0 &&
               parseRxPowerSavingUint32(parts[1], &first) &&
               strcmp(parts[2], "preamble") == 0 &&
               parseRxPowerSavingUint32(parts[3], &second)) {
      if (first < 1 || first > RX_POWERSAVING_RISKY_WORKING_MAX_LEVEL ||
          (second != 16 && second != 32)) {
        snprintf(reply, reply_size, "ERROR: level range is 1-%u (or max|overdrive|riskyWorkingMax); preamble is 16 or 32",
                 RX_POWERSAVING_RISKY_WORKING_MAX_LEVEL);
        return false;
      }
      level = (uint8_t)first;
      preamble = (uint8_t)second;
      level_requested = true;
      preamble_overridden = true;
      proposed.enabled = 1;
    } else if (count == 2 && parseRxPowerSavingUint32(parts[0], &first) &&
               parseRxPowerSavingUint32(parts[1], &second)) {
      proposed.rx_us = first;
      proposed.sleep_us = second;
      proposed.enabled = 1;
    } else {
      snprintf(reply, reply_size,
               "ERROR: use off|on|conservative|balanced|max|overdrive|riskyWorkingMax|level <1-10>|<rx_us> <sleep_us>");
      return false;
    }
  }

  if (level_requested &&
      !calcRxPowerSavingLevel(level, sf, bw, preamble,
                              &proposed.rx_us, &proposed.sleep_us,
                              rxPowerSavingCaptureCost(control),
                              rxPowerSavingTransition(control))) {
    // Level and preamble were validated above, so the only way to get here is
    // that no sleep exists which both arms the hardware and still lets a
    // preamble of this length be caught - i.e. the symbol is too short for RXPS
    // at this SF/BW. Saying "level range is 1-10" would send the user hunting
    // for the wrong thing.
    snprintf(reply, reply_size, "ERROR: RXPS does not fit this SF/BW with preamble %lu",
             (unsigned long)preamble);
    return false;
  }
  if (!isValidRxPowerSavingPeriod(proposed.rx_us) ||
      !isValidRxPowerSavingPeriod(proposed.sleep_us)) {
    snprintf(reply, reply_size, "ERROR: range is %lu-%lu us",
             (unsigned long)RX_POWERSAVING_MIN_PERIOD_US,
             (unsigned long)RX_POWERSAVING_MAX_PERIOD_US);
    return false;
  }

  bool applied = control != nullptr
      ? control->setRxPowerSaving(proposed.enabled != 0, proposed.rx_us, proposed.sleep_us)
      : proposed.enabled == 0;
  if (!applied) {
    snprintf(reply, reply_size, "ERROR: RX powersaving unsupported");
    return false;
  }

  if (level_requested) {
    proposed.level = level;
    proposed.preamble = preamble_overridden ? preamble : 0;
  } else if (strcmp(value, "off") != 0) {
    proposed.level = 0;
    proposed.preamble = 0;
  }

  *config = proposed;
  snprintf(reply, reply_size, "OK - %s,level=%lu%s,preamble=%lu,rx=%lu,sleep=%lu",
           config->enabled ? "on" : "off", (unsigned long)config->level,
           rxPowerSavingLevelTag(config->level),
           (unsigned long)config->preamble, (unsigned long)config->rx_us,
           (unsigned long)config->sleep_us);
  return true;
}

void RXPowerSavingCLI::get(const RxPowerSavingConfig* config,
                           const RxPowerSavingControl* control,
                           uint8_t sf, float bw, char* reply, size_t reply_size) {
  if (config == nullptr || reply == nullptr || reply_size == 0) return;

  RxPowerSavingStatus status = control != nullptr
      ? control->getRxPowerSavingStatus()
      : RxPowerSavingStatus{};
  // Preamble symbols the node actually catches. Not simply the number the level
  // asked for: the sleep floor can shorten the sleep further, and at SF6 with
  // the 16-symbol profile that collapses the first few levels onto the same
  // point. Reporting the requested value there would be a lie.
  char margin[24];
  margin[0] = 0;
  if (config->level >= 1 && config->level <= RX_POWERSAVING_GUARDED_LEVELS &&
      sf >= 5 && sf <= 12 && bw > 0.0f) {
    const uint8_t preamble = config->preamble ? config->preamble : rxPowerSavingPreambleForSF(sf);
    const float symbol_us = (1000.0f * (float)(1UL << sf)) / bw;
    const float effective = (float)preamble - (float)config->sleep_us / symbol_us;
    // No %f on this platform's printf, and rounding the whole and fractional
    // parts separately overflows: 4.999 came out as "4.10".
    const int tenths = (int)(effective * 10.0f + (effective < 0.0f ? -0.5f : 0.5f));
    snprintf(margin, sizeof(margin), ",catch=%d.%d", tenths / 10,
             tenths < 0 ? -(tenths % 10) : tenths % 10);
  }

  // Overdrive has to be visible here, otherwise it reads as an ordinary level
  // and nobody remembers it runs outside the datasheet rule.
  int len = snprintf(reply, reply_size,
           "> desired=%s,effective=%s,supported=%s,level=%lu%s%s,preamble=%lu,rx=%lu,sleep=%lu,err=%d,fail=%lu",
           config->enabled ? "on" : "off", status.armed ? "armed" : "continuous",
           status.supported ? "yes" : "no", (unsigned long)config->level,
           rxPowerSavingLevelTag(config->level), margin,
           (unsigned long)config->preamble, (unsigned long)config->rx_us,
           (unsigned long)config->sleep_us, (int)status.last_error,
           (unsigned long)status.arm_failures);

  // Only report the periods the hardware really runs with when a driver had to
  // clamp them - printing them always would not fit the 160 byte reply buffer.
  if (len > 0 && (size_t)len < reply_size && status.armed &&
      (status.effective_rx_us != config->rx_us ||
       status.effective_sleep_us != config->sleep_us)) {
    snprintf(reply + len, reply_size - (size_t)len, ",erx=%lu,eslp=%lu",
             (unsigned long)status.effective_rx_us,
             (unsigned long)status.effective_sleep_us);
  }
}
