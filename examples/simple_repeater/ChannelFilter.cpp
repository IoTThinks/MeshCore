// PowerSaving variant
// Packet filter engine - compiled only when built with -D PACKET_FILTER=1
#if PACKET_FILTER

#include "ChannelFilter.h"

#include "MyMesh.h"

#include <stdio.h>
#include <string.h>

// Stable rule key strings for JSON serialization (ra..rh for up to 8 rules)
const char *const kRuleKeys[] = { "ra", "rb", "rc", "rd", "re", "rf", "rg", "rh", "ri",
                                  "rj", "rk", "rl", "rm", "rn", "ro", "rp", "rq", "rr",
                                  "rs", "rt", "ru", "rv", "rw", "rx", "ry", "rz" };

// ---------------------------------------------------------------------------
// Helpers shared between FilterProfileSerializer and ChannelFilter
// ---------------------------------------------------------------------------

static bool applyOp(FilterOp op, int16_t pkt_val, int16_t rule_val) {
  switch (op) {
  case FilterOp::EQ:
    return pkt_val == rule_val;
  case FilterOp::NEQ:
    return pkt_val != rule_val;
  case FilterOp::GT:
    return pkt_val > rule_val;
  case FilterOp::LT:
    return pkt_val < rule_val;
  default:
    return false;
  }
}

// ---------------------------------------------------------------------------
// ChannelFilter — constructor, load, save
// ---------------------------------------------------------------------------

ChannelFilter::ChannelFilter() : _active(0) {
  // Profiles initialize themselves in their own constructors
}

void ChannelFilter::load(FILESYSTEM &fs) {
  // Try JSON file first
#if defined(NRF52_PLATFORM) || defined(STM32_PLATFORM)
  File f = fs.open(FILTER_RULES_FILE, FILE_O_READ);
#elif defined(RP2040_PLATFORM)
  File f = fs.open(FILTER_RULES_FILE, "r");
#else
  File f = fs.open(FILTER_RULES_FILE);
#endif
  if (f) {
    loadSerial(f);
    f.close();
    // Clamp _active to valid range in case of corrupt file
    if (_active >= MAX_FILTER_PROFILES) _active = 0;
    return;
  }

  // JSON not found — try legacy binary migration (loads into profile 1)
  if (_loadBinary(fs)) {
    save(fs);
#if defined(NRF52_PLATFORM) || defined(STM32_PLATFORM)
    fs.remove(FILTER_RULES_FILE_BIN);
#else
    if (fs.exists(FILTER_RULES_FILE_BIN)) fs.remove(FILTER_RULES_FILE_BIN);
#endif
  }
}

void ChannelFilter::save(FILESYSTEM &fs) const {
#if defined(NRF52_PLATFORM) || defined(STM32_PLATFORM)
  fs.remove(FILTER_RULES_FILE);
  File f = fs.open(FILTER_RULES_FILE, FILE_O_WRITE);
#elif defined(RP2040_PLATFORM)
  File f = fs.open(FILTER_RULES_FILE, "w");
#else
  if (fs.exists(FILTER_RULES_FILE)) fs.remove(FILTER_RULES_FILE);
  File f = fs.open(FILTER_RULES_FILE, "w");
#endif
  if (!f) return;

  // saveSerial() needs non-const — cast is safe: only writes, never modifies state
  const_cast<ChannelFilter *>(this)->saveSerial(f);
  f.close();
}

bool ChannelFilter::_loadBinary(FILESYSTEM &fs) {
#if defined(NRF52_PLATFORM) || defined(STM32_PLATFORM)
  File f = fs.open(FILTER_RULES_FILE_BIN, FILE_O_READ);
#elif defined(RP2040_PLATFORM)
  File f = fs.open(FILTER_RULES_FILE_BIN, "r");
#else
  File f = fs.open(FILTER_RULES_FILE_BIN);
#endif
  if (!f) return false;

  uint8_t magic;
  if (f.read(&magic, 1) != 1 || magic != FILTER_FILE_MAGIC) {
    f.close();
    return false;
  }

  uint8_t version;
  if (f.read(&version, 1) != 1 || version < FILTER_FILE_VERSION) {
    f.close();
    return false;
  }

  uint8_t mode_byte;
  if (f.read(&mode_byte, 1) != 1) {
    f.close();
    return false;
  }

  // Migrate into profile 1 (index 0)
  FilterProfileSerializer &p = _profiles[0];
  p.setMode((mode_byte <= (uint8_t)FilterMode::DROP) ? (FilterMode)mode_byte : FilterMode::ALLOW);

  FilterRule *rules = p.getRules();
  size_t sz = sizeof(FilterRule) * MAX_FILTER_RULES;
  size_t bytes_read = f.read((uint8_t *)rules, sz);
  if (bytes_read < sz) memset((uint8_t *)rules + bytes_read, 0, sz - bytes_read);
  f.close();
  _active = 0;
  return true;
}

// ---------------------------------------------------------------------------
// Evaluation
// ---------------------------------------------------------------------------

bool ChannelFilter::_evalScalar(FilterField field, FilterOp op, int16_t val, const mesh::Packet *pkt,
                                int16_t rssi) const {
  switch (field) {
  case FilterField::ROUTE:
    return applyOp(op, (int16_t)pkt->getRouteType(), val);
  case FilterField::TYPE:
    return applyOp(op, (int16_t)pkt->getPayloadType(), val);
  case FilterField::HOPS:
    return applyOp(op, (int16_t)pkt->getPathHashCount(), val);
  case FilterField::PATHSIZE:
    return applyOp(op, (int16_t)pkt->getPathHashSize(), val);
  case FilterField::CHANNEL: {
    uint8_t pt = pkt->getPayloadType();
    if (pt != 0x05 && pt != 0x06) return false;
    if (pkt->payload_len < 1) return false;
    return applyOp(op, (int16_t)pkt->payload[0], val);
  }
  case FilterField::SNR:
    return applyOp(op, (int16_t)pkt->_snr, val);
  case FilterField::RSSI:
    return applyOp(op, rssi, val);
  default:
    return false;
  }
}

// Evaluate source AND condition — checks payload[1] as src_hash.
// Returns true if source condition passes (or is not set).
static bool evalSource(const FilterRule &rule, const mesh::Packet *pkt) {
  if (rule.source_hash_count == 0) return true; // no source filter

  uint8_t pt = pkt->getPayloadType();

  // Source check only applicable where payload[1] is src_hash
  bool applicable = (pt == PAYLOAD_TYPE_REQ || // req
                     pt == PAYLOAD_TYPE_RESPONSE || // resp
                     pt == PAYLOAD_TYPE_TXT_MSG || // txt
                     pt == PAYLOAD_TYPE_ANON_REQ || // anonreq
                     pt == PAYLOAD_TYPE_PATH);  // path
  if (!applicable) return true;    // auto-true for advert/ack/ctrl/etc.

  if (pkt->payload_len < 2) return false;
  uint8_t src = pkt->payload[1];
  for (uint8_t i = 0; i < rule.source_hash_count; i++) {
    if (rule.source_hashes[i] == src) return true;
  }
  return false;
}

// Evaluate scope AND condition — checks packet's region against named region.
// Returns true if scope condition passes (or is not set).
static bool evalScope(const FilterRule &rule, const mesh::Packet *pkt, const RegionMap *region_map) {
  if (rule.scope_name[0] == '\0') return true; // no scope filter
  if (!region_map) return true;                // no region_map available — skip check

  // Direct packets have no transport_codes — scope check auto-passes
  if (!pkt->hasTransportCodes()) return true;

  RegionMap *rm = const_cast<RegionMap *>(region_map);
  const RegionEntry *target = rm->findByName(rule.scope_name);
  if (!target) return false; // unknown scope — no match

  const RegionEntry *pkt_region = rm->findMatch(const_cast<mesh::Packet *>(pkt), 0);
  if (!pkt_region) return false;

  return pkt_region->id == target->id;
}

bool ChannelFilter::_ruleMatches(const FilterRule &rule, const mesh::Packet *pkt, int16_t rssi,
                                 const RegionMap *region_map) const {
  // --- Primary field evaluation ---

  if (rule.field == FilterField::PATH) {
    uint8_t hash_size = pkt->getPathHashSize();
    uint8_t hash_count = pkt->getPathHashCount();
    if (hash_count == 0) return false;
    if (hash_size != rule.path_hash_len) return false;
    uint16_t last_hop_offset = (uint16_t)(hash_count - 1) * hash_size;
    if (last_hop_offset + hash_size > MAX_PATH_SIZE) return false;
    const uint8_t *last_hop = pkt->path + last_hop_offset;
    bool found = false;
    for (uint8_t i = 0; i < rule.path_hash_count; i++) {
      if (memcmp(rule.path_hashes[i], last_hop, hash_size) == 0) {
        found = true;
        break;
      }
    }
    bool primary = (rule.op == FilterOp::EQ) ? found : !found;
    if (!primary) return false;

  } else if (rule.field == FilterField::CHANNEL) {
    uint8_t pt = pkt->getPayloadType();
    if (pt != 0x05 && pt != 0x06) return false;
    if (pkt->payload_len < 1) return false;
    uint8_t ch = pkt->payload[0];
    bool found = false;
    for (uint8_t i = 0; i < rule.channel_hash_count; i++) {
      if (rule.channel_hashes[i] == ch) {
        found = true;
        break;
      }
    }
    bool primary;
    if (rule.op == FilterOp::EQ)
      primary = found;
    else if (rule.op == FilterOp::NEQ)
      primary = !found;
    else
      return false;
    if (!primary) return false;

  } else if (rule.field == FilterField::REPEATER) {
    uint8_t pt = pkt->getPayloadType();
    bool is_mgmt = (pt == PAYLOAD_TYPE_REQ || pt == PAYLOAD_TYPE_RESPONSE || pt == PAYLOAD_TYPE_TXT_MSG ||
                    pt == PAYLOAD_TYPE_ACK || pt == PAYLOAD_TYPE_ADVERT || pt == PAYLOAD_TYPE_ANON_REQ ||
                    pt == PAYLOAD_TYPE_PATH || pt == PAYLOAD_TYPE_CONTROL);
    if (!is_mgmt) return false;

  } else if (rule.field == FilterField::SCOPE) {
    // SCOPE as primary field — packet must match named region
    if (!pkt->hasTransportCodes()) return false;
    if (!region_map || rule.scope_name[0] == '\0') return false;
    RegionMap *rm = const_cast<RegionMap *>(region_map);
    const RegionEntry *target = rm->findByName(rule.scope_name);
    if (!target) return false;
    const RegionEntry *pkt_region = rm->findMatch(const_cast<mesh::Packet *>(pkt), 0);
    if (!pkt_region) return false;
    if (pkt_region->id != target->id) return false;

  } else {
    // Scalar primary condition
    if (!_evalScalar(rule.field, rule.op, rule.value, pkt, rssi)) return false;
  }

  // --- Universal AND conditions (applied after any primary field) ---

  // Scalar AND condition
  if (rule.and_field != FILTER_FIELD_NONE) {
    if (!_evalScalar((FilterField)rule.and_field, rule.and_op, rule.and_value, pkt, rssi)) return false;
  }

  // Source AND condition
  if (!evalSource(rule, pkt)) return false;

  // Scope AND condition
  if (!evalScope(rule, pkt, region_map)) return false;

  return true;
}

bool ChannelFilter::evaluate(const mesh::Packet *pkt, int16_t rssi, const RegionMap *region_map) const {
  if (!pkt) return false;
  const FilterProfileSerializer *prof = &_profiles[_active];
  const FilterRule *rules = prof->getRules();

  for (uint8_t i = 0; i < MAX_FILTER_RULES; i++) {
    const FilterRule &rule = rules[i];
    if (!rule.in_use || !rule.enabled) continue;
    if (_ruleMatches(rule, pkt, rssi, region_map)) {
      return rule.action == FilterAction::DROP;
    }
  }
  // No rule matched — apply default policy
  return prof->getMode() == FilterMode::DROP;
}

// ---------------------------------------------------------------------------
// List output helpers
// ---------------------------------------------------------------------------

static const char *fieldStr(FilterField f) {
  switch (f) {
  case FilterField::ROUTE:
    return "route";
  case FilterField::TYPE:
    return "payload";
  case FilterField::HOPS:
    return "hops";
  case FilterField::PATHSIZE:
    return "pathsize";
  case FilterField::PATH:
    return "path";
  case FilterField::CHANNEL:
    return "channel";
  case FilterField::SNR:
    return "snr";
  case FilterField::RSSI:
    return "rssi";
  case FilterField::REPEATER:
    return "repeater";
  case FilterField::SCOPE:
    return "scope";
  default:
    return "?";
  }
}

static const char *opStr(FilterOp op) {
  switch (op) {
  case FilterOp::EQ:
    return "eq";
  case FilterOp::NEQ:
    return "neq";
  case FilterOp::GT:
    return "gt";
  case FilterOp::LT:
    return "lt";
  default:
    return "?";
  }
}

static const char *routeValueStr(int16_t v) {
  switch (v) {
  case 0x00:
    return "tflood";
  case 0x01:
    return "flood";
  case 0x02:
    return "direct";
  case 0x03:
    return "tdirect";
  default:
    return "?";
  }
}

static const char *payloadTypeValueStr(int16_t v) {
  switch (v) {
  case 0x00:
    return "req";
  case 0x01:
    return "resp";
  case 0x02:
    return "txt";
  case 0x03:
    return "ack";
  case 0x04:
    return "advert";
  case 0x05:
    return "grptxt";
  case 0x06:
    return "grpdata";
  case 0x07:
    return "anonreq";
  case 0x08:
    return "path";
  case 0x09:
    return "trace";
  case 0x0A:
    return "multi";
  case 0x0B:
    return "ctrl";
  case 0x0F:
    return "raw";
  default:
    return "?";
  }
}

void ChannelFilter::formatRuleValue(const FilterRule &rule, char *out, int outlen) {
  if (outlen <= 0) return;
  if (rule.field == FilterField::PATH) {
    int pos = 0;
    for (uint8_t i = 0; i < rule.path_hash_count && pos < outlen - 1; i++) {
      if (i > 0 && pos < outlen - 2) out[pos++] = ' ';
      for (uint8_t b = 0; b < rule.path_hash_len && pos < outlen - 3; b++) {
        pos += snprintf(out + pos, outlen - pos, "%02X", rule.path_hashes[i][b]);
      }
    }
    out[pos] = '\0';
  } else if (rule.field == FilterField::ROUTE) {
    snprintf(out, outlen, "%s", routeValueStr(rule.value));
  } else if (rule.field == FilterField::TYPE) {
    snprintf(out, outlen, "%s", payloadTypeValueStr(rule.value));
  } else if (rule.field == FilterField::CHANNEL) {
    int pos = 0;
    for (uint8_t i = 0; i < rule.channel_hash_count && pos < outlen - 1; i++) {
      if (i > 0 && pos < outlen - 2) out[pos++] = ' ';
      pos += snprintf(out + pos, outlen - pos, "0x%02X", rule.channel_hashes[i]);
    }
    out[pos] = '\0';
  } else if (rule.field == FilterField::REPEATER) {
    // Show source hashes if present
    if (rule.source_hash_count > 0) {
      int pos = snprintf(out, outlen, "src");
      for (uint8_t i = 0; i < rule.source_hash_count && pos < outlen - 5; i++) {
        pos += snprintf(out + pos, outlen - pos, " 0x%02X", rule.source_hashes[i]);
      }
    } else {
      out[0] = '\0';
    }
  } else if (rule.field == FilterField::SCOPE) {
    snprintf(out, outlen, "%s", rule.scope_name);
  } else if (rule.field == FilterField::SNR) {
    snprintf(out, outlen, "%d", (int)(rule.value / 4));
  } else {
    snprintf(out, outlen, "%d", (int)rule.value);
  }
}

static int formatRuleLine(const FilterRule &rule, uint8_t idx, char *buf, int buflen) {
  char val_buf[48];
  ChannelFilter::formatRuleValue(rule, val_buf, (int)sizeof(val_buf));

  // Build scalar AND suffix
  char and_buf[48] = "";
  if (rule.and_field != FILTER_FIELD_NONE) {
    char and_val_buf[32];
    FilterField af = (FilterField)rule.and_field;
    if (af == FilterField::ROUTE) {
      snprintf(and_val_buf, sizeof(and_val_buf), "%s", routeValueStr(rule.and_value));
    } else if (af == FilterField::TYPE) {
      snprintf(and_val_buf, sizeof(and_val_buf), "%s", payloadTypeValueStr(rule.and_value));
    } else if (af == FilterField::CHANNEL) {
      snprintf(and_val_buf, sizeof(and_val_buf), "0x%02X", (uint8_t)rule.and_value);
    } else if (af == FilterField::SNR) {
      snprintf(and_val_buf, sizeof(and_val_buf), "%d", (int)(rule.and_value / 4));
    } else {
      snprintf(and_val_buf, sizeof(and_val_buf), "%d", (int)rule.and_value);
    }
    snprintf(and_buf, sizeof(and_buf), " and %s %s %s", fieldStr(af), opStr(rule.and_op), and_val_buf);
  }

  // Build source AND suffix
  char src_buf[32] = "";
  if (rule.source_hash_count > 0) {
    int pos = snprintf(src_buf, sizeof(src_buf), " and source");
    for (uint8_t i = 0; i < rule.source_hash_count && pos < (int)sizeof(src_buf) - 5; i++) {
      pos += snprintf(src_buf + pos, sizeof(src_buf) - pos, " 0x%02X", rule.source_hashes[i]);
    }
  }

  // Build scope AND suffix
  char scope_buf[32] = "";
  if (rule.scope_name[0] != '\0') {
    snprintf(scope_buf, sizeof(scope_buf), " and scope %s", rule.scope_name);
  }

  // REPEATER and SCOPE primary fields have no op in output
  if (rule.field == FilterField::REPEATER) {
    return snprintf(buf, buflen, "%d%s %s repeater%s%s%s\n", idx, rule.enabled ? "" : "*",
                    rule.action == FilterAction::DROP ? "drop" : "allow", and_buf, src_buf, scope_buf);
  }
  if (rule.field == FilterField::SCOPE) {
    // scope_name is already shown as the primary value — don't repeat it as an AND suffix
    return snprintf(buf, buflen, "%d%s %s scope %s%s%s\n", idx, rule.enabled ? "" : "*",
                    rule.action == FilterAction::DROP ? "drop" : "allow", val_buf, and_buf, src_buf);
  }

  return snprintf(buf, buflen, "%d%s %s %s %s %s%s%s%s\n", idx, rule.enabled ? "" : "*",
                  rule.action == FilterAction::DROP ? "drop" : "allow", fieldStr(rule.field), opStr(rule.op),
                  val_buf, and_buf, src_buf, scope_buf);
}

#define FILTER_REPLY_BUDGET     128
#define FILTER_LINE_MAX         128 // longest possible rule line is ~120 chars
#define FILTER_REPLY_HEADER_MAX 36 // slightly larger to fit profile info
#define FILTER_REPLY_HINT_LEN   18

void ChannelFilter::_listRules(char *reply, uint8_t page, bool is_remote, uint8_t profile_num) const {
  const FilterProfileSerializer *prof = _getProfile(profile_num);
  const FilterRule *rules = prof->getRules();

  // Collect in-use rule indexes
  uint8_t indexes[MAX_FILTER_RULES];
  uint8_t total = 0;
  for (uint8_t i = 0; i < MAX_FILTER_RULES; i++) {
    if (rules[i].in_use) indexes[total++] = i;
  }

  uint8_t shown_profile = (profile_num == 0) ? _active + 1 : profile_num;
  const char *mode_str = (prof->getMode() == FilterMode::DROP) ? "drop" : "allow";
  const char *active_marker = (shown_profile == _active + 1) ? "*" : "";

  if (!is_remote) {
    // Local Serial — dump everything at once
    Serial.printf("profile:%d%s mode:%s rules:%d/%d\n", shown_profile, active_marker, mode_str, total,
                  MAX_FILTER_RULES);
    for (uint8_t i = 0; i < total; i++) {
      char line[FILTER_LINE_MAX];
      formatRuleLine(rules[indexes[i]], indexes[i], line, sizeof(line));
      Serial.printf("%s", line);
    }
    return;
  }

  // Remote — paginate within FILTER_REPLY_BUDGET
  uint8_t page_start[MAX_FILTER_RULES + 1];
  uint8_t num_pages = 0;
  page_start[0] = 0;
  {
    uint8_t i = 0;
    while (i < total) {
      int budget = FILTER_REPLY_BUDGET - FILTER_REPLY_HEADER_MAX - FILTER_REPLY_HINT_LEN;
      uint8_t page_end = i;
      while (page_end < total) {
        char line_buf[FILTER_LINE_MAX];
        int line_len =
            formatRuleLine(rules[indexes[page_end]], indexes[page_end], line_buf, sizeof(line_buf));
        if (budget - line_len < 0) break;
        budget -= line_len;
        page_end++;
      }
      if (page_end == i) page_end = i + 1;
      num_pages++;
      i = page_end;
      page_start[num_pages] = i;
    }
  }
  if (total == 0) num_pages = 1;

  if (page >= num_pages) {
    snprintf(reply, 80, "Err - page %d out of range (0-%d)", page, num_pages - 1);
    return;
  }

  int pos;
  if (num_pages > 1) {
    pos = snprintf(reply, FILTER_REPLY_BUDGET, "p%d%s mode:%s %d/%d pg%d/%d\n", shown_profile, active_marker,
                   mode_str, total, MAX_FILTER_RULES, page + 1, num_pages);
  } else {
    pos = snprintf(reply, FILTER_REPLY_BUDGET, "p%d%s mode:%s rules:%d/%d\n", shown_profile, active_marker,
                   mode_str, total, MAX_FILTER_RULES);
  }

  if (total == 0) {
    snprintf(reply + pos, FILTER_REPLY_BUDGET - pos, "(no rules)");
    return;
  }

  uint8_t start = page_start[page];
  uint8_t end = page_start[page + 1];
  for (uint8_t i = start; i < end; i++) {
    char line_buf[FILTER_LINE_MAX];
    formatRuleLine(rules[indexes[i]], indexes[i], line_buf, sizeof(line_buf));
    int written = snprintf(reply + pos, FILTER_REPLY_BUDGET - pos, "%s", line_buf);
    if (written > 0) pos += written;
    if (pos >= FILTER_REPLY_BUDGET) {
      pos = FILTER_REPLY_BUDGET;
      break;
    }
  }

  if (page + 1 < num_pages && pos < FILTER_REPLY_BUDGET) {
    uint8_t pnum = (profile_num == 0) ? 0 : profile_num;
    if (pnum == 0) {
      snprintf(reply + pos, FILTER_REPLY_BUDGET - pos, "-> filter list %d", page + 1);
    } else {
      snprintf(reply + pos, FILTER_REPLY_BUDGET - pos, "-> filter %d list %d", pnum, page + 1);
    }
  }
}

// ---------------------------------------------------------------------------
// CLI dispatch
// ---------------------------------------------------------------------------

void ChannelFilter::handleCommand(const char *args, char *reply, FILESYSTEM &fs, ClientInfo *sender) {
  if (!args) args = ""; // guard against null input
  bool is_remote = (sender != nullptr);
  FilterParseResult res = parseFilterCommand(args);

  if (res.error != FilterParseError::OK) {
    snprintf(reply, 80, "%s", filterParseErrorStr(res.error));
    return;
  }

  // SET_PROFILE and GET_PROFILE don't target a specific profile's rules
  if (res.command == FilterCommand::SET_PROFILE) {
    if (res.target_profile < 1 || res.target_profile > MAX_FILTER_PROFILES) {
      snprintf(reply, 80, "Err - invalid profile");
      return;
    }
    _active = res.target_profile - 1;
    save(fs);
    snprintf(reply, 80, "OK - active profile: %d", res.target_profile);
    return;
  }

  if (res.command == FilterCommand::GET_PROFILE) {
    snprintf(reply, 80, "active profile: %d", _active + 1);
    return;
  }

  if (res.command == FilterCommand::LIST) {
    _listRules(reply, res.rule_id, is_remote, res.target_profile);
    return;
  }

  // All other commands operate on the target profile
  FilterProfileSerializer *prof = _getProfile(res.target_profile);

  switch (res.command) {
  case FilterCommand::ADD: {
    int slot = prof->firstFreeSlot();
    if (slot < 0) {
      snprintf(reply, 80, "Err - rules full (max %d)", MAX_FILTER_RULES);
      return;
    }
    prof->getRules()[slot] = res.rule;
    save(fs);
    snprintf(reply, 80, "OK - rule %d added (profile %d)", slot,
             res.target_profile == 0 ? _active + 1 : res.target_profile);
    break;
  }
  case FilterCommand::DEL: {
    uint8_t id = res.rule_id;
    if (!prof->getRules()[id].in_use) {
      snprintf(reply, 80, "Err - rule %d not in use", id);
      return;
    }
    memset(&prof->getRules()[id], 0, sizeof(FilterRule));
    save(fs);
    snprintf(reply, 80, "OK - rule %d deleted", id);
    break;
  }
  case FilterCommand::DISABLE: {
    uint8_t id = res.rule_id;
    if (!prof->getRules()[id].in_use) {
      snprintf(reply, 80, "Err - rule %d not in use", id);
      return;
    }
    prof->getRules()[id].enabled = false;
    save(fs);
    snprintf(reply, 80, "OK - rule %d disabled", id);
    break;
  }
  case FilterCommand::ENABLE: {
    uint8_t id = res.rule_id;
    if (!prof->getRules()[id].in_use) {
      snprintf(reply, 80, "Err - rule %d not in use", id);
      return;
    }
    prof->getRules()[id].enabled = true;
    save(fs);
    snprintf(reply, 80, "OK - rule %d enabled", id);
    break;
  }
  case FilterCommand::CLEAR:
    prof->reset();
    save(fs);
    snprintf(reply, 80, "OK - profile %d cleared",
             res.target_profile == 0 ? _active + 1 : res.target_profile);
    break;
  case FilterCommand::MODE:
    prof->setMode(res.mode);
    save(fs);
    snprintf(reply, 80, "OK - profile %d mode: %s",
             res.target_profile == 0 ? _active + 1 : res.target_profile,
             res.mode == FilterMode::DROP ? "drop" : "allow");
    break;
  default:
    break;
  }
}

#endif // PACKET_FILTER
