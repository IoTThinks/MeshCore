#include "MyMesh.h"
#include "ChannelFilter.h"
#include <string.h>
#include <stdio.h>

// ---------------------------------------------------------------------------
// Persistence layout (binary blob, fixed size):
//   [uint8_t  mode]
//   [FilterRule * MAX_FILTER_RULES]
// ---------------------------------------------------------------------------

ChannelFilter::ChannelFilter() {
    memset(_rules, 0, sizeof(_rules));
    _mode = FilterMode::ALLOW;  // safe default: pass all packets if no rules loaded
}

// ---------------------------------------------------------------------------
// Load / Save
// ---------------------------------------------------------------------------

void ChannelFilter::load(FILESYSTEM& fs) {
#if defined(NRF52_PLATFORM) || defined(STM32_PLATFORM)
    File f = fs.open(FILTER_RULES_FILE, FILE_O_READ);
#elif defined(RP2040_PLATFORM)
    File f = fs.open(FILTER_RULES_FILE, "r");
#else
    File f = fs.open(FILTER_RULES_FILE);
#endif
    if (!f) return;

    uint8_t mode_byte;
    if (f.read(&mode_byte, 1) != 1) { f.close(); return; }
    // Validate mode byte — default to ALLOW if file is corrupt
    _mode = (mode_byte <= (uint8_t)FilterMode::DROP)
            ? (FilterMode)mode_byte
            : FilterMode::ALLOW;

    // Check read length — if truncated, zero remaining slots (in_use=false = harmless)
    size_t bytes_read = f.read((uint8_t*)_rules, sizeof(_rules));
    if (bytes_read < sizeof(_rules)) {
        memset((uint8_t*)_rules + bytes_read, 0, sizeof(_rules) - bytes_read);
    }
    f.close();
}

void ChannelFilter::save(FILESYSTEM& fs) const {
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

    uint8_t mode_byte = (uint8_t)_mode;
    f.write(&mode_byte, 1);
    f.write((const uint8_t*)_rules, sizeof(_rules));
    f.close();
}

// ---------------------------------------------------------------------------
// Evaluation helpers
// ---------------------------------------------------------------------------

static bool applyOp(FilterOp op, int16_t pkt_val, int16_t rule_val) {
    switch (op) {
        case FilterOp::EQ:  return pkt_val == rule_val;
        case FilterOp::NEQ: return pkt_val != rule_val;
        case FilterOp::GT:  return pkt_val >  rule_val;
        case FilterOp::LT:  return pkt_val <  rule_val;
        default:            return false;
    }
}

bool ChannelFilter::_ruleMatches(const FilterRule& rule, const mesh::Packet* pkt, int16_t rssi) const {
    // PATH field has its own OR-list logic — handle separately
    if (rule.field == FilterField::PATH) {
        uint8_t hash_size  = pkt->getPathHashSize();
        uint8_t hash_count = pkt->getPathHashCount();

        if (hash_count == 0) return false;
        if (hash_size != rule.path_hash_len) return false;

        uint16_t last_hop_offset = (uint16_t)(hash_count - 1) * hash_size;
        if (last_hop_offset + hash_size > MAX_PATH_SIZE) return false;

        const uint8_t* last_hop = pkt->path + last_hop_offset;
        bool found = false;
        for (uint8_t i = 0; i < rule.path_hash_count; i++) {
            if (memcmp(rule.path_hashes[i], last_hop, hash_size) == 0) {
                found = true;
                break;
            }
        }

        bool primary_match = (rule.op == FilterOp::EQ) ? found : !found;
        if (!primary_match) return false;

        // AND condition (PATH as primary can still have a scalar AND)
        if (rule.and_field != FILTER_FIELD_NONE) {
            if (!_evalScalar((FilterField)rule.and_field, rule.and_op, rule.and_value, pkt, rssi))
                return false;
        }
        return true;
    }

    // Scalar primary condition
    if (!_evalScalar(rule.field, rule.op, rule.value, pkt, rssi)) return false;

    // AND condition if present
    if (rule.and_field != FILTER_FIELD_NONE) {
        if (!_evalScalar((FilterField)rule.and_field, rule.and_op, rule.and_value, pkt, rssi)) return false;
    }

    return true;
}

// Evaluate a single scalar condition against a packet.
// PATH field is not handled here — it has its own block in the switch above.
bool ChannelFilter::_evalScalar(FilterField field, FilterOp op, int16_t val,
                                const mesh::Packet* pkt, int16_t rssi) const {
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

bool ChannelFilter::evaluate(const mesh::Packet* pkt, int16_t rssi) const {
    if (!pkt) return false;  // null guard — pass unknown packets rather than crash
    for (uint8_t i = 0; i < MAX_FILTER_RULES; i++) {
        const FilterRule& rule = _rules[i];
        if (!rule.in_use || !rule.enabled) continue;

        if (_ruleMatches(rule, pkt, rssi)) {
            return rule.action == FilterAction::DROP;
        }
    }
    // No rule matched — apply default policy
    return _mode == FilterMode::DROP;
}

// ---------------------------------------------------------------------------
// Slot helpers
// ---------------------------------------------------------------------------

int ChannelFilter::_firstFreeSlot() const {
    for (int i = 0; i < MAX_FILTER_RULES; i++) {
        if (!_rules[i].in_use) return i;
    }
    return -1;
}

// ---------------------------------------------------------------------------
// List formatting
// ---------------------------------------------------------------------------

// Return a short token string for a FilterField value
static const char* fieldStr(FilterField f) {
    switch (f) {
        case FilterField::ROUTE:    return "route";
        case FilterField::TYPE:     return "payload";
        case FilterField::HOPS:     return "hops";
        case FilterField::PATHSIZE: return "pathsize";
        case FilterField::PATH:     return "path";
        case FilterField::CHANNEL:  return "channel";
        case FilterField::SNR:      return "snr";
        case FilterField::RSSI:     return "rssi";
        default:                    return "?";
    }
}

static const char* opStr(FilterOp op) {
    switch (op) {
        case FilterOp::EQ:  return "eq";
        case FilterOp::NEQ: return "neq";
        case FilterOp::GT:  return "gt";
        case FilterOp::LT:  return "lt";
        default:            return "?";
    }
}

// Translate ROUTE_TYPE_* numeric value to token string
static const char* routeValueStr(int16_t v) {
    switch (v) {
        case 0x00: return "tflood";
        case 0x01: return "flood";
        case 0x02: return "direct";
        case 0x03: return "tdirect";
        default:   return "?";
    }
}

// Translate PAYLOAD_TYPE_* numeric value to token string
static const char* payloadTypeValueStr(int16_t v) {
    switch (v) {
        case 0x00: return "req";
        case 0x01: return "resp";
        case 0x02: return "txt";
        case 0x03: return "ack";
        case 0x04: return "advert";
        case 0x05: return "grptxt";
        case 0x06: return "grpdata";
        case 0x07: return "anonreq";
        case 0x08: return "path";
        case 0x09: return "trace";
        case 0x0A: return "multi";
        case 0x0B: return "ctrl";
        case 0x0F: return "raw";
        default:   return "?";
    }
}

static void formatRuleValue(const FilterRule& rule, char* out, int outlen) {
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
        snprintf(out, outlen, "0x%02X", (uint8_t)rule.value);
    } else if (rule.field == FilterField::SNR) {
        // Convert stored quarter-dB back to whole dB for display
        snprintf(out, outlen, "%d", (int)(rule.value / 4));
    } else {
        snprintf(out, outlen, "%d", (int)rule.value);
    }
}

// Maximum reply length — stay safely below the 138-char packet limit
#define FILTER_REPLY_BUDGET  128
// Reserved for header and hint line
#define FILTER_REPLY_HEADER_MAX  32
#define FILTER_REPLY_HINT_LEN    18  // "-> filter list N\0"

// Format a single rule line into buf (null-terminated). Returns number of chars written.
static int formatRuleLine(const FilterRule& rule, uint8_t idx, char* buf, int buflen) {
    char val_buf[32];
    formatRuleValue(rule, val_buf, (int)sizeof(val_buf));

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
        snprintf(and_buf, sizeof(and_buf), " and %s %s %s",
            fieldStr(af), opStr(rule.and_op), and_val_buf);
    }

    return snprintf(buf, buflen, "%d%s %s %s %s %s%s\n",
        idx,
        rule.enabled ? "" : "*",
        rule.action == FilterAction::DROP ? "drop" : "allow",
        fieldStr(rule.field),
        opStr(rule.op),
        val_buf,
        and_buf
    );
}

void ChannelFilter::_listRules(char* reply, uint8_t page) const {
    // Count in-use rules and collect their indexes
    uint8_t indexes[MAX_FILTER_RULES];
    uint8_t total = 0;
    for (uint8_t i = 0; i < MAX_FILTER_RULES; i++) {
        if (_rules[i].in_use) indexes[total++] = i;
    }

    const char* mode_str = (_mode == FilterMode::DROP) ? "drop" : "allow";

    // Pre-scan: determine page boundaries dynamically based on actual line lengths.
    // Each page gets as many rules as fit within FILTER_REPLY_BUDGET minus header and hint.
    uint8_t page_start[MAX_FILTER_RULES + 1];  // start index into indexes[] for each page
    uint8_t num_pages = 0;
    page_start[0] = 0;

    {
        uint8_t i = 0;
        while (i < total) {
            // Available budget for rule lines on this page
            int budget = FILTER_REPLY_BUDGET - FILTER_REPLY_HEADER_MAX - FILTER_REPLY_HINT_LEN;
            uint8_t page_end = i;

            while (page_end < total) {
                char line_buf[80];
                int line_len = formatRuleLine(_rules[indexes[page_end]], indexes[page_end],
                                              line_buf, sizeof(line_buf));
                if (budget - line_len < 0) break;  // doesn't fit
                budget -= line_len;
                page_end++;
            }

            // Safety: always advance at least one rule to avoid infinite loop
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

    // Write header
    int pos;
    if (num_pages > 1) {
        pos = snprintf(reply, FILTER_REPLY_BUDGET, "mode:%s rules:%d/%d p%d/%d\n",
            mode_str, total, MAX_FILTER_RULES, page + 1, num_pages);
    } else {
        pos = snprintf(reply, FILTER_REPLY_BUDGET, "mode:%s rules:%d/%d\n",
            mode_str, total, MAX_FILTER_RULES);
    }

    if (total == 0) {
        snprintf(reply + pos, FILTER_REPLY_BUDGET - pos, "(no rules)");
        return;
    }

    // Write rule lines for this page
    uint8_t start = page_start[page];
    uint8_t end   = page_start[page + 1];

    for (uint8_t i = start; i < end; i++) {
        char line_buf[80];
        formatRuleLine(_rules[indexes[i]], indexes[i], line_buf, sizeof(line_buf));
        pos += snprintf(reply + pos, FILTER_REPLY_BUDGET - pos, "%s", line_buf);
    }

    // Hint if more pages follow
    if (page + 1 < num_pages) {
        snprintf(reply + pos, FILTER_REPLY_BUDGET - pos, "-> filter list %d", page + 1);
    }
}

// ---------------------------------------------------------------------------
// CLI dispatch
// ---------------------------------------------------------------------------

void ChannelFilter::handleCommand(const char* args, char* reply, FILESYSTEM& fs) {
    FilterParseResult res = parseFilterCommand(args);

    if (res.error != FilterParseError::OK) {
        snprintf(reply, 80, "%s", filterParseErrorStr(res.error));
        return;
    }

    switch (res.command) {
        case FilterCommand::ADD: {
            int slot = _firstFreeSlot();
            if (slot < 0) {
                snprintf(reply, 80, "Err - rules full (max %d)", MAX_FILTER_RULES);
                return;
            }
            _rules[slot] = res.rule;
            save(fs);
            snprintf(reply, 80, "OK - rule %d added", slot);
            break;
        }

        case FilterCommand::DEL: {
            uint8_t id = res.rule_id;
            if (!_rules[id].in_use) {
                snprintf(reply, 80, "Err - rule %d not in use", id);
                return;
            }
            memset(&_rules[id], 0, sizeof(FilterRule));
            save(fs);
            snprintf(reply, 80, "OK - rule %d deleted", id);
            break;
        }

        case FilterCommand::DISABLE: {
            uint8_t id = res.rule_id;
            if (!_rules[id].in_use) {
                snprintf(reply, 80, "Err - rule %d not in use", id);
                return;
            }
            _rules[id].enabled = false;
            save(fs);
            snprintf(reply, 80, "OK - rule %d disabled", id);
            break;
        }

        case FilterCommand::ENABLE: {
            uint8_t id = res.rule_id;
            if (!_rules[id].in_use) {
                snprintf(reply, 80, "Err - rule %d not in use", id);
                return;
            }
            _rules[id].enabled = true;
            save(fs);
            snprintf(reply, 80, "OK - rule %d enabled", id);
            break;
        }

        case FilterCommand::LIST:
            _listRules(reply, res.rule_id);
            break;

        case FilterCommand::CLEAR:
            memset(_rules, 0, sizeof(_rules));
            save(fs);
            snprintf(reply, 80, "OK - all rules cleared");
            break;

        case FilterCommand::MODE:
            _mode = res.mode;
            save(fs);
            snprintf(reply, 80, "OK - mode: %s", res.mode == FilterMode::DROP ? "drop" : "allow");
            break;
    }
}