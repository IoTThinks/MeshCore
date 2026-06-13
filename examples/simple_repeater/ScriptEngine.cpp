// ScriptEngine.cpp
//
// Implements load/save, CLI parser and evaluation of script rules.
// Included directly from MyMesh.cpp — call the four public methods:
//
//   _scriptLoad()                         — from begin()
//   scriptHandleCommand(arg, reply)        — from handleCommand()
//   _scriptEvaluate()                     — from loop() on timer
//   _scriptSendMessage(rule, text)        — internal, called by _scriptEvaluate()

#include "ScriptEngine.h"
#include "MyMesh.h"

// ═══════════════════════════════════════════════════════════════════════════════
//  LOAD / SAVE
// ═══════════════════════════════════════════════════════════════════════════════

void MyMesh::_scriptLoad() {
  memset(_scriptRules, 0, sizeof(_scriptRules));
  _scriptRuleCount = 0;

#if defined(NRF52_PLATFORM) || defined(STM32_PLATFORM)
  File f = _fs->open(SCRIPT_RULES_FILE, FILE_O_READ);
#elif defined(RP2040_PLATFORM)
  File f = _fs->open(SCRIPT_RULES_FILE, "r");
#else
  File f = _fs->open(SCRIPT_RULES_FILE);
#endif
  if (!f) return;

  ScriptRulesFile hdr;
  if (f.read((uint8_t*)&hdr, sizeof(hdr)) != sizeof(hdr)) { f.close(); return; }

  if (hdr.version != SCRIPT_ENGINE_VERSION) {
    MESH_DEBUG_PRINTLN("ScriptEngine: version mismatch, discarding rules");
    f.close(); return;
  }
  if (hdr.count > MAX_SCRIPT_RULES) hdr.count = MAX_SCRIPT_RULES;

  memcpy(_scriptRules, hdr.rules, hdr.count * sizeof(ScriptRule));
  _scriptRuleCount = hdr.count;
  f.close();
  MESH_DEBUG_PRINTLN("ScriptEngine: loaded %d rules", (int)_scriptRuleCount);
}

void MyMesh::_scriptSave() {
  ScriptRulesFile hdr;
  hdr.version = SCRIPT_ENGINE_VERSION;
  hdr.count   = _scriptRuleCount;
  memcpy(hdr.rules, _scriptRules, _scriptRuleCount * sizeof(ScriptRule));

#if defined(NRF52_PLATFORM) || defined(STM32_PLATFORM)
  _fs->remove(SCRIPT_RULES_FILE);
  File f = _fs->open(SCRIPT_RULES_FILE, FILE_O_WRITE);
#elif defined(RP2040_PLATFORM)
  File f = _fs->open(SCRIPT_RULES_FILE, "w");
#else
  if (_fs->exists(SCRIPT_RULES_FILE)) _fs->remove(SCRIPT_RULES_FILE);
  File f = _fs->open(SCRIPT_RULES_FILE, "w");
#endif
  if (!f) { MESH_DEBUG_PRINTLN("ScriptEngine: ERROR saving rules"); return; }
  f.write((uint8_t*)&hdr, sizeof(hdr));
  f.close();
}

// ═══════════════════════════════════════════════════════════════════════════════
//  SEND HELPER
// ═══════════════════════════════════════════════════════════════════════════════

// Initialise a GroupChannel from a ScriptRule.
//
// Both channel types use the same 16-byte key and the same hash derivation:
//   secret[] = 16-byte key, zero-padded to PUB_KEY_SIZE
//   hash[]   = first PATH_HASH_SIZE bytes of SHA256(key[0..15])
//
// Hash channel  (@hash:name):
//   key = SHA256("#" + name)[0..15]
//
// Private channel (@private:hexkey):
//   key = fromHex(hexkey)  — exactly 32 hex chars (16 bytes)
static void _scriptInitChannel(mesh::GroupChannel& ch, const ScriptRule& rule) {
  memset(&ch, 0, sizeof(ch));

  uint8_t key16[16];
  memset(key16, 0, sizeof(key16));

  if (rule.chan_is_hash) {
    char prefixed[40];
    snprintf(prefixed, sizeof(prefixed), "#%s", rule.chan_name);
    uint8_t digest[32];
    mesh::Utils::sha256(digest, sizeof(digest),
                        (const uint8_t*)prefixed, strlen(prefixed));
    memcpy(key16, digest, 16);
  } else {
    // fromHex() requires strlen(src) == dest_size*2 exactly (16*2 = 32 hex chars)
    mesh::Utils::fromHex(key16, 16, rule.chan_name);
  }

  // secret[] = 16-byte key, rest zero-padded
  memcpy(ch.secret, key16, 16);

  // hash[] = first PATH_HASH_SIZE bytes of SHA256(key[0..15])
  uint8_t hash_digest[32];
  mesh::Utils::sha256(hash_digest, sizeof(hash_digest), key16, 16);
  memcpy(ch.hash, hash_digest, sizeof(ch.hash));
}

// Sends a message on a GroupChannel — built exactly like periodic_msg.
void MyMesh::_scriptSendMessage(const ScriptRule& rule, const char* text) {
  mesh::GroupChannel ch;
  _scriptInitChannel(ch, rule);

  uint32_t timestamp = getRTCClock()->getCurrentTimeUnique();
  uint8_t temp[128];
  memcpy(temp, &timestamp, 4);
  temp[4] = 0; // TXT_TYPE_PLAIN
  snprintf((char*)&temp[5], sizeof(temp) - 5, "%s: %s", _prefs.node_name, text);
  int content_len = strlen((char*)&temp[5]);

  auto pkt = createGroupDatagram(PAYLOAD_TYPE_GRP_TXT, ch, temp, 5 + content_len);
  if (pkt) {
    // Use named scope if set, otherwise fall back to default_scope as-is.
    // A local TransportKey is used so default_scope is never modified.
    if (rule.scope_name[0] != 0) {
      TransportKey scope;
      memset(scope.key, 0, sizeof(scope.key));
      RegionEntry* region = region_map.findByName(rule.scope_name);
      if (region) region_map.getTransportKeysFor(*region, &scope, 1);
      sendFloodScoped(scope, pkt, 0, _prefs.path_hash_mode + 1);
    } else {
      sendFloodScoped(default_scope, pkt, 0, _prefs.path_hash_mode + 1);
    }
    MESH_DEBUG_PRINTLN("ScriptEngine: sent '%s' on '%s'", text, rule.chan_name);
  }
}

// ═══════════════════════════════════════════════════════════════════════════════
//  EVALUATE RULES  (called from loop() every minute)
// ═══════════════════════════════════════════════════════════════════════════════

void MyMesh::_scriptEvaluate() {
  if (_scriptRuleCount == 0) return;

  uint32_t now       = getRTCClock()->getCurrentTime();
  uint16_t bat_mv    = board.getBattMilliVolts();
  float    temp_f    = board.getMCUTemperature();     // NaN if not supported by target
  int16_t  noise_dbm = (int16_t)_radio->getNoiseFloor();

  for (uint8_t i = 0; i < _scriptRuleCount; i++) {
    ScriptRule& r = _scriptRules[i];
    if (!r.enabled) continue;

    // Enforce minimum interval
    uint32_t effective_interval = max((uint32_t)SCRIPT_MIN_INTERVAL_SECS, r.interval_secs);
    if (r.last_fired != 0 && (now - r.last_fired) < effective_interval) continue;

    // If at_hour is set, only fire within the correct minute window.
    // Reuse already-fetched timestamp (now) to avoid a second RTC call.
    if (r.at_hour != -1) {
      uint8_t cur_hour   = (now % 86400) / 3600;
      uint8_t cur_minute = (now % 3600)  / 60;
      if (cur_hour != (uint8_t)r.at_hour || cur_minute != (uint8_t)r.at_minute) continue;
    }

    // Evaluate trigger
    bool fire = false;
    int16_t actual_value = 0;
    const char* unit = "";

    switch (r.trigger) {
      case TRIGGER_BAT_BELOW:
        actual_value = (int16_t)bat_mv;
        unit = "mV";
        fire = (bat_mv < (uint16_t)r.trigger_value);
        break;

      case TRIGGER_BAT_ABOVE:
        actual_value = (int16_t)bat_mv;
        unit = "mV";
        fire = (bat_mv > (uint16_t)r.trigger_value);
        break;

      case TRIGGER_TEMP_BELOW:
        if (isnan(temp_f)) break;
        actual_value = (int16_t)temp_f;
        unit = "C";
        fire = (actual_value < r.trigger_value);
        break;

      case TRIGGER_TEMP_ABOVE:
        if (isnan(temp_f)) break;
        actual_value = (int16_t)temp_f;
        unit = "C";
        fire = (actual_value > r.trigger_value);
        break;

      case TRIGGER_NOISE_ABOVE:
        actual_value = noise_dbm;
        unit = "dBm";
        fire = (noise_dbm > r.trigger_value);
        break;

      case TRIGGER_NOISE_BELOW:
      actual_value = noise_dbm;
      unit = "dBm";
      fire = (noise_dbm < r.trigger_value);
      break;

      case TRIGGER_PERIODIC:
        switch (r.report_var) {
          case REPORT_BAT:
            actual_value = (int16_t)bat_mv;
            unit = "mV";
            fire = true;
            break;
          case REPORT_TEMP:
            if (!isnan(temp_f)) {  // only fire if sensor is available
              actual_value = (int16_t)temp_f;
              unit = "C";
              fire = true;
            }
            break;
          case REPORT_NOISE:
            actual_value = noise_dbm;
            unit = "dBm";
            fire = true;
            break;
        }
        break;
    }

    if (!fire) continue;

    // Build message — replace {value} and {unit} in the template
    const char* tmpl = r.message[0] ? r.message : "{value}{unit}";
    char val_str[16];
    snprintf(val_str, sizeof(val_str), "%d", (int)actual_value);

    // Manual template substitution without dynamic allocation
    char buf[128];
    const char* src = tmpl;
    char* dst = buf;
    char* end = buf + sizeof(buf) - 1;
    while (*src && dst < end) {
      if (strncmp(src, "{value}", 7) == 0) {
        size_t vlen = strlen(val_str);
        if (dst + vlen < end) { memcpy(dst, val_str, vlen); dst += vlen; }
        src += 7;
      } else if (strncmp(src, "{unit}", 6) == 0) {
        size_t ulen = strlen(unit);
        if (dst + ulen < end) { memcpy(dst, unit, ulen); dst += ulen; }
        src += 6;
      } else {
        *dst++ = *src++;
      }
    }
    *dst = 0;

    _scriptSendMessage(r, buf);
    r.last_fired = now;
    // last_fired is not persisted to SPIFFS on every fire — it resets on reboot.
    // Worst case: one extra message sent after an unexpected reboot, which is acceptable.
  }
}

// ═══════════════════════════════════════════════════════════════════════════════
//  CLI PARSER
// ═══════════════════════════════════════════════════════════════════════════════
//
// Command format:
//   rule add bat<3400  /3600 @private:MyKey  "Battery low: {value}{unit}"
//   rule add bat>4300  /3600 @hash:AlertChan "Battery high: {value}{unit}"
//   rule add temp<-10  /3600 @private:MyKey  "Temp low: {value}{unit}"
//   rule add temp>60   /3600 @hash:Chan      "Temp high: {value}{unit}"
//   rule add noise>-90 /3600 @hash:Chan      "Noise: {value}{unit}"
//   rule add noise<-80 /3600 @hash:Chan      "Noise: {value}{unit}"
//   rule add report:bat   /3600 @hash:Chan   "Battery: {value}{unit}"
//   rule add report:temp  /1800 @hash:Chan   "Temp: {value}{unit}"
//   rule add report:noise /7200 @hash:Chan   "Noise floor: {value}{unit}"
//   rule list
//   rule del <idx>          (1-based index)
//   rule clear
//   rule test <idx>         (force immediate fire)
//   rule enable <idx>
//   rule disable <idx>

static const char* _triggerName(ScriptTriggerType t, ScriptReportVar rv) {
  switch (t) {
    case TRIGGER_BAT_BELOW:   return "bat<";
    case TRIGGER_BAT_ABOVE:   return "bat>";
    case TRIGGER_TEMP_BELOW:  return "temp<";
    case TRIGGER_TEMP_ABOVE:  return "temp>";
    case TRIGGER_NOISE_ABOVE: return "noise>";
    case TRIGGER_NOISE_BELOW: return "noise<";
    case TRIGGER_PERIODIC:
      switch (rv) {
        case REPORT_BAT:   return "report:bat";
        case REPORT_TEMP:  return "report:temp";
        case REPORT_NOISE: return "report:noise";
      }
  }
  return "?";
}

void MyMesh::scriptHandleCommand(const char* arg, char* reply) {
  while (*arg == ' ') arg++;  // skip leading spaces

  // ── rule list ──────────────────────────────────────────────────────────────
  if (strcmp(arg, "list") == 0) {
    if (_scriptRuleCount == 0) {
      strcpy(reply, "No rules"); return;
    }
    int len = 0;
    for (uint8_t i = 0; i < _scriptRuleCount; i++) {
      const ScriptRule& r = _scriptRules[i];
      char at_buf[12] = "";
      if (r.at_hour != -1) snprintf(at_buf, sizeof(at_buf), " at:%02d:%02d", r.at_hour, r.at_minute);
      len += snprintf(reply + len, 160 - len,
        "%d:%s %s/%us%s @%s:%s%s%s \"%s\"\n",
        i + 1,
        r.enabled ? "on" : "off",
        _triggerName(r.trigger, r.report_var),
        r.interval_secs,
        at_buf,
        r.chan_is_hash ? "hash" : "private",
        r.chan_name,
        r.scope_name[0] ? " @scope:" : "",
        r.scope_name[0] ? r.scope_name : "",
        r.message);
      if (len >= 155) break;
    }
    return;
  }

  // ── rule clear ────────────────────────────────────────────────────────────
  if (strcmp(arg, "clear") == 0) {
    _scriptRuleCount = 0;
    memset(_scriptRules, 0, sizeof(_scriptRules));
    _scriptSave();
    strcpy(reply, "OK - all rules cleared");
    return;
  }

  // ── rule del <n> ──────────────────────────────────────────────────────────
  if (strncmp(arg, "del ", 4) == 0) {
    int idx = atoi(arg + 4) - 1;  // convert 1-based to 0-based
    if (idx < 0 || idx >= (int)_scriptRuleCount) {
      snprintf(reply, 160, "Err - invalid index (1..%d)", (int)_scriptRuleCount);
      return;
    }
    // Shift subsequent rules down one slot
    for (int j = idx; j < (int)_scriptRuleCount - 1; j++) {
      _scriptRules[j] = _scriptRules[j + 1];
    }
    _scriptRuleCount--;
    _scriptSave();
    snprintf(reply, 160, "OK - rule %d deleted", idx + 1);
    return;
  }

  // ── rule enable / disable <n> ─────────────────────────────────────────────
  if (strncmp(arg, "enable ", 7) == 0 || strncmp(arg, "disable ", 8) == 0) {
    bool en = (arg[0] == 'e');
    int idx = atoi(arg + (en ? 7 : 8)) - 1;
    if (idx < 0 || idx >= (int)_scriptRuleCount) {
      snprintf(reply, 160, "Err - invalid index (1..%d)", (int)_scriptRuleCount);
      return;
    }
    _scriptRules[idx].enabled = en;
    _scriptSave();
    snprintf(reply, 160, "OK - rule %d %s", idx + 1, en ? "enabled" : "disabled");
    return;
  }

  // ── rule test <n> ─────────────────────────────────────────────────────────
  if (strncmp(arg, "test ", 5) == 0) {
    int idx = atoi(arg + 5) - 1;
    if (idx < 0 || idx >= (int)_scriptRuleCount) {
      snprintf(reply, 160, "Err - invalid index (1..%d)", (int)_scriptRuleCount);
      return;
    }
    // Reset last_fired so _scriptEvaluate() fires immediately
    _scriptRules[idx].last_fired = 0;
    _scriptEvaluate();
    snprintf(reply, 160, "OK - rule %d tested", idx + 1);
    return;
  }

  // ── rule add ──────────────────────────────────────────────────────────────
  if (strncmp(arg, "add ", 4) == 0) {
    if (_scriptRuleCount >= MAX_SCRIPT_RULES) {
      snprintf(reply, 160, "Err - max %d rules reached", MAX_SCRIPT_RULES);
      return;
    }

    const char* p = arg + 4;
    ScriptRule r;
    memset(&r, 0, sizeof(r));
    r.enabled = true;
    r.at_hour = -1;
    r.at_minute = 0;

    // Parse trigger: bat<, bat>, temp<, temp>, noise>, noise< or report:bat/temp/noise
    if (strncmp(p, "report:", 7) == 0) {
      r.trigger = TRIGGER_PERIODIC;
      p += 7;
      if      (strncmp(p, "bat",   3) == 0) { r.report_var = REPORT_BAT;   p += 3; }
      else if (strncmp(p, "temp",  4) == 0) { r.report_var = REPORT_TEMP;  p += 4; }
      else if (strncmp(p, "noise", 5) == 0) { r.report_var = REPORT_NOISE; p += 5; }
      else { strcpy(reply, "Err - unknown report var (bat/temp/noise)"); return; }
    } else {
      // Detect variable and operator
      const char* ops[] = { "bat<", "bat>", "temp<", "temp>", "noise>", "noise<" };
      ScriptTriggerType types[] = {
        TRIGGER_BAT_BELOW, TRIGGER_BAT_ABOVE,
        TRIGGER_TEMP_BELOW, TRIGGER_TEMP_ABOVE,
        TRIGGER_NOISE_ABOVE, TRIGGER_NOISE_BELOW
      };
      bool found = false;
      for (int k = 0; k < 5; k++) {
        size_t olen = strlen(ops[k]);
        if (strncmp(p, ops[k], olen) == 0) {
          r.trigger       = types[k];
          r.trigger_value = (int16_t)atoi(p + olen);
          p += olen;
          while (*p && *p != ' ') p++;  // skip past the number
          found = true; break;
        }
      }
      if (!found) {
        strcpy(reply, "Err - unknown trigger (bat<, bat>, temp<, temp>, noise>, noise<, report:X)");
        return;
      }
    }

    // Parse interval: /NNNN (seconds)
    while (*p == ' ') p++;
    if (*p != '/') { strcpy(reply, "Err - expected /interval_secs"); return; }
    p++;
    r.interval_secs = (uint32_t)atoi(p);
    if (r.interval_secs < SCRIPT_MIN_INTERVAL_SECS) {
      snprintf(reply, 160, "Err - interval min %d secs (%d min)",
               SCRIPT_MIN_INTERVAL_SECS, SCRIPT_MIN_INTERVAL_SECS / 60);
      return;
    }
    while (*p && *p != ' ') p++;  // skip past the number

    // Parse channel: @private:KEY or @hash:NAME
    // Skip optional at:HH:MM token if it appears before the channel
    while (*p == ' ') p++;
    if (strncmp(p, "at:", 3) == 0) {
      while (*p && *p != ' ') p++;  // skip past at:HH:MM
      while (*p == ' ') p++;
    }
    if (*p != '@') { strcpy(reply, "Err - expected @private:KEY or @hash:NAME"); return; }
    p++;
    if (strncmp(p, "private:", 8) == 0) {
      r.chan_is_hash = false;
      p += 8;
    } else if (strncmp(p, "hash:", 5) == 0) {
      r.chan_is_hash = true;
      p += 5;
    } else {
      strcpy(reply, "Err - expected @private:KEY or @hash:NAME");
      return;
    }
    // Channel name/key runs until next space
    int ci = 0;
    while (*p && *p != ' ' && ci < (int)sizeof(r.chan_name) - 1) {
      r.chan_name[ci++] = *p++;
    }
    r.chan_name[ci] = 0;
    if (ci == 0) { strcpy(reply, "Err - empty channel name/key"); return; }

    // Parse optional @scope:NAME token anywhere in remaining input
    {
      const char* scope_tok = strstr(p, "@scope:");
      if (scope_tok) {
        scope_tok += 7;  // skip "@scope:"
        int si = 0;
        while (*scope_tok && *scope_tok != ' ' && *scope_tok != '"' && si < (int)sizeof(r.scope_name) - 1) {
          r.scope_name[si++] = *scope_tok++;
        }
        r.scope_name[si] = 0;
      }
    }

    // Parse optional at:HH:MM — search in the full remaining arg string
    // before parsing the message, so p still covers the whole tail.
    // Use " at:" prefix to avoid false matches inside channel names.
    {
      const char* at_tok = strstr(p, " at:");
      if (at_tok) {
        at_tok += 4;  // skip " at:"
        int hh = atoi(at_tok);
        const char* colon = strchr(at_tok, ':');
        int mm = colon ? atoi(colon + 1) : 0;
        if (hh >= 0 && hh <= 23 && mm >= 0 && mm <= 59) {
          r.at_hour   = (int8_t)hh;
          r.at_minute = (int8_t)mm;
        }
      }
    }

    // Parse optional message template (quoted)
    const char* msg = strchr(p, '"');
    if (msg) {
      msg++;  // skip opening quote
      int mi = 0;
      while (*msg && *msg != '"' && mi < (int)sizeof(r.message) - 1) {
        r.message[mi++] = *msg++;
      }
      r.message[mi] = 0;
    } else {
      // Default template if none provided
      strcpy(r.message, "{value}{unit}");
    }

    _scriptRules[_scriptRuleCount++] = r;
    _scriptSave();
    char at_reply[12] = "";
    if (r.at_hour != -1) snprintf(at_reply, sizeof(at_reply), " at:%02d:%02d", r.at_hour, r.at_minute);
    snprintf(reply, 160, "OK - rule %d added: %s/%us%s @%s:%s%s%s",
             (int)_scriptRuleCount,
             _triggerName(r.trigger, r.report_var),
             r.interval_secs,
             at_reply,
             r.chan_is_hash ? "hash" : "private",
             r.chan_name,
             r.scope_name[0] ? " @scope:" : "",
             r.scope_name[0] ? r.scope_name : "");
    return;
  }

  // ── Unknown sub-command ───────────────────────────────────────────────────
  strcpy(reply,
    "Err - usage: rule <add|list|del <n>|clear|test <n>|enable <n>|disable <n>>");
}