// PowerSaving variant
// Packet filter engine - used only when built with -D PACKET_FILTER=1
#pragma once

#if PACKET_FILTER

// NOTE: FILESYSTEM is defined by helpers/IdentityStore.h (platform-specific macro).
// ConfigSerializer provides the JSON persistence base class.

#include "FilterParser.h"
#include "FilterRule.h"
#include "helpers/IdentityStore.h"   // defines FILESYSTEM

#include <helpers/ConfigSerializer.h>

// Forward declarations — full headers are pulled in by MyMesh.h in .cpp files
namespace mesh { class Packet; }
struct ClientInfo;
class RegionMap;

// Sanity check — rule keys are ra..r? so MAX_FILTER_RULES must fit in a-z
static_assert(MAX_FILTER_RULES <= 26, "MAX_FILTER_RULES must be <= 26 (key suffix is a single letter)");

// JSON persistence file paths
#define FILTER_RULES_FILE      "/filter_rules.json"   // current format (two profiles)
#define FILTER_RULES_FILE_BIN  "/filter_rules.bin"    // legacy binary — migrated on first load

// Legacy binary format magic and minimum version
#define FILTER_FILE_MAGIC      0xFC
#define FILTER_FILE_VERSION    3

// Precomputed rule key table — avoids char[] mutation in structure() loops.
// Defined in ChannelFilter.cpp to avoid multiple-definition issues.
// ra, rb, rc ... up to MAX_FILTER_RULES entries (max 26).
extern const char* const kRuleKeys[];

// ---------------------------------------------------------------------------
// FilterRuleSerializer — serializes a single FilterRule as a JSON sub-object
// ---------------------------------------------------------------------------

class FilterRuleSerializer : public ConfigSerializer {
    FilterRule* _rule;
protected:
    void structure() override {
        if (!_rule) return;  // guard against uninitialized use
        def("act",  (uint8_t&)_rule->action);
        def("fld",  (uint8_t&)_rule->field);
        def("op",   (uint8_t&)_rule->op);
        def("val",  _rule->value);
        def("af",   _rule->and_field);
        def("aop",  (uint8_t&)_rule->and_op);
        def("av",   _rule->and_value);
        // channel hashes as flat hex blob (MAX_CHANNEL_HASHES bytes)
        def("ch",   _rule->channel_hashes, MAX_CHANNEL_HASHES);
        def("ch_n", _rule->channel_hash_count);
        // path hashes as flat hex blob (MAX_PATH_HASHES_PER_RULE * MAX_PATH_HASH_SIZE bytes)
        def("path", _rule->path_hashes, MAX_PATH_HASHES_PER_RULE * MAX_PATH_HASH_SIZE);
        def("pl",   _rule->path_hash_len);
        def("pn",   _rule->path_hash_count);
        // REPEATER field: source OR-list
        def("src",  _rule->source_hashes, MAX_SOURCE_HASHES);
        def("src_n",_rule->source_hash_count);
        // SCOPE field: region name
        def("scn",  _rule->scope_name, MAX_SCOPE_NAME_LEN);
        def("en",   _rule->enabled);
        def("use",  _rule->in_use);
    }
public:
    FilterRuleSerializer() : _rule(nullptr) {}
    void setRule(FilterRule* r) { _rule = r; }
};

// ---------------------------------------------------------------------------
// FilterProfileSerializer — serializes one profile (rules + mode)
// ---------------------------------------------------------------------------

class FilterProfileSerializer : public ConfigSerializer {
    FilterRule           _rules[MAX_FILTER_RULES];
    FilterMode           _mode;
    FilterRuleSerializer _rule_ser[MAX_FILTER_RULES];
protected:
    void structure() override {
        def("mode", (uint8_t&)_mode);
        for (int i = 0; i < MAX_FILTER_RULES; i++) {
            def(kRuleKeys[i], _rule_ser[i]);  // stable string pointers — no mutation
        }
    }
public:
    FilterProfileSerializer() : _mode(FilterMode::ALLOW) {
        memset(_rules, 0, sizeof(_rules));
        for (int i = 0; i < MAX_FILTER_RULES; i++) {
            _rule_ser[i].setRule(&_rules[i]);
        }
    }

    // Non-copyable: _rule_ser[] contains pointers into _rules[]
    FilterProfileSerializer(const FilterProfileSerializer&)            = delete;
    FilterProfileSerializer& operator=(const FilterProfileSerializer&) = delete;

    FilterMode        getMode()  const { return _mode; }
    void              setMode(FilterMode m) { _mode = m; }
    FilterRule*       getRules()       { return _rules; }
    const FilterRule* getRules() const { return _rules; }

    void reset() {
        memset(_rules, 0, sizeof(_rules));
        _mode = FilterMode::ALLOW;
    }

    int firstFreeSlot() const {
        for (int i = 0; i < MAX_FILTER_RULES; i++) {
            if (!_rules[i].in_use) return i;
        }
        return -1;
    }
};

// ---------------------------------------------------------------------------
// ChannelFilter — two profiles + active profile selection
// ---------------------------------------------------------------------------

class ChannelFilter : public ConfigSerializer {
    FilterProfileSerializer _profiles[MAX_FILTER_PROFILES];
    uint8_t                 _active;  // 0-based index of active profile (0 or 1)

protected:
    void structure() override {
        def("ap", _active);
        def("pa", _profiles[0]);  // profile 1
        def("pb", _profiles[1]);  // profile 2
    }

public:
    ChannelFilter();

    // Non-copyable
    ChannelFilter(const ChannelFilter&)            = delete;
    ChannelFilter& operator=(const ChannelFilter&) = delete;

    // --- Lifecycle ----------------------------------------------------------

    void load(FILESYSTEM& fs);
    void save(FILESYSTEM& fs) const;

    // --- Evaluation ---------------------------------------------------------

    // Evaluate active profile against a received packet.
    // region_map is used for SCOPE field matching — pass nullptr to skip scope rules.
    // Returns true if the packet should be DROPPED.
    bool evaluate(const mesh::Packet* pkt, int16_t rssi, const RegionMap* region_map = nullptr) const;

    // --- CLI dispatch -------------------------------------------------------

    // Handle a "filter ..." command string (everything after "filter ").
    // sender == nullptr = local Serial CLI; non-null = remote CLI (paginates).
    void handleCommand(const char* args, char* reply, FILESYSTEM& fs, ClientInfo* sender = nullptr);

    // Rule value formatter — used by formatRuleLine() in ChannelFilter.cpp
    static void formatRuleValue(const FilterRule& rule, char* out, int outlen);

private:
    // Returns pointer to profile by 1-based number, or active profile if num==0.
    // Clamps to valid range — never returns out-of-bounds pointer.
    FilterProfileSerializer* _getProfile(uint8_t num) {
        if (num == 0 || num > MAX_FILTER_PROFILES) return &_profiles[_active];
        return &_profiles[num - 1];
    }
    const FilterProfileSerializer* _getProfile(uint8_t num) const {
        if (num == 0 || num > MAX_FILTER_PROFILES) return &_profiles[_active];
        return &_profiles[num - 1];
    }

    bool _ruleMatches(const FilterRule& rule, const mesh::Packet* pkt, int16_t rssi,
                      const RegionMap* region_map) const;
    bool _evalScalar(FilterField field, FilterOp op, int16_t val,
                     const mesh::Packet* pkt, int16_t rssi) const;

    void _listRules(char* reply, uint8_t page, bool is_remote, uint8_t profile_num) const;
    bool _loadBinary(FILESYSTEM& fs);
};

#endif // PACKET_FILTER
