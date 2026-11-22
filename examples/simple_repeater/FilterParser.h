// PowerSaving variant
// Packet filter engine - used only when built with -D PACKET_FILTER=1
#pragma once

#if PACKET_FILTER

#include "FilterRule.h"

// ---------------------------------------------------------------------------
// Parse errors
// ---------------------------------------------------------------------------

enum class FilterParseError : uint8_t {
    OK = 0,
    UNKNOWN_COMMAND,
    UNKNOWN_ACTION,
    UNKNOWN_FIELD,
    UNKNOWN_OP,
    UNKNOWN_VALUE,
    MISSING_TOKEN,
    INVALID_RULE_ID,
    INVALID_HEX,
    TOO_MANY_HASHES,
    HASH_SIZE_MISMATCH,
    UNKNOWN_MODE,
    AND_PATH_NOT_ALLOWED,
    AND_DUPLICATE_FIELD,
    AND_CHANNEL_NOT_ALLOWED,
    TOO_MANY_CHANNELS,
    INVALID_PROFILE,
    TOO_MANY_SOURCES,
    SCOPE_NAME_TOO_LONG,
};

// ---------------------------------------------------------------------------
// Commands
// ---------------------------------------------------------------------------

enum class FilterCommand : uint8_t {
    ADD,
    DEL,
    LIST,
    DISABLE,
    ENABLE,
    CLEAR,
    MODE,
    SET_PROFILE,
    GET_PROFILE,
};

// ---------------------------------------------------------------------------
// Parse result
// ---------------------------------------------------------------------------

struct FilterParseResult {
    FilterParseError error;
    FilterCommand    command;
    FilterRule       rule;
    uint8_t          rule_id;
    FilterMode       mode;
    uint8_t          target_profile;
};

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

FilterParseResult parseFilterCommand(const char* input);
const char* filterParseErrorStr(FilterParseError err);

#endif // PACKET_FILTER
