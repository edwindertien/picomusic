#pragma once
// ============================================================
//  drum_hex.h  —  hex text <-> bytes, for the serial commands  dump  and  image <hex>
//  (tools/drums talks to the machine with them; host-tested in tests/drums/test_hex.cpp)
// ============================================================
#include <stdint.h>
#include <stddef.h>

static inline char drumHexDigit(uint8_t v) { return (char)(v < 10 ? '0' + v : 'a' + v - 10); }

// out needs 2*n bytes (no terminator written)
static inline size_t drumHexEncode(const uint8_t* in, size_t n, char* out) {
    for (size_t i = 0; i < n; i++) { out[2 * i] = drumHexDigit(in[i] >> 4); out[2 * i + 1] = drumHexDigit(in[i] & 15); }
    return 2 * n;
}

static inline int drumHexVal(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

// Accepts upper/lower case; fails (false) on an odd length, a non-hex character or an image longer than cap. len = bytes written.
static inline bool drumHexDecode(const char* s, size_t n, uint8_t* out, size_t cap, size_t& len) {
    len = 0;
    if (n & 1) return false;
    if (n / 2 > cap) return false;
    for (size_t i = 0; i < n; i += 2) {
        const int h = drumHexVal(s[i]), l = drumHexVal(s[i + 1]);
        if (h < 0 || l < 0) return false;
        out[len++] = (uint8_t)(h * 16 + l);
    }
    return true;
}
