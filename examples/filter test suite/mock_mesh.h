#pragma once

// ---------------------------------------------------------------------------
// Minimal stubs so FilterParser.cpp and ChannelFilter.cpp compile on Linux
// without Arduino/MeshCore headers.
// ---------------------------------------------------------------------------

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>

// Silence debug prints
#define MESH_DEBUG_PRINTLN(x)  do {} while(0)

// Route type constants (mirrors Packet.h)
#define ROUTE_TYPE_TRANSPORT_FLOOD   0x00
#define ROUTE_TYPE_FLOOD             0x01
#define ROUTE_TYPE_DIRECT            0x02
#define ROUTE_TYPE_TRANSPORT_DIRECT  0x03

// Payload type constants (mirrors Packet.h)
#define PAYLOAD_TYPE_REQ         0x00
#define PAYLOAD_TYPE_RESPONSE    0x01
#define PAYLOAD_TYPE_TXT_MSG     0x02
#define PAYLOAD_TYPE_ACK         0x03
#define PAYLOAD_TYPE_ADVERT      0x04
#define PAYLOAD_TYPE_GRP_TXT     0x05
#define PAYLOAD_TYPE_GRP_DATA    0x06
#define PAYLOAD_TYPE_ANON_REQ    0x07
#define PAYLOAD_TYPE_PATH        0x08
#define PAYLOAD_TYPE_TRACE       0x09
#define PAYLOAD_TYPE_MULTIPART   0x0A
#define PAYLOAD_TYPE_CONTROL     0x0B
#define PAYLOAD_TYPE_RAW_CUSTOM  0x0F

#define PH_ROUTE_MASK   0x03
#define PH_TYPE_SHIFT      2
#define PH_TYPE_MASK    0x0F
#define PH_VER_SHIFT       6

#define MAX_PATH_SIZE        64
#define MAX_PACKET_PAYLOAD  200

namespace mesh {

class Packet {
public:
    uint8_t  header;
    uint16_t payload_len;
    uint16_t path_len;
    uint8_t  path[MAX_PATH_SIZE];
    uint8_t  payload[MAX_PACKET_PAYLOAD];
    int8_t   _snr;   // quarter-dB

    Packet() { memset(this, 0, sizeof(*this)); }

    uint8_t getRouteType()    const { return header & PH_ROUTE_MASK; }
    uint8_t getPayloadType()  const { return (header >> PH_TYPE_SHIFT) & PH_TYPE_MASK; }
    uint8_t getPathHashSize() const { return (path_len >> 6) + 1; }
    uint8_t getPathHashCount()const { return path_len & 63; }
    float   getSNR()          const { return ((float)_snr) / 4.0f; }

    // Helper: set route + payload type in header
    void setHeader(uint8_t route, uint8_t type) {
        header = (route & PH_ROUTE_MASK) | ((type & PH_TYPE_MASK) << PH_TYPE_SHIFT);
    }

    // Helper: set path with N hops of hash_size bytes each
    void setPath(const uint8_t* data, uint8_t hash_size, uint8_t hop_count) {
        path_len = ((hash_size - 1) << 6) | (hop_count & 63);
        memcpy(path, data, hop_count * hash_size);
    }
};

} // namespace mesh

// ---------------------------------------------------------------------------
// Minimal in-memory FILESYSTEM mock
// ---------------------------------------------------------------------------

#include <vector>
#include <map>
#include <string>

class MockFile {
public:
    std::vector<uint8_t>* _buf;
    size_t _pos;
    bool   _write;
    bool   _valid;

    MockFile() : _buf(nullptr), _pos(0), _write(false), _valid(false) {}

    explicit operator bool() const { return _valid; }

    size_t read(uint8_t* dest, size_t len) {
        if (!_valid || _write) return 0;
        size_t avail = _buf->size() - _pos;
        size_t n = len < avail ? len : avail;
        memcpy(dest, _buf->data() + _pos, n);
        _pos += n;
        return n;
    }

    size_t write(const uint8_t* src, size_t len) {
        if (!_valid || !_write) return 0;
        _buf->insert(_buf->end(), src, src + len);
        return len;
    }

    void close() { _valid = false; }
};

class MockFS {
    std::map<std::string, std::vector<uint8_t>> _files;
public:
    MockFile open(const char* path, const char* mode = "r") {
        MockFile f;
        bool writing = (mode && mode[0] == 'w');
        f._write = writing;
        if (writing) {
            _files[path].clear();
            f._buf   = &_files[path];
            f._valid = true;
        } else {
            auto it = _files.find(path);
            if (it != _files.end()) {
                f._buf   = &it->second;
                f._valid = true;
            }
        }
        f._pos = 0;
        return f;
    }

    bool exists(const char* path) { return _files.count(path) > 0; }
    bool remove(const char* path) { return _files.erase(path) > 0; }
    void clear()                  { _files.clear(); }
};

// Alias so ChannelFilter.cpp sees FILESYSTEM
using FILESYSTEM = MockFS;
// File type alias
using File = MockFile;
