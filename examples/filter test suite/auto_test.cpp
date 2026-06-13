#include "mock_mesh.h"
#include "FilterRule.h"
#include "FilterParser.h"
#include "ChannelFilter.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

// ---------------------------------------------------------------------------
// Minimal test framework
// ---------------------------------------------------------------------------

static int _pass = 0, _fail = 0;

#define CHECK(desc, expr) do { \
    if (expr) { \
        printf("  PASS  %s\n", desc); \
        _pass++; \
    } else { \
        printf("  FAIL  %s  (line %d)\n", desc, __LINE__); \
        _fail++; \
    } \
} while(0)

static void section(const char* name) {
    printf("\n── %s\n", name);
}

// ---------------------------------------------------------------------------
// Packet builder helpers
// ---------------------------------------------------------------------------

static mesh::Packet makePacket(uint8_t route, uint8_t type,
                               int8_t snr_qdB = 0,
                               uint8_t channel = 0,
                               uint8_t hop_count = 0, uint8_t hash_size = 1,
                               const uint8_t* path_data = nullptr) {
    mesh::Packet p;
    p.setHeader(route, type);
    p._snr = snr_qdB;
    if (type == PAYLOAD_TYPE_GRP_TXT || type == PAYLOAD_TYPE_GRP_DATA) {
        p.payload[0]  = channel;
        p.payload_len = 1;
    }
    if (hop_count > 0 && path_data) {
        p.setPath(path_data, hash_size, hop_count);
    }
    return p;
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

static void test_parser() {
    section("Parser — valid commands");

    auto r = parseFilterCommand("add drop payload eq grptxt");
    CHECK("add drop payload eq grptxt -> OK",        r.error == FilterParseError::OK);
    CHECK("  command == ADD",                         r.command == FilterCommand::ADD);
    CHECK("  action  == DROP",                        r.rule.action == FilterAction::DROP);
    CHECK("  field   == TYPE",                        r.rule.field  == FilterField::TYPE);
    CHECK("  op      == EQ",                          r.rule.op     == FilterOp::EQ);
    CHECK("  value   == 0x05 (grptxt)",               r.rule.value  == 0x05);

    r = parseFilterCommand("add allow route eq flood");
    CHECK("add allow route eq flood -> OK",           r.error == FilterParseError::OK);
    CHECK("  action == ALLOW",                        r.rule.action == FilterAction::ALLOW);
    CHECK("  field  == ROUTE",                        r.rule.field  == FilterField::ROUTE);
    CHECK("  value  == 0x01 (flood)",                 r.rule.value  == 0x01);

    r = parseFilterCommand("add drop hops gt 5");
    CHECK("add drop hops gt 5 -> OK",                 r.error == FilterParseError::OK);
    CHECK("  field == HOPS, op == GT, value == 5",
          r.rule.field == FilterField::HOPS && r.rule.op == FilterOp::GT && r.rule.value == 5);

    r = parseFilterCommand("add drop snr lt -10");
    CHECK("add drop snr lt -10 -> OK",                r.error == FilterParseError::OK);
    CHECK("  SNR stored as quarter-dB (-40)",          r.rule.value == -40);

    r = parseFilterCommand("add drop rssi lt -110");
    CHECK("add drop rssi lt -110 -> OK",              r.error == FilterParseError::OK);
    CHECK("  RSSI value == -110",                      r.rule.value == -110);

    r = parseFilterCommand("add drop channel eq 0xAB");
    CHECK("add drop channel eq 0xAB -> OK",           r.error == FilterParseError::OK);
    CHECK("  channel value == 0xAB",                   r.rule.value == 0xAB);

    r = parseFilterCommand("add drop path eq AB 12 CD");
    CHECK("add drop path eq AB 12 CD -> OK",          r.error == FilterParseError::OK);
    CHECK("  path_hash_count == 3",                    r.rule.path_hash_count == 3);
    CHECK("  path_hash_len   == 1",                    r.rule.path_hash_len   == 1);
    CHECK("  hash[0] == 0xAB",                         r.rule.path_hashes[0][0] == 0xAB);
    CHECK("  hash[1] == 0x12",                         r.rule.path_hashes[1][0] == 0x12);
    CHECK("  hash[2] == 0xCD",                         r.rule.path_hashes[2][0] == 0xCD);

    r = parseFilterCommand("del 3");
    CHECK("del 3 -> OK, rule_id == 3",
          r.error == FilterParseError::OK && r.command == FilterCommand::DEL && r.rule_id == 3);

    r = parseFilterCommand("disable 0");
    CHECK("disable 0 -> OK",
          r.error == FilterParseError::OK && r.command == FilterCommand::DISABLE);

    r = parseFilterCommand("enable 7");
    CHECK("enable 7 -> OK",
          r.error == FilterParseError::OK && r.command == FilterCommand::ENABLE);

    r = parseFilterCommand("list");
    CHECK("list -> OK, page 0",
          r.error == FilterParseError::OK && r.command == FilterCommand::LIST && r.rule_id == 0);

    r = parseFilterCommand("list 1");
    CHECK("list 1 -> OK, page 1",
          r.error == FilterParseError::OK && r.rule_id == 1);

    r = parseFilterCommand("clear");
    CHECK("clear -> OK",
          r.error == FilterParseError::OK && r.command == FilterCommand::CLEAR);

    r = parseFilterCommand("mode drop");
    CHECK("mode drop -> OK",
          r.error == FilterParseError::OK && r.mode == FilterMode::DROP);

    r = parseFilterCommand("mode allow");
    CHECK("mode allow -> OK",
          r.error == FilterParseError::OK && r.mode == FilterMode::ALLOW);

    section("Parser — error cases");

    r = parseFilterCommand("add drop payload eq grptxt_TYPO");
    CHECK("unknown value -> UNKNOWN_VALUE",            r.error == FilterParseError::UNKNOWN_VALUE);

    r = parseFilterCommand("add drop BADFIELD eq 5");
    CHECK("unknown field -> UNKNOWN_FIELD",            r.error == FilterParseError::UNKNOWN_FIELD);

    r = parseFilterCommand("add drop payload BADOP grptxt");
    CHECK("unknown op -> UNKNOWN_OP",                  r.error == FilterParseError::UNKNOWN_OP);

    r = parseFilterCommand("add drop payload eq");
    CHECK("missing value -> MISSING_TOKEN",            r.error == FilterParseError::MISSING_TOKEN);

    r = parseFilterCommand("del 99");
    CHECK("del out-of-range -> INVALID_RULE_ID",       r.error == FilterParseError::INVALID_RULE_ID);

    r = parseFilterCommand("BADCMD");
    CHECK("unknown command -> UNKNOWN_COMMAND",        r.error == FilterParseError::UNKNOWN_COMMAND);

    r = parseFilterCommand("add drop path eq AB 12 ZZZZ");
    CHECK("invalid hex in path -> INVALID_HEX",        r.error == FilterParseError::INVALID_HEX);

    r = parseFilterCommand("add drop path eq AB 1234");
    CHECK("mixed hash size -> HASH_SIZE_MISMATCH",     r.error == FilterParseError::HASH_SIZE_MISMATCH);

    r = parseFilterCommand("mode BADMODE");
    CHECK("unknown mode -> UNKNOWN_MODE",              r.error == FilterParseError::UNKNOWN_MODE);
}

// Helper: add a rule and immediately enable it (rules are added disabled by default)
static void addRule(ChannelFilter& f, const char* cmd, MockFS& fs) {
    char reply[160];
    char enable_cmd[16];
    f.handleCommand(cmd, reply, fs);
    // Extract slot id from "OK - rule N added"
    int id = -1;
    sscanf(reply, "OK - rule %d added", &id);
    if (id >= 0) {
        snprintf(enable_cmd, sizeof(enable_cmd), "enable %d", id);
        f.handleCommand(enable_cmd, reply, fs);
    }
}

static void test_evaluate() {
    MockFS fs;
    ChannelFilter f;

    section("Evaluate — default mode allow (no rules)");
    mesh::Packet p = makePacket(ROUTE_TYPE_FLOOD, PAYLOAD_TYPE_GRP_TXT);
    CHECK("no rules, mode allow -> pass",  !f.evaluate(&p, -80));

    section("Evaluate — route match");
    char reply[160];
    addRule(f, "add drop route eq tflood", fs);
    mesh::Packet tflood = makePacket(ROUTE_TYPE_TRANSPORT_FLOOD, PAYLOAD_TYPE_TXT_MSG);
    mesh::Packet flood  = makePacket(ROUTE_TYPE_FLOOD,           PAYLOAD_TYPE_TXT_MSG);
    CHECK("tflood packet -> DROP",    f.evaluate(&tflood, -80));
    CHECK("flood packet  -> PASS",   !f.evaluate(&flood,  -80));
    f.handleCommand("clear", reply, fs);

    section("Evaluate — payload type match");
    addRule(f, "add drop payload eq grptxt", fs);
    mesh::Packet grptxt = makePacket(ROUTE_TYPE_FLOOD, PAYLOAD_TYPE_GRP_TXT);
    mesh::Packet txt    = makePacket(ROUTE_TYPE_FLOOD, PAYLOAD_TYPE_TXT_MSG);
    CHECK("grptxt packet -> DROP",    f.evaluate(&grptxt, -80));
    CHECK("txt packet    -> PASS",   !f.evaluate(&txt,    -80));
    f.handleCommand("clear", reply, fs);

    section("Evaluate — hops gt");
    addRule(f, "add drop hops gt 3", fs);
    uint8_t path4[4] = {0x01, 0x02, 0x03, 0x04};
    uint8_t path2[2] = {0x01, 0x02};
    mesh::Packet p4 = makePacket(ROUTE_TYPE_FLOOD, PAYLOAD_TYPE_TXT_MSG, 0, 0, 4, 1, path4);
    mesh::Packet p2 = makePacket(ROUTE_TYPE_FLOOD, PAYLOAD_TYPE_TXT_MSG, 0, 0, 2, 1, path2);
    CHECK("4 hops, rule gt 3 -> DROP",   f.evaluate(&p4, -80));
    CHECK("2 hops, rule gt 3 -> PASS",  !f.evaluate(&p2, -80));
    f.handleCommand("clear", reply, fs);

    section("Evaluate — SNR lt");
    addRule(f, "add drop snr lt -10", fs);
    // SNR -12 dB = -48 quarter-dB; SNR -8 dB = -32 quarter-dB
    mesh::Packet pLowSNR  = makePacket(ROUTE_TYPE_FLOOD, PAYLOAD_TYPE_TXT_MSG, -48);
    mesh::Packet pHighSNR = makePacket(ROUTE_TYPE_FLOOD, PAYLOAD_TYPE_TXT_MSG, -32);
    CHECK("SNR -12 dB < -10 -> DROP",    f.evaluate(&pLowSNR,  -80));
    CHECK("SNR  -8 dB > -10 -> PASS",   !f.evaluate(&pHighSNR, -80));
    f.handleCommand("clear", reply, fs);

    section("Evaluate — RSSI lt");
    addRule(f, "add drop rssi lt -100", fs);
    CHECK("RSSI -110 < -100 -> DROP",    f.evaluate(&p, -110));
    CHECK("RSSI  -80 > -100 -> PASS",   !f.evaluate(&p,  -80));
    f.handleCommand("clear", reply, fs);

    section("Evaluate — channel match");
    addRule(f, "add drop channel eq 0xAB", fs);
    mesh::Packet chanAB = makePacket(ROUTE_TYPE_FLOOD, PAYLOAD_TYPE_GRP_TXT, 0, 0xAB);
    mesh::Packet chanCD = makePacket(ROUTE_TYPE_FLOOD, PAYLOAD_TYPE_GRP_TXT, 0, 0xCD);
    CHECK("channel 0xAB -> DROP",    f.evaluate(&chanAB, -80));
    CHECK("channel 0xCD -> PASS",   !f.evaluate(&chanCD, -80));
    f.handleCommand("clear", reply, fs);

    section("Evaluate — path OR-match");
    addRule(f, "add drop path eq AB CD", fs);
    uint8_t hopAB[1] = {0xAB};
    uint8_t hopCD[1] = {0xCD};
    uint8_t hopEF[1] = {0xEF};
    mesh::Packet pAB = makePacket(ROUTE_TYPE_FLOOD, PAYLOAD_TYPE_TXT_MSG, 0, 0, 1, 1, hopAB);
    mesh::Packet pCD = makePacket(ROUTE_TYPE_FLOOD, PAYLOAD_TYPE_TXT_MSG, 0, 0, 1, 1, hopCD);
    mesh::Packet pEF = makePacket(ROUTE_TYPE_FLOOD, PAYLOAD_TYPE_TXT_MSG, 0, 0, 1, 1, hopEF);
    CHECK("last hop 0xAB in list -> DROP",    f.evaluate(&pAB, -80));
    CHECK("last hop 0xCD in list -> DROP",    f.evaluate(&pCD, -80));
    CHECK("last hop 0xEF not in list -> PASS",!f.evaluate(&pEF, -80));
    f.handleCommand("clear", reply, fs);

    section("Evaluate — first-match order");
    addRule(f, "add allow payload eq grptxt", fs);
    addRule(f, "add drop  route  eq flood",   fs);
    mesh::Packet grp  = makePacket(ROUTE_TYPE_FLOOD, PAYLOAD_TYPE_GRP_TXT);
    mesh::Packet norm = makePacket(ROUTE_TYPE_FLOOD, PAYLOAD_TYPE_TXT_MSG);
    CHECK("grptxt flood: allow rule fires first -> PASS", !f.evaluate(&grp,  -80));
    CHECK("txt   flood: drop  rule fires        -> DROP",  f.evaluate(&norm, -80));
    f.handleCommand("clear", reply, fs);

    section("Evaluate — disabled rule ignored");
    f.handleCommand("add drop payload eq grptxt", reply, fs);  // added disabled — do NOT enable
    mesh::Packet g = makePacket(ROUTE_TYPE_FLOOD, PAYLOAD_TYPE_GRP_TXT);
    CHECK("added rule is disabled by default -> packet passes",  !f.evaluate(&g, -80));
    f.handleCommand("enable 0", reply, fs);
    CHECK("re-enabled rule -> packet drops",  f.evaluate(&g, -80));
    f.handleCommand("disable 0", reply, fs);
    CHECK("disabled again -> packet passes",  !f.evaluate(&g, -80));
    f.handleCommand("clear", reply, fs);

    section("Evaluate — default mode drop");
    f.handleCommand("mode drop", reply, fs);
    mesh::Packet any = makePacket(ROUTE_TYPE_FLOOD, PAYLOAD_TYPE_TXT_MSG);
    CHECK("mode drop, no rules -> DROP",    f.evaluate(&any, -80));
    f.handleCommand("mode allow", reply, fs);
    CHECK("mode allow, no rules -> PASS",  !f.evaluate(&any, -80));
}

static void test_persistence() {
    section("Persistence — save and reload");

    MockFS fs;
    ChannelFilter f1;
    char reply[160];

    addRule(f1, "add drop payload eq grptxt", fs);  // rule 0 — enabled
    addRule(f1, "add drop route eq tflood",   fs);  // rule 1 — enabled
    f1.handleCommand("disable 0",             reply, fs);
    f1.handleCommand("mode drop",             reply, fs);

    ChannelFilter f2;
    f2.load(fs);

    mesh::Packet grptxt = makePacket(ROUTE_TYPE_FLOOD,           PAYLOAD_TYPE_GRP_TXT);
    mesh::Packet tflood = makePacket(ROUTE_TYPE_TRANSPORT_FLOOD, PAYLOAD_TYPE_TXT_MSG);
    mesh::Packet norm   = makePacket(ROUTE_TYPE_FLOOD,           PAYLOAD_TYPE_TXT_MSG);

    // Rule 0 disabled — grptxt falls through to default policy (drop)
    CHECK("rule 0 disabled after reload -> grptxt drops (default policy)", f2.evaluate(&grptxt, -80));
    CHECK("rule 1 active after reload   -> tflood drops",                   f2.evaluate(&tflood, -80));
    CHECK("mode drop after reload       -> norm drops",                     f2.evaluate(&norm,   -80));

    // Verify disabled means rule is skipped — test with mode allow
    ChannelFilter f3;
    f3.handleCommand("add drop payload eq grptxt", reply, fs);  // added disabled
    CHECK("added disabled, mode allow -> grptxt passes", !f3.evaluate(&grptxt, -80));
}

static void test_and_condition() {
    MockFS fs;
    ChannelFilter f;
    char reply[160];

    section("Parser — AND condition");

    auto r = parseFilterCommand("add drop channel eq 0x11 and hops gt 8");
    CHECK("AND parse -> OK",                           r.error == FilterParseError::OK);
    CHECK("  primary field  == CHANNEL",               r.rule.field    == FilterField::CHANNEL);
    CHECK("  primary value  == 0x11",                  r.rule.value    == 0x11);
    CHECK("  and_field      == HOPS",                  r.rule.and_field == (uint8_t)FilterField::HOPS);
    CHECK("  and_op         == GT",                    r.rule.and_op   == FilterOp::GT);
    CHECK("  and_value      == 8",                     r.rule.and_value == 8);

    r = parseFilterCommand("add drop snr lt -10 and rssi lt -100");
    CHECK("AND snr+rssi -> OK",                        r.error == FilterParseError::OK);
    CHECK("  and_field == RSSI",                       r.rule.and_field == (uint8_t)FilterField::RSSI);
    CHECK("  and_value == -100",                       r.rule.and_value == -100);

    r = parseFilterCommand("add drop path eq AB and hops gt 3");
    CHECK("AND path+hops -> OK",                       r.error == FilterParseError::OK);
    CHECK("  path primary intact",                     r.rule.path_hash_count == 1);
    CHECK("  and_field == HOPS",                       r.rule.and_field == (uint8_t)FilterField::HOPS);

    r = parseFilterCommand("add drop channel eq 0x11 and path eq AB");
    CHECK("AND with path -> AND_PATH_NOT_ALLOWED",     r.error == FilterParseError::AND_PATH_NOT_ALLOWED);

    r = parseFilterCommand("add drop channel eq 0x11 and channel eq 0x22");
    CHECK("AND duplicate field -> AND_DUPLICATE_FIELD",r.error == FilterParseError::AND_DUPLICATE_FIELD);

    section("Evaluate — AND condition");

    // Rule: drop channel eq 0x11 and hops gt 3
    addRule(f, "add drop channel eq 0x11 and hops gt 3", fs);

    uint8_t path4[4] = {0x01, 0x02, 0x03, 0x04};
    uint8_t path2[2] = {0x01, 0x02};

    // Both conditions true -> DROP
    mesh::Packet p_both = makePacket(ROUTE_TYPE_FLOOD, PAYLOAD_TYPE_GRP_TXT, 0, 0x11, 4, 1, path4);
    CHECK("channel 0x11 AND hops=4 > 3 -> DROP",       f.evaluate(&p_both, -80));

    // Channel matches, hops does not -> PASS
    mesh::Packet p_ch_only = makePacket(ROUTE_TYPE_FLOOD, PAYLOAD_TYPE_GRP_TXT, 0, 0x11, 2, 1, path2);
    CHECK("channel 0x11 AND hops=2 not > 3 -> PASS",  !f.evaluate(&p_ch_only, -80));

    // Hops match, channel does not -> PASS
    mesh::Packet p_hop_only = makePacket(ROUTE_TYPE_FLOOD, PAYLOAD_TYPE_GRP_TXT, 0, 0x22, 4, 1, path4);
    CHECK("channel 0x22 AND hops=4, ch mismatch -> PASS", !f.evaluate(&p_hop_only, -80));

    f.handleCommand("clear", reply, fs);

    section("List — AND condition display");

    f.handleCommand("add drop channel eq 0x11 and hops gt 8", reply, fs);  // list doesn't need enabled
    f.handleCommand("list", reply, fs);
    CHECK("list shows 'and' keyword",   strstr(reply, "and") != nullptr);
    CHECK("list shows 'hops'",          strstr(reply, "hops") != nullptr);
    CHECK("list shows '8'",             strstr(reply, "8")    != nullptr);
    CHECK("list within 138 chars",      strlen(reply) <= 138);
    f.handleCommand("clear", reply, fs);
}

static void test_list_length() {
    section("List output — page length within 138 chars");

    MockFS fs;
    ChannelFilter f;
    char reply[160];

    // Fill with worst-case rules
    f.handleCommand("add drop payload neq grpdata", reply, fs);
    f.handleCommand("add drop payload neq grpdata", reply, fs);
    f.handleCommand("add drop payload neq grpdata", reply, fs);
    f.handleCommand("add drop payload neq grpdata", reply, fs);
    f.handleCommand("add drop payload neq grpdata", reply, fs);
    f.handleCommand("add drop payload neq grpdata", reply, fs);

    for (int page = 0; ; page++) {
        char pr[16];
        snprintf(pr, sizeof(pr), "list %d", page);
        f.handleCommand(pr, reply, fs);
        int len = strlen(reply);
        char desc[64];
        snprintf(desc, sizeof(desc), "page %d length %d <= 138", page, len);
        CHECK(desc, len <= 138);
        // Stop when no more pages hinted
        if (strstr(reply, "-> filter list") == nullptr) break;
        if (page > 10) break;  // safety
    }
}

// ---------------------------------------------------------------------------
// Entry point
// ---------------------------------------------------------------------------

int main() {
    printf("═══════════════════════════════════════\n");
    printf(" MeshCore Filter Engine — Test Suite\n");
    printf("═══════════════════════════════════════\n");

    test_parser();
    test_evaluate();
    test_persistence();
    test_and_condition();
    test_list_length();

    printf("\n═══════════════════════════════════════\n");
    printf(" Results: %d passed, %d failed\n", _pass, _fail);
    printf("═══════════════════════════════════════\n");

    return (_fail == 0) ? 0 : 1;
}
