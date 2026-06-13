#include "mock_mesh.h"
#include "FilterRule.h"
#include "FilterParser.h"
#include "ChannelFilter.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

// ---------------------------------------------------------------------------
// Interactive shell — type "filter ..." commands and see output
// ---------------------------------------------------------------------------

static void print_help() {
    printf("\n");
    printf("  Commands:\n");
    printf("    filter add <action> <field> <op> <value>\n");
    printf("    filter del <id>\n");
    printf("    filter disable <id> / enable <id>\n");
    printf("    filter list [page]\n");
    printf("    filter clear\n");
    printf("    filter mode allow|drop\n");
    printf("\n");
    printf("  Test packet evaluation:\n");
    printf("    eval route:<0-3> type:<0-15> snr:<dB> rssi:<dBm> hops:<n> [channel:<hex>] [hop:<hex>]\n");
    printf("      route: 0=tflood 1=flood 2=direct 3=tdirect\n");
    printf("      type:  0=req 2=txt 4=advert 5=grptxt 6=grpdata ...\n");
    printf("\n");
    printf("    eval example: eval route:1 type:5 snr:-5 rssi:-90 hops:2 channel:AB hop:CD\n");
    printf("\n");
    printf("    help   — show this message\n");
    printf("    quit   — exit\n");
    printf("\n");
}

// Simple key:value parser for eval command
static bool getIntArg(const char* line, const char* key, long* out) {
    const char* p = strstr(line, key);
    if (!p) return false;
    p += strlen(key);
    char* end;
    *out = strtol(p, &end, 0);
    return end != p;
}

static bool getHexArg(const char* line, const char* key, uint8_t* out) {
    const char* p = strstr(line, key);
    if (!p) return false;
    p += strlen(key);
    // skip optional 0x
    if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) p += 2;
    char* end;
    long v = strtol(p, &end, 16);
    if (end == p) return false;
    *out = (uint8_t)v;
    return true;
}

static void handle_eval(const char* line, ChannelFilter& f) {
    long route = 1, type = 2, snr_db = 0, rssi = -80, hops = 0;
    uint8_t channel = 0, hop_hash = 0;
    bool has_channel = false, has_hop = false;

    getIntArg(line, "route:", &route);
    getIntArg(line, "type:",  &type);
    getIntArg(line, "snr:",   &snr_db);
    getIntArg(line, "rssi:",  &rssi);
    getIntArg(line, "hops:",  &hops);
    has_channel = getHexArg(line, "channel:", &channel);
    has_hop     = getHexArg(line, "hop:",     &hop_hash);

    mesh::Packet pkt;
    pkt.setHeader((uint8_t)route, (uint8_t)type);
    pkt._snr = (int8_t)(snr_db * 4);

    if (has_channel) {
        pkt.payload[0]  = channel;
        pkt.payload_len = 1;
    }

    uint8_t path_data[64];
    memset(path_data, 0, sizeof(path_data));
    if (hops > 0) {
        // Fill path with zeros except last hop
        for (int i = 0; i < hops - 1; i++) path_data[i] = 0x00;
        path_data[hops - 1] = has_hop ? hop_hash : 0x00;
        pkt.setPath(path_data, 1, (uint8_t)hops);
    }

    bool dropped = f.evaluate(&pkt, (int16_t)rssi);

    printf("  Packet: route=%ld type=%ld snr=%lddB rssi=%lddBm hops=%ld",
           route, type, snr_db, rssi, hops);
    if (has_channel) printf(" channel=0x%02X", channel);
    if (has_hop)     printf(" last_hop=0x%02X", hop_hash);
    printf("\n");
    printf("  Result: %s\n", dropped ? "*** DROPPED ***" : "PASSED (forwarded)");
}

int main() {
    MockFS fs;
    ChannelFilter filter;

    printf("═══════════════════════════════════════\n");
    printf(" MeshCore Filter Engine — Interactive Shell\n");
    printf(" (rules are not persisted between sessions)\n");
    printf("═══════════════════════════════════════\n");
    print_help();

    char line[256];
    while (true) {
        printf("> ");
        fflush(stdout);

        if (!fgets(line, sizeof(line), stdin)) break;

        // Strip trailing newline
        size_t len = strlen(line);
        while (len > 0 && (line[len-1] == '\n' || line[len-1] == '\r')) {
            line[--len] = '\0';
        }

        if (len == 0) continue;

        if (strcmp(line, "quit") == 0 || strcmp(line, "exit") == 0) {
            printf("Bye.\n");
            break;
        }

        if (strcmp(line, "help") == 0) {
            print_help();
            continue;
        }

        if (strncmp(line, "eval", 4) == 0) {
            handle_eval(line + 4, filter);
            continue;
        }

        if (strncmp(line, "filter", 6) == 0 &&
            (line[6] == ' ' || line[6] == '\0')) {
            char reply[160];
            const char* args = (line[6] == ' ') ? line + 7 : "";
            filter.handleCommand(args, reply, fs);
            printf("  -> %s\n", reply);
            continue;
        }

        printf("  Unknown command. Type 'help' for usage.\n");
    }

    return 0;
}
