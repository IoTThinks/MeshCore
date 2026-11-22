// PowerSaving variant
// Packet filter engine - used only when built with -D PACKET_FILTER=1
#pragma once

#if PACKET_FILTER

#include <stdint.h>

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------

#define MAX_FILTER_RULES          8
#define MAX_FILTER_PROFILES       2
#define MAX_PATH_HASHES_PER_RULE  4
#define MAX_PATH_HASH_SIZE        3   // 1, 2 or 3 bytes per hash
#define MAX_CHANNEL_HASHES        4   // OR-list for channel field
#define MAX_SOURCE_HASHES         4   // OR-list for source AND condition
#define MAX_SCOPE_NAME_LEN        16  // max length of scope/region name

// Sanity check — rule keys are ra..r? so MAX_FILTER_RULES must fit in a-z
static_assert(MAX_FILTER_RULES <= 26, "MAX_FILTER_RULES must be <= 26");

// ---------------------------------------------------------------------------
// Enums
// ---------------------------------------------------------------------------

enum class FilterAction : uint8_t {
    DROP  = 0,
    ALLOW = 1,
};

enum class FilterField : uint8_t {
    ROUTE    = 0,   // getRouteType()
    TYPE     = 1,   // getPayloadType()
    HOPS     = 2,   // getPathHashCount()
    PATHSIZE = 3,   // getPathHashSize()
    PATH     = 4,   // last hop in path — OR-match
    CHANNEL  = 5,   // payload[0] — channel hash (GRP_TXT / GRP_DATA)
    SNR      = 6,   // packet->_snr in quarter-dB
    RSSI     = 7,   // dBm
    REPEATER = 8,   // matches management payload types
    SCOPE    = 9,   // transport_codes matched against named region
};

enum class FilterOp : uint8_t {
    EQ  = 0,
    NEQ = 1,
    GT  = 2,
    LT  = 3,
};

enum class FilterMode : uint8_t {
    ALLOW = 0,
    DROP  = 1,
};

// ---------------------------------------------------------------------------
// Sentinel
// ---------------------------------------------------------------------------

#define FILTER_FIELD_NONE  0xFF

// ---------------------------------------------------------------------------
// Rule struct
// ---------------------------------------------------------------------------

struct FilterRule {
    FilterAction action;
    FilterField  field;
    FilterOp     op;

    // Scalar comparison value (SNR in quarter-dB, RSSI in dBm, others uint8)
    int16_t value;

    // Standard AND condition (scalar fields only — not PATH, CHANNEL, REPEATER, SCOPE)
    uint8_t  and_field;   // FILTER_FIELD_NONE = no AND condition
    FilterOp and_op;
    int16_t  and_value;

    // PATH field: OR-list of last-hop hashes
    uint8_t path_hashes[MAX_PATH_HASHES_PER_RULE][MAX_PATH_HASH_SIZE];
    uint8_t path_hash_len;
    uint8_t path_hash_count;

    // CHANNEL field: OR-list of channel hash bytes
    uint8_t channel_hashes[MAX_CHANNEL_HASHES];
    uint8_t channel_hash_count;

    // REPEATER field: management payload type list (hard-coded, no value stored)

    // SOURCE AND condition — valid with any primary field.
    // Matches src_hash (payload[1]) for txt/req/resp/anonreq/path.
    // Auto-true (skipped) for advert/ack/ctrl.
    // source_hash_count == 0 means no source filter.
    uint8_t source_hashes[MAX_SOURCE_HASHES];
    uint8_t source_hash_count;

    // SCOPE AND condition — valid with any primary field.
    // Matches packet's region against named region in region_map.
    // scope_name[0] == '\0' means no scope filter.
    char scope_name[MAX_SCOPE_NAME_LEN];

    bool enabled;
    bool in_use;
};

#endif // PACKET_FILTER
