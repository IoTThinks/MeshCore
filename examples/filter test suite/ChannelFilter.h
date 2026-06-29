#pragma once

// NOTE: This header relies on FILESYSTEM being defined before inclusion.
// MyMesh.h includes the platform-specific filesystem headers before including
// this file, so FILESYSTEM is always defined in that context.

#include "FilterRule.h"
#include "FilterParser.h"


// Persistence file path
#define FILTER_RULES_FILE  "/filter_rules.bin"

// ---------------------------------------------------------------------------
// ChannelFilter
// ---------------------------------------------------------------------------

class ChannelFilter {
public:
    ChannelFilter();

    // --- Lifecycle ----------------------------------------------------------

    // Load rules and mode from filesystem. Call once at startup.
    void load(FILESYSTEM& fs);

    // Save rules and mode to filesystem.
    void save(FILESYSTEM& fs) const;

    // --- Evaluation ---------------------------------------------------------

    // Evaluate all active rules against a received packet.
    // 'rssi' is passed separately as it lives in the radio driver, not in Packet.
    // Returns true if the packet should be DROPPED, false if it should pass.
    bool evaluate(const mesh::Packet* pkt, int16_t rssi) const;

    // --- CLI dispatch -------------------------------------------------------

    // Handle a "filter ..." command string (everything after "filter ").
    // Writes a human-readable result into 'reply' (assumed >= 80 bytes).
    void handleCommand(const char* args, char* reply, FILESYSTEM& fs);

private:
    FilterRule  _rules[MAX_FILTER_RULES];
    FilterMode  _mode;   // default policy when no rule matches

    // --- Rule helpers -------------------------------------------------------

    // Find the first free slot. Returns index or -1 if full.
    int  _firstFreeSlot() const;

    // Evaluate a single rule against a packet + rssi.
    // Returns true if the rule matches.
    bool _ruleMatches(const FilterRule& rule, const mesh::Packet* pkt, int16_t rssi) const;
    bool _evalScalar(FilterField field, FilterOp op, int16_t val,
                     const mesh::Packet* pkt, int16_t rssi) const;

    // --- list command -------------------------------------------------------
    void _listRules(char* reply, uint8_t page) const;
};
