// PowerSaving variant
// Packet filter engine - compiled only when built with -D PACKET_FILTER=1
#if PACKET_FILTER

#include "FilterParser.h"
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <stdio.h>

// ---------------------------------------------------------------------------
// Internal tokenizer
// ---------------------------------------------------------------------------

// Maximum token length (no single token should exceed this)
#define MAX_TOKEN_LEN  16

struct Tokenizer {
    const char* pos;   // current position in input string
};

// Copy the next whitespace-delimited token into 'out' (null-terminated).
// 'lowercase' controls case folding — keywords are folded, names are not.
// Returns false if no more tokens are available.
static bool nextTokenEx(Tokenizer& tz, char out[MAX_TOKEN_LEN + 1], bool lowercase) {
    // Skip leading whitespace
    while (*tz.pos == ' ' || *tz.pos == '\t') tz.pos++;

    if (*tz.pos == '\0') return false;

    int i = 0;
    while (*tz.pos != '\0' && *tz.pos != ' ' && *tz.pos != '\t') {
        if (i < MAX_TOKEN_LEN) {
            out[i++] = lowercase ? (char)tolower((unsigned char)*tz.pos) : *tz.pos;
        }
        tz.pos++;
    }
    out[i] = '\0';
    return true;
}

// Keyword token — case-folded so commands are case-insensitive.
static bool nextToken(Tokenizer& tz, char out[MAX_TOKEN_LEN + 1]) {
    return nextTokenEx(tz, out, true);
}

// Name token — case preserved (region/scope names are case-sensitive).
static bool nextTokenRaw(Tokenizer& tz, char out[MAX_TOKEN_LEN + 1]) {
    return nextTokenEx(tz, out, false);
}

// Peek at next token without advancing position.
static bool peekToken(Tokenizer tz, char out[MAX_TOKEN_LEN + 1]) {
    return nextToken(tz, out);  // tz passed by value — copy is intentional
}

// ---------------------------------------------------------------------------
// Token → enum helpers
// ---------------------------------------------------------------------------

static bool parseAction(const char* tok, FilterAction& out) {
    if (strcmp(tok, "drop")  == 0) { out = FilterAction::DROP;  return true; }
    if (strcmp(tok, "allow") == 0) { out = FilterAction::ALLOW; return true; }
    return false;
}

static bool parseField(const char* tok, FilterField& out) {
    if (strcmp(tok, "route")    == 0) { out = FilterField::ROUTE;    return true; }
    if (strcmp(tok, "payload")  == 0) { out = FilterField::TYPE;     return true; }
    if (strcmp(tok, "hops")     == 0) { out = FilterField::HOPS;     return true; }
    if (strcmp(tok, "pathsize") == 0) { out = FilterField::PATHSIZE; return true; }
    if (strcmp(tok, "path")     == 0) { out = FilterField::PATH;     return true; }
    if (strcmp(tok, "channel")  == 0) { out = FilterField::CHANNEL;  return true; }
    if (strcmp(tok, "snr")      == 0) { out = FilterField::SNR;      return true; }
    if (strcmp(tok, "rssi")     == 0) { out = FilterField::RSSI;     return true; }
    if (strcmp(tok, "repeater") == 0) { out = FilterField::REPEATER; return true; }
    if (strcmp(tok, "scope")    == 0) { out = FilterField::SCOPE;    return true; }
    return false;
}

static bool parseOp(const char* tok, FilterOp& out) {
    if (strcmp(tok, "eq")  == 0) { out = FilterOp::EQ;  return true; }
    if (strcmp(tok, "neq") == 0) { out = FilterOp::NEQ; return true; }
    if (strcmp(tok, "gt")  == 0) { out = FilterOp::GT;  return true; }
    if (strcmp(tok, "lt")  == 0) { out = FilterOp::LT;  return true; }
    return false;
}

static bool parseMode(const char* tok, FilterMode& out) {
    if (strcmp(tok, "allow") == 0) { out = FilterMode::ALLOW; return true; }
    if (strcmp(tok, "drop")  == 0) { out = FilterMode::DROP;  return true; }
    return false;
}

// ---------------------------------------------------------------------------
// Value parsers per field
// ---------------------------------------------------------------------------

// Parse ROUTE value token → uint8_t ROUTE_TYPE_* equivalent
static bool parseRouteValue(const char* tok, int16_t& out) {
    if (strcmp(tok, "tflood")  == 0) { out = 0x00; return true; }  // ROUTE_TYPE_TRANSPORT_FLOOD
    if (strcmp(tok, "flood")   == 0) { out = 0x01; return true; }  // ROUTE_TYPE_FLOOD
    if (strcmp(tok, "direct")  == 0) { out = 0x02; return true; }  // ROUTE_TYPE_DIRECT
    if (strcmp(tok, "tdirect") == 0) { out = 0x03; return true; }  // ROUTE_TYPE_TRANSPORT_DIRECT
    return false;
}

// Parse PAYLOAD_TYPE value token → uint8_t PAYLOAD_TYPE_* equivalent
static bool parseTypeValue(const char* tok, int16_t& out) {
    if (strcmp(tok, "req")     == 0) { out = 0x00; return true; }
    if (strcmp(tok, "resp")    == 0) { out = 0x01; return true; }
    if (strcmp(tok, "txt")     == 0) { out = 0x02; return true; }
    if (strcmp(tok, "ack")     == 0) { out = 0x03; return true; }
    if (strcmp(tok, "advert")  == 0) { out = 0x04; return true; }
    if (strcmp(tok, "grptxt")  == 0) { out = 0x05; return true; }
    if (strcmp(tok, "grpdata") == 0) { out = 0x06; return true; }
    if (strcmp(tok, "anonreq") == 0) { out = 0x07; return true; }
    if (strcmp(tok, "path")    == 0) { out = 0x08; return true; }
    if (strcmp(tok, "trace")   == 0) { out = 0x09; return true; }
    if (strcmp(tok, "multi")   == 0) { out = 0x0A; return true; }
    if (strcmp(tok, "ctrl")    == 0) { out = 0x0B; return true; }
    if (strcmp(tok, "raw")     == 0) { out = 0x0F; return true; }
    // Also accept raw numeric values (decimal or hex)
    char* end;
    long v = strtol(tok, &end, 0);
    if (*end == '\0' && v >= 0 && v <= 0x0F) { out = (int16_t)v; return true; }
    return false;
}

// Parse a hex string (with or without 0x prefix) into up to MAX_PATH_HASH_SIZE bytes.
// Returns number of bytes written, or 0 on failure.
static uint8_t parseHexBytes(const char* tok, uint8_t* out) {
    // Skip optional 0x / 0X prefix — check both chars exist first
    if (tok[0] == '0' && tok[1] != '\0' && (tok[1] == 'x' || tok[1] == 'X')) tok += 2;

    size_t hexlen = strlen(tok);
    if (hexlen == 0 || hexlen > (MAX_PATH_HASH_SIZE * 2) || (hexlen & 1) != 0) return 0;

    for (size_t i = 0; i < hexlen; i += 2) {
        char hi = tok[i];
        char lo = tok[i + 1];

        auto hexdig = [](char c) -> int {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'a' && c <= 'f') return c - 'a' + 10;
            if (c >= 'A' && c <= 'F') return c - 'A' + 10;
            return -1;
        };

        int h = hexdig(hi);
        int l = hexdig(lo);
        if (h < 0 || l < 0) return 0;

        out[i / 2] = (uint8_t)((h << 4) | l);
    }
    return (uint8_t)(hexlen / 2);
}

// Parse a scalar value token for a given field into out.
// Returns false if the token is not valid for the field.
static bool parseScalarValue(const char* tok, FilterField field, int16_t& out) {
    switch (field) {
        case FilterField::ROUTE:
            return parseRouteValue(tok, out);
        case FilterField::TYPE:
            return parseTypeValue(tok, out);
        case FilterField::HOPS:
        case FilterField::PATHSIZE: {
            char* end;
            long v = strtol(tok, &end, 0);
            if (*end != '\0' || v < 0 || v > 255) return false;
            out = (int16_t)v;
            return true;
        }
        case FilterField::CHANNEL:
            // Channel OR-list is handled separately in parseAddCommand.
            return false;
        case FilterField::REPEATER:
        case FilterField::SCOPE:
            // These fields have no scalar value — handled separately.
            return false;
        case FilterField::SNR: {
            char* end;
            long v = strtol(tok, &end, 0);
            if (*end != '\0' || v < -128 || v > 127) return false;
            out = (int16_t)(v * 4);  // store as quarter-dB
            return true;
        }
        case FilterField::RSSI: {
            char* end;
            long v = strtol(tok, &end, 0);
            if (*end != '\0' || v < -32768 || v > 32767) return false;
            out = (int16_t)v;
            return true;
        }
        default:
            return false;
    }
}

static FilterParseResult parseAddCommand(Tokenizer& tz) {
    FilterParseResult result;
    result.error   = FilterParseError::OK;
    result.command = FilterCommand::ADD;
    memset(&result.rule, 0, sizeof(FilterRule));
    result.rule.enabled   = false;  // rules are added disabled — use 'filter enable <id>' to activate
    result.rule.in_use    = true;
    result.rule.and_field = FILTER_FIELD_NONE;  // no AND condition by default

    char tok[MAX_TOKEN_LEN + 1];

    // --- action ---
    if (!nextToken(tz, tok)) { result.error = FilterParseError::MISSING_TOKEN; return result; }
    if (!parseAction(tok, result.rule.action)) { result.error = FilterParseError::UNKNOWN_ACTION; return result; }

    // --- field ---
    if (!nextToken(tz, tok)) { result.error = FilterParseError::MISSING_TOKEN; return result; }
    if (!parseField(tok, result.rule.field)) { result.error = FilterParseError::UNKNOWN_FIELD; return result; }

    // --- value (field-specific) ---
    // REPEATER and SCOPE take no operator — handle them before attempting to read op
    if (result.rule.field == FilterField::REPEATER) {
        // REPEATER: no op or value — fall through to AND parsing below
        result.rule.op    = FilterOp::EQ;
        result.rule.value = 0;
        goto parse_and;

    } else if (result.rule.field == FilterField::SCOPE) {
        // SCOPE: no op — just "filter add allow scope <name>"
        result.rule.op    = FilterOp::EQ;
        result.rule.value = 0;

        // Region names are case-sensitive — read without case folding
        if (!nextTokenRaw(tz, tok)) { result.error = FilterParseError::MISSING_TOKEN; return result; }
        if (strlen(tok) >= MAX_SCOPE_NAME_LEN) {
            result.error = FilterParseError::SCOPE_NAME_TOO_LONG;
            return result;
        }
        strncpy(result.rule.scope_name, tok, MAX_SCOPE_NAME_LEN - 1);
        result.rule.scope_name[MAX_SCOPE_NAME_LEN - 1] = '\0';
        goto parse_and;

    } else {
        // All other fields require an operator
        if (!nextToken(tz, tok)) { result.error = FilterParseError::MISSING_TOKEN; return result; }
        if (!parseOp(tok, result.rule.op)) { result.error = FilterParseError::UNKNOWN_OP; return result; }
    }

    if (result.rule.field == FilterField::PATH) {
        // PATH: one or more hex hash tokens (OR-list), stops at "and" or end of input
        uint8_t count   = 0;
        uint8_t hashlen = 0;

        while (peekToken(tz, tok)) {
            if (strcmp(tok, "and") == 0) break;
            nextToken(tz, tok);

            if (count >= MAX_PATH_HASHES_PER_RULE) {
                result.error = FilterParseError::TOO_MANY_HASHES;
                return result;
            }

            uint8_t bytes[MAX_PATH_HASH_SIZE];
            uint8_t len = parseHexBytes(tok, bytes);
            if (len == 0) {
                result.error = FilterParseError::INVALID_HEX;
                return result;
            }

            if (hashlen == 0) {
                hashlen = len;
            } else if (len != hashlen) {
                result.error = FilterParseError::HASH_SIZE_MISMATCH;
                return result;
            }

            memcpy(result.rule.path_hashes[count], bytes, len);
            count++;
        }

        if (count == 0) {
            result.error = FilterParseError::MISSING_TOKEN;
            return result;
        }

        result.rule.path_hash_len   = hashlen;
        result.rule.path_hash_count = count;

    } else if (result.rule.field == FilterField::CHANNEL) {
        // CHANNEL: one or more space-separated hex byte tokens (OR-list)
        // AND condition not supported when channel is primary field
        uint8_t count = 0;

        while (peekToken(tz, tok)) {
            // Stop consuming hashes when we see the "and" keyword
            if (strcmp(tok, "and") == 0) break;

            nextToken(tz, tok);

            if (count >= MAX_CHANNEL_HASHES) {
                result.error = FilterParseError::TOO_MANY_CHANNELS;
                return result;
            }

            // Accept 0xNN or NN (exactly 1 byte)
            uint8_t bytes[MAX_PATH_HASH_SIZE];
            uint8_t len = parseHexBytes(tok, bytes);
            if (len == 1) {
                result.rule.channel_hashes[count++] = bytes[0];
            } else {
                // Try plain decimal
                char* end;
                long v = strtol(tok, &end, 0);
                if (*end != '\0' || v < 0 || v > 255) {
                    result.error = FilterParseError::INVALID_HEX;
                    return result;
                }
                result.rule.channel_hashes[count++] = (uint8_t)v;
            }
        }

        if (count == 0) {
            result.error = FilterParseError::MISSING_TOKEN;
            return result;
        }

        result.rule.channel_hash_count = count;
        // Channel OR-list parsed — fall through to AND condition parsing below

    } else {
        // Scalar field
        if (!nextToken(tz, tok)) { result.error = FilterParseError::MISSING_TOKEN; return result; }
        if (!parseScalarValue(tok, result.rule.field, result.rule.value)) {
            result.error = FilterParseError::UNKNOWN_VALUE;
            return result;
        }
    }

    parse_and:
    // --- optional AND conditions ---
    // Supports: "and <field> <op> <value>", "and source <hashes>", "and scope <name>"
    // Multiple AND keywords allowed: one scalar AND + source + scope (any combination)
    while (peekToken(tz, tok) && strcmp(tok, "and") == 0) {
        nextToken(tz, tok);  // consume "and"

        if (!nextToken(tz, tok)) { result.error = FilterParseError::MISSING_TOKEN; return result; }

        // --- "and source <hash> [<hash> ...]" ---
        if (strcmp(tok, "source") == 0) {
            uint8_t count = result.rule.source_hash_count;
            while (peekToken(tz, tok) && strcmp(tok, "and") != 0) {
                nextToken(tz, tok);
                if (count >= MAX_SOURCE_HASHES) {
                    result.error = FilterParseError::TOO_MANY_SOURCES;
                    return result;
                }
                uint8_t bytes[MAX_PATH_HASH_SIZE];
                uint8_t len = parseHexBytes(tok, bytes);
                if (len != 1) {
                    result.error = FilterParseError::INVALID_HEX;
                    return result;
                }
                result.rule.source_hashes[count++] = bytes[0];
            }
            if (count == 0) { result.error = FilterParseError::MISSING_TOKEN; return result; }
            result.rule.source_hash_count = count;
            continue;
        }

        // --- "and scope <name>" ---
        if (strcmp(tok, "scope") == 0) {
            // Region names are case-sensitive — read without case folding
            if (!nextTokenRaw(tz, tok)) { result.error = FilterParseError::MISSING_TOKEN; return result; }
            if (strlen(tok) >= MAX_SCOPE_NAME_LEN) {
                result.error = FilterParseError::SCOPE_NAME_TOO_LONG;
                return result;
            }
            strncpy(result.rule.scope_name, tok, MAX_SCOPE_NAME_LEN - 1);
            result.rule.scope_name[MAX_SCOPE_NAME_LEN - 1] = '\0';
            continue;
        }

        // --- "and <field> <op> <value>" (scalar AND — only one allowed) ---
        FilterField and_field;
        if (!parseField(tok, and_field)) { result.error = FilterParseError::UNKNOWN_FIELD; return result; }

        if (and_field == result.rule.field) {
            result.error = FilterParseError::AND_DUPLICATE_FIELD;
            return result;
        }
        if (and_field == FilterField::PATH || and_field == FilterField::CHANNEL ||
            and_field == FilterField::REPEATER || and_field == FilterField::SCOPE) {
            result.error = FilterParseError::AND_PATH_NOT_ALLOWED;
            return result;
        }

        FilterOp and_op;
        if (!nextToken(tz, tok)) { result.error = FilterParseError::MISSING_TOKEN; return result; }
        if (!parseOp(tok, and_op)) { result.error = FilterParseError::UNKNOWN_OP; return result; }

        int16_t and_value = 0;
        if (!nextToken(tz, tok)) { result.error = FilterParseError::MISSING_TOKEN; return result; }
        if (!parseScalarValue(tok, and_field, and_value)) {
            result.error = FilterParseError::UNKNOWN_VALUE;
            return result;
        }

        result.rule.and_field = (uint8_t)and_field;
        result.rule.and_op    = and_op;
        result.rule.and_value = and_value;
    }

    return result;
}

// ---------------------------------------------------------------------------
// Internal helper — parse a sub-command given a tokenizer already positioned
// past any profile prefix. Sets result.target_profile before calling.
// ---------------------------------------------------------------------------

static FilterParseResult parseSubCommand(Tokenizer& tz, uint8_t target_profile) {
    FilterParseResult result;
    memset(&result, 0, sizeof(result));
    result.target_profile = target_profile;

    char tok[MAX_TOKEN_LEN + 1];

    if (!nextToken(tz, tok)) {
        result.error = FilterParseError::MISSING_TOKEN;
        return result;
    }

    if (strcmp(tok, "add") == 0) {
        result = parseAddCommand(tz);
        result.target_profile = target_profile;
        return result;
    }

    if (strcmp(tok, "list") == 0) {
        result.error   = FilterParseError::OK;
        result.command = FilterCommand::LIST;
        result.rule_id = 0;  // default page 0
        if (peekToken(tz, tok)) {
            nextToken(tz, tok);
            char* end;
            long page = strtol(tok, &end, 10);
            if (*end != '\0' || page < 0) {
                result.error = FilterParseError::INVALID_RULE_ID;
                return result;
            }
            result.rule_id = (uint8_t)page;
        }
        return result;
    }

    if (strcmp(tok, "clear") == 0) {
        result.error   = FilterParseError::OK;
        result.command = FilterCommand::CLEAR;
        return result;
    }

    if (strcmp(tok, "del") == 0 || strcmp(tok, "disable") == 0 || strcmp(tok, "enable") == 0) {
        FilterCommand cmd = (strcmp(tok, "del")     == 0) ? FilterCommand::DEL     :
                            (strcmp(tok, "disable") == 0) ? FilterCommand::DISABLE :
                                                            FilterCommand::ENABLE;
        if (!nextToken(tz, tok)) {
            result.error = FilterParseError::MISSING_TOKEN;
            return result;
        }
        char* end;
        long id = strtol(tok, &end, 10);
        if (*end != '\0' || id < 0 || id >= MAX_FILTER_RULES) {
            result.error = FilterParseError::INVALID_RULE_ID;
            return result;
        }
        result.error   = FilterParseError::OK;
        result.command = cmd;
        result.rule_id = (uint8_t)id;
        return result;
    }

    if (strcmp(tok, "mode") == 0) {
        if (!nextToken(tz, tok)) {
            result.error = FilterParseError::MISSING_TOKEN;
            return result;
        }
        if (!parseMode(tok, result.mode)) {
            result.error = FilterParseError::UNKNOWN_MODE;
            return result;
        }
        result.error   = FilterParseError::OK;
        result.command = FilterCommand::MODE;
        return result;
    }

    result.error = FilterParseError::UNKNOWN_COMMAND;
    return result;
}

// ---------------------------------------------------------------------------
// Public entry point
// ---------------------------------------------------------------------------

FilterParseResult parseFilterCommand(const char* input) {
    FilterParseResult result;
    memset(&result, 0, sizeof(result));  // zero all fields — safe default for all error paths

    Tokenizer tz = { input };
    char tok[MAX_TOKEN_LEN + 1];

    if (!nextToken(tz, tok)) {
        result.error = FilterParseError::MISSING_TOKEN;
        return result;
    }

    // --- profile prefix: "filter 1 ..." or "filter 2 ..." ---
    if (strcmp(tok, "1") == 0 || strcmp(tok, "2") == 0) {
        uint8_t profile = (uint8_t)(tok[0] - '0');
        return parseSubCommand(tz, profile);
    }

    // --- "filter set profile <num>" ---
    if (strcmp(tok, "set") == 0) {
        if (!nextToken(tz, tok)) { result.error = FilterParseError::MISSING_TOKEN; return result; }
        if (strcmp(tok, "profile") != 0) { result.error = FilterParseError::UNKNOWN_COMMAND; return result; }
        if (!nextToken(tz, tok)) { result.error = FilterParseError::MISSING_TOKEN; return result; }
        char* end;
        long p = strtol(tok, &end, 10);
        if (*end != '\0' || p < 1 || p > MAX_FILTER_PROFILES) {
            result.error = FilterParseError::INVALID_PROFILE;
            return result;
        }
        result.error          = FilterParseError::OK;
        result.command        = FilterCommand::SET_PROFILE;
        result.target_profile = (uint8_t)p;
        return result;
    }

    // --- "filter profile" — show active profile ---
    if (strcmp(tok, "profile") == 0) {
        result.error   = FilterParseError::OK;
        result.command = FilterCommand::GET_PROFILE;
        return result;
    }

    // Reconstruct input with tok prepended so parseSubCommand sees the full command.
    // 256 bytes is a safe upper bound for any CLI command string.
    char reconstructed[MAX_TOKEN_LEN + 256];
    snprintf(reconstructed, sizeof(reconstructed), "%s %s", tok, tz.pos);
    Tokenizer tz2 = { reconstructed };
    return parseSubCommand(tz2, 0);
}

// ---------------------------------------------------------------------------
// Error string helper
// ---------------------------------------------------------------------------

const char* filterParseErrorStr(FilterParseError err) {
    switch (err) {
        case FilterParseError::OK:                return "OK";
        case FilterParseError::UNKNOWN_COMMAND:   return "Err - unknown command";
        case FilterParseError::UNKNOWN_ACTION:    return "Err - unknown action (use: drop, allow)";
        case FilterParseError::UNKNOWN_FIELD:     return "Err - unknown field (use: route, payload, hops, pathsize, path, channel, snr, rssi)";
        case FilterParseError::UNKNOWN_OP:        return "Err - unknown operator (use: eq, neq, gt, lt)";
        case FilterParseError::UNKNOWN_VALUE:     return "Err - unknown or out-of-range value";
        case FilterParseError::MISSING_TOKEN:     return "Err - missing token";
        case FilterParseError::INVALID_RULE_ID:   return "Err - invalid rule id";
        case FilterParseError::INVALID_HEX:       return "Err - invalid hex value";
        case FilterParseError::TOO_MANY_HASHES:   return "Err - too many path hashes (max 4)";
        case FilterParseError::HASH_SIZE_MISMATCH:return "Err - mixed hash sizes in path rule";
        case FilterParseError::UNKNOWN_MODE:      return "Err - unknown mode (use: allow, drop)";
        case FilterParseError::AND_PATH_NOT_ALLOWED:    return "Err - path field not supported as AND condition";
        case FilterParseError::AND_DUPLICATE_FIELD:     return "Err - AND condition cannot use same field as primary";
        case FilterParseError::AND_CHANNEL_NOT_ALLOWED: return "Err - channel field not supported as AND condition";
        case FilterParseError::TOO_MANY_CHANNELS:       return "Err - too many channel hashes (max 4)";
        case FilterParseError::INVALID_PROFILE:         return "Err - invalid profile (use: 1 or 2)";
        case FilterParseError::TOO_MANY_SOURCES:        return "Err - too many source hashes (max 4)";
        case FilterParseError::SCOPE_NAME_TOO_LONG:     return "Err - scope name too long (max 15 chars)";
        default:                                  return "Err - unknown error";
    }
}

#endif // PACKET_FILTER
