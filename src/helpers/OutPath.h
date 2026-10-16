#pragma once
#include <Packet.h>
#include <Utils.h>
#include <stdio.h>
#include <string.h>
#define OUT_PATH_FORCE_FLOOD 0xFE
#define OUT_PATH_UNKNOWN 0xFF

static char* trimOutPathSpaces(char* s) {
  while (*s == ' ') s++;
  char* end = s + strlen(s);
  while (end > s && end[-1] == ' ') end--;
  *end = 0;
  return s;
}

static bool parsePathCommandImpl(char* raw, uint8_t* out_path, uint8_t& out_path_len, const char*& err) {
  if (raw == NULL || out_path == NULL) {
    err = "Err - bad params";
    return false;
  }

  char* spec = trimOutPathSpaces(raw);
  if (*spec == 0) {
    err = "Err - missing path";
    return false;
  }
  if (strcmp(spec, "clear") == 0 || strcmp(spec, "-") == 0 || strcmp(spec, "none") == 0) {
    out_path_len = OUT_PATH_UNKNOWN;
    return true;
  }
  if (strcmp(spec, "flood") == 0) {
    out_path_len = OUT_PATH_FORCE_FLOOD;
    return true;
  }
  if (strcmp(spec, "direct") == 0) {
    out_path_len = 0;
    return true;
  }

  uint8_t hash_size = 0;
  uint8_t hop_count = 0;
  char* token = spec;
  while (token && *token) {
    char* comma = strchr(token, ',');
    if (comma && comma[1] == 0) { err = "Err - missing hop"; return false; }
    if (comma) *comma = 0;
    token = trimOutPathSpaces(token);

    int hex_len = strlen(token);
    if (!(hex_len == 2 || hex_len == 4 || hex_len == 6)) {
      err = "Err - bad params";
      return false;
    }

    for (int i = 0; i < hex_len; ++i) {
      char c = token[i];
      if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))) {
        err = "Err - bad hex";
        return false;
      }
    }
    uint8_t hop_hash_size = (uint8_t)(hex_len / 2);
    if (hash_size == 0) {
      hash_size = hop_hash_size;
    } else if (hash_size != hop_hash_size) {
      err = "Err - bad params";
      return false;
    }

    if (hop_count >= 63 || (hop_count + 1) * hash_size > MAX_PATH_SIZE) {
      err = "Err - bad params";
      return false;
    }
    if (!mesh::Utils::fromHex(&out_path[hop_count * hash_size], hash_size, token)) {
      err = "Err - bad hex";
      return false;
    }

    hop_count++;
    token = comma ? comma + 1 : NULL;
  }

  if (hash_size == 0 || hop_count == 0) {
    err = "Err - missing path";
    return false;
  }
  out_path_len = ((hash_size - 1) << 6) | (hop_count & 63);
  return true;
}

static void formatPathReply(const uint8_t* path, uint8_t path_len, char* out, size_t out_len) {
  if (path_len == OUT_PATH_FORCE_FLOOD) {
    snprintf(out, out_len, "> flood");
    return;
  }
  if (path_len == OUT_PATH_UNKNOWN) {
    snprintf(out, out_len, "> unknown");
    return;
  }
  if (!mesh::Packet::isValidPathLen(path_len)) {
    snprintf(out, out_len, "> invalid");
    return;
  }
  if ((path_len & 63) == 0) {
    snprintf(out, out_len, "> direct");
    return;
  }

  uint8_t hash_size = (path_len >> 6) + 1;
  uint8_t hop_count = path_len & 63;
  uint8_t byte_len = hop_count * hash_size;
  char hex[(MAX_PATH_SIZE * 2) + 1];
  mesh::Utils::toHex(hex, path, byte_len);
  snprintf(out, out_len, "> hs=%u hops=%u hex=%s", (uint32_t)hash_size, (uint32_t)hop_count, hex);
}


inline bool parsePathCommand(char* raw, uint8_t* out_path, uint8_t& out_path_len, const char*& err) {
  if (!raw || !out_path) { err = "Err - bad params"; return false; }
  uint8_t proposed[MAX_PATH_SIZE] = {};
  uint8_t proposed_len = OUT_PATH_UNKNOWN;
  if (!parsePathCommandImpl(raw, proposed, proposed_len, err)) return false;
  memcpy(out_path, proposed, MAX_PATH_SIZE);
  out_path_len = proposed_len;
  return true;
}

inline void learnOutPath(uint8_t* stored, uint8_t& stored_len, const uint8_t* path, uint8_t len) {
  if (stored_len != OUT_PATH_FORCE_FLOOD) stored_len = mesh::Packet::copyPath(stored, path, len);
}
