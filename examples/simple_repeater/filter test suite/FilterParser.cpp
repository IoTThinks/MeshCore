#include "FilterParser.h"
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

// ---------------------------------------------------------------------------
// Internal tokenizer
// ---------------------------------------------------------------------------

// Maximum token length (no single token should exceed this)
#define MAX_TOKEN_LEN  16

struct Tokenizer {
    const char* pos;   // current position in input string
};

// Copy the next whitespace-delimited token into 'out' (null-terminated).
// Returns false if no more tokens are available.
static bool nextToken(Tokenizer& tz, char out[MAX_TOKEN_LEN + 1]) {
    // Skip leading whitespace
    while (*tz.pos == ' ' || *tz.pos == '\t') tz.pos++;

    if (*tz.pos == '\0') return false;

    int i = 0;
    while (*tz.pos != '\0' && *tz.pos != ' ' && *tz.pos != '\t') {
        if (i < MAX_TOKEN_LEN) {
            out[i++] = (char)tolower((unsigned char)*tz.pos);
        }
        tz.pos++;
    }
    out[i] = '\0';
    return true;
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
        case FilterField::CHANNEL: {
            uint8_t bytes[MAX_PATH_HASH_SIZE];
            uint8_t len = parseHexBytes(tok, bytes);
            if (len == 1) { out = bytes[0]; return true; }
            char* end;
            long v = strtol(tok, &end, 0);
            if (*end != '\0' || v < 0 || v > 255) return false;
            out = (int16_t)v;
            return true;
        }
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

    // --- operator ---
    if (!nextToken(tz, tok)) { result.error = FilterParseError::MISSING_TOKEN; return result; }
    if (!parseOp(tok, result.rule.op)) { result.error = FilterParseError::UNKNOWN_OP; return result; }

    // --- value (field-specific) ---
    if (result.rule.field == FilterField::PATH) {
        // PATH: one or more hex hash tokens (OR-list), stops at "and" or end of input
        uint8_t count   = 0;
        uint8_t hashlen = 0;

        while (peekToken(tz, tok)) {
            // Stop consuming hashes when we see the "and" keyword
            if (strcmp(tok, "and") == 0) break;

            nextToken(tz, tok);  // consume

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

    } else {
        // Scalar field
        if (!nextToken(tz, tok)) { result.error = FilterParseError::MISSING_TOKEN; return result; }
        if (!parseScalarValue(tok, result.rule.field, result.rule.value)) {
            result.error = FilterParseError::UNKNOWN_VALUE;
            return result;
        }
    }

    // --- optional AND condition ---
    if (peekToken(tz, tok) && strcmp(tok, "and") == 0) {
        nextToken(tz, tok);  // consume "and"

        // AND field
        if (!nextToken(tz, tok)) { result.error = FilterParseError::MISSING_TOKEN; return result; }
        FilterField and_field;
        if (!parseField(tok, and_field)) { result.error = FilterParseError::UNKNOWN_FIELD; return result; }

        // PATH not supported as AND condition
        if (and_field == FilterField::PATH) {
            result.error = FilterParseError::AND_PATH_NOT_ALLOWED;
            return result;
        }

        // Duplicate field not allowed
        if (and_field == result.rule.field) {
            result.error = FilterParseError::AND_DUPLICATE_FIELD;
            return result;
        }

        // AND operator
        FilterOp and_op;
        if (!nextToken(tz, tok)) { result.error = FilterParseError::MISSING_TOKEN; return result; }
        if (!parseOp(tok, and_op)) { result.error = FilterParseError::UNKNOWN_OP; return result; }

        // AND value
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
// Public entry point
// ---------------------------------------------------------------------------

FilterParseResult parseFilterCommand(const char* input) {
    FilterParseResult result;
    memset(&result, 0, sizeof(result));  // zero all fields including rule — safe default for all error paths

    Tokenizer tz = { input };
    char tok[MAX_TOKEN_LEN + 1];

    if (!nextToken(tz, tok)) {
        result.error = FilterParseError::MISSING_TOKEN;
        return result;
    }

    // --- route to sub-command ---
    if (strcmp(tok, "add") == 0) {
        return parseAddCommand(tz);
    }

    if (strcmp(tok, "list") == 0) {
        result.error   = FilterParseError::OK;
        result.command = FilterCommand::LIST;
        result.rule_id = 0;  // default page 0
        // Optional page number: "filter list 1"
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
        case FilterParseError::AND_PATH_NOT_ALLOWED: return "Err - path field not supported as AND condition";
        case FilterParseError::AND_DUPLICATE_FIELD:  return "Err - AND condition cannot use same field as primary";
        default:                                  return "Err - unknown error";
    }
}
