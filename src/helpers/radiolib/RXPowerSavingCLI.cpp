#include "RXPowerSavingCLI.h"

#include <Utils.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>

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
               "ERROR: use off|on|conservative|balanced|level <1-10>|<rx_us> <sleep_us>");
      return false;
    }
    strcpy(input, value);

    const char* parts[4];
    int count = mesh::Utils::parseTextParts(input, parts, 4, ' ');
    uint32_t first = 0;
    uint32_t second = 0;
    if (count == 1 && parseRxPowerSavingUint32(parts[0], &first)) {
      if (first < 1 || first > 10) {
        snprintf(reply, reply_size, "ERROR: level range is 1-10; preamble is 16 or 32");
        return false;
      }
      level = (uint8_t)first;
      level_requested = true;
      proposed.enabled = 1;
    } else if (count == 2 && strcmp(parts[0], "level") == 0 &&
               parseRxPowerSavingUint32(parts[1], &first)) {
      if (first < 1 || first > 10) {
        snprintf(reply, reply_size, "ERROR: level range is 1-10; preamble is 16 or 32");
        return false;
      }
      level = (uint8_t)first;
      level_requested = true;
      proposed.enabled = 1;
    } else if (count == 4 && strcmp(parts[0], "level") == 0 &&
               parseRxPowerSavingUint32(parts[1], &first) &&
               strcmp(parts[2], "preamble") == 0 &&
               parseRxPowerSavingUint32(parts[3], &second)) {
      if (first < 1 || first > 10 || (second != 16 && second != 32)) {
        snprintf(reply, reply_size, "ERROR: level range is 1-10; preamble is 16 or 32");
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
               "ERROR: use off|on|conservative|balanced|level <1-10>|<rx_us> <sleep_us>");
      return false;
    }
  }

  if (level_requested &&
      !calcRxPowerSavingLevel(level, sf, bw, preamble,
                              &proposed.rx_us, &proposed.sleep_us)) {
    snprintf(reply, reply_size, "ERROR: level range is 1-10; preamble is 16 or 32");
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
  snprintf(reply, reply_size, "OK - %s,level=%lu,preamble=%lu,rx=%lu,sleep=%lu",
           config->enabled ? "on" : "off", (unsigned long)config->level,
           (unsigned long)config->preamble, (unsigned long)config->rx_us,
           (unsigned long)config->sleep_us);
  return true;
}

void RXPowerSavingCLI::get(const RxPowerSavingConfig* config,
                           const RxPowerSavingControl* control,
                           char* reply, size_t reply_size) {
  if (config == nullptr || reply == nullptr || reply_size == 0) return;

  RxPowerSavingStatus status = control != nullptr
      ? control->getRxPowerSavingStatus()
      : RxPowerSavingStatus{};
  int len = snprintf(reply, reply_size,
           "> desired=%s,effective=%s,supported=%s,level=%lu,preamble=%lu,rx=%lu,sleep=%lu,err=%d,fail=%lu",
           config->enabled ? "on" : "off", status.armed ? "armed" : "continuous",
           status.supported ? "yes" : "no", (unsigned long)config->level,
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

void RXPowerSavingCLI::setRfRxDisabled(const char* value, RxPowerSavingControl* control,
                                       char* reply, size_t reply_size) {
  if (value == nullptr || reply == nullptr || reply_size == 0) return;

  bool disabled;
  if (strcmp(value, "on") == 0) {
    disabled = true;
  } else if (strcmp(value, "off") == 0) {
    disabled = false;
  } else {
    snprintf(reply, reply_size, "Error: state must be on or off");
    return;
  }

  if (control == nullptr || !control->setRxPowerSavingRfRxDisabled(disabled)) {
    snprintf(reply, reply_size, "Error: unsupported");
  } else {
    snprintf(reply, reply_size, "OK - radio.rxps.rfrx_disabled %s", disabled ? "on" : "off");
  }
}

void RXPowerSavingCLI::getRfRxDisabled(const RxPowerSavingControl* control,
                                       char* reply, size_t reply_size) {
  if (reply == nullptr || reply_size == 0) return;
  if (control == nullptr || !control->supportsRxPowerSavingRfRxDisable()) {
    snprintf(reply, reply_size, "Error: unsupported");
  } else {
    snprintf(reply, reply_size, "> %s", control->isRxPowerSavingRfRxDisabled() ? "on" : "off");
  }
}
