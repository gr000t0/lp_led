#pragma once
#include <cstdint>
#include <cstddef>
#include <cstring>
#include "credentials.hpp"

// Pure protocol helpers; also used in host-side tests.
namespace protocol {
inline bool text_field(char* out, size_t capacity, const uint8_t* data, size_t len) {
    if (len >= capacity || (len && (!data || memchr(data, 0, len)))) return false;
    memset(out, 0, capacity);
    if (len) memcpy(out, data, len);
    return true;
}
inline bool rgb(const uint8_t* data, size_t len, uint32_t& out) {
    if (len != 3 || !data) return false;
    out = (uint32_t(data[0]) << 16) | (uint32_t(data[1]) << 8) | data[2];
    return true;
}
// Consecutive prepare-write fragments for ONE attribute, maximum 64 bytes.
// Reject gaps/overlap rather than silently constructing a different credential.
struct PreparedWrite {
    uint16_t handle = 0;
    size_t length = 0;
    uint8_t bytes[64]{};
    void clear() { handle = 0; length = 0; memset(bytes, 0, sizeof(bytes)); }
    bool append(uint16_t h, size_t offset, const uint8_t* data, size_t len) {
        if (!h || (handle && handle != h) || offset != length || len > sizeof(bytes)-length) return false;
        handle = h;
        if (len) memcpy(bytes+length, data, len);
        length += len;
        return true;
    }
};
}
