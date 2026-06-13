#pragma once

#include <stdint.h>

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------

#define MAX_FILTER_RULES          8
#define MAX_PATH_HASHES_PER_RULE  4
#define MAX_PATH_HASH_SIZE        3   // 1, 2 or 3 bytes per hash

// ---------------------------------------------------------------------------
// Enums
// ---------------------------------------------------------------------------

enum class FilterAction : uint8_t {
    DROP  = 0,
    ALLOW = 1,
};

enum class FilterField : uint8_t {
    ROUTE    = 0,   // getRouteType()        — ROUTE_TYPE_* values
    TYPE     = 1,   // getPayloadType()      — PAYLOAD_TYPE_* values
    HOPS     = 2,   // getPathHashCount()    — number of hops
    PATHSIZE = 3,   // getPathHashSize()     — bytes per path hash (1-3)
    PATH     = 4,   // last hop in path      — OR-match against hash list
    CHANNEL  = 5,   // payload[0]            — channel hash (GRP_TXT / GRP_DATA)
    SNR      = 6,   // packet->_snr          — stored in quarter-dB, compared in whole dB
    RSSI     = 7,   // passed in at eval     — dBm
};

enum class FilterOp : uint8_t {
    EQ  = 0,   // equal
    NEQ = 1,   // not equal
    GT  = 2,   // greater than
    LT  = 3,   // less than
};

// ---------------------------------------------------------------------------
// Default policy when no rule matches
// ---------------------------------------------------------------------------

enum class FilterMode : uint8_t {
    ALLOW = 0,   // default-allow (blacklist mode)
    DROP  = 1,   // default-drop  (whitelist mode)
};

// ---------------------------------------------------------------------------
// Rule struct
// ---------------------------------------------------------------------------

// Sentinel value meaning "no AND condition"
#define FILTER_FIELD_NONE  0xFF

struct FilterRule {
    FilterAction action;
    FilterField  field;
    FilterOp     op;

    // Scalar comparison value.
    // SNR  : stored in quarter-dB (matches packet->_snr units), parser converts from whole dB
    // RSSI : stored in dBm (int16_t)
    // All other fields: uint8_t cast to int16_t
    int16_t value;

    // Optional AND condition — active only when and_field != FILTER_FIELD_NONE.
    // PATH field is not supported as an AND condition.
    uint8_t  and_field;  // raw uint8_t so FILTER_FIELD_NONE (0xFF) fits without casting
    FilterOp and_op;
    int16_t  and_value;

    // PATH field only: list of hashes for OR-match against the last hop in path.
    // Unused slots are zero-filled.
    uint8_t path_hashes[MAX_PATH_HASHES_PER_RULE][MAX_PATH_HASH_SIZE];
    uint8_t path_hash_len;    // bytes per hash (1, 2 or 3) — same for all hashes in this rule
    uint8_t path_hash_count;  // number of valid hashes in path_hashes (1..MAX_PATH_HASHES_PER_RULE)

    bool enabled;  // false = rule is defined but temporarily inactive
    bool in_use;   // false = slot is empty
};