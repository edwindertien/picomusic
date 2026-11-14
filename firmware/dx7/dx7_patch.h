#pragma once
// ============================================================
//  dx7_patch.h  —  the voice ("patch") of a six-operator FM synthesizer, the 32 algorithms, and the 128-byte packed voice
//  format (VMEM) that DX7 32-voice SysEx bank dumps use. No hardware access: used by the firmware, the tests and the render tool.
//
//  Operator order: index 0 is operator 6, index 5 is operator 1 (the order of the packed format AND of the algorithm table,
//  where operators are evaluated first to last).
// ============================================================
#include <stdint.h>
#include <string.h>

struct Dx7Op {
    uint8_t r[4], l[4];          // envelope rates and levels, 0..99
    uint8_t bp, ld, rd, lc, rc;  // key level scaling: break point 0..99 (0 = A-1), left/right depth 0..99, curves 0..3 (-LIN -EXP +EXP +LIN)
    uint8_t rs;                  // rate scaling 0..7
    uint8_t det;                 // detune 0..14 (7 = none)
    uint8_t ams;                 // amplitude modulation sensitivity 0..3
    uint8_t kvs;                 // key velocity sensitivity 0..7
    uint8_t ol;                  // output level 0..99
    uint8_t fixed;               // 0 = frequency ratio, 1 = fixed frequency
    uint8_t coarse, fine;        // coarse 0..31, fine 0..99
};
struct Dx7Patch {
    Dx7Op   op[6];
    uint8_t pr[4], pl[4];        // pitch envelope (stored and round-tripped, not played)
    uint8_t alg;                 // 0..31  (algorithm 1..32)
    uint8_t fb;                  // feedback 0..7
    uint8_t oks;                 // oscillator key sync
    uint8_t lfs, lfd, lpmd, lamd, lfoSync, lfw, lpms;   // LFO speed, delay, pitch depth, amplitude depth, key sync, wave 0..5, pitch sensitivity 0..7
    uint8_t transpose;           // 0..48, 24 = C3 (no transposition)
    char    name[11];
};

// ---- packed voice: 128 bytes ------------------------------------------------------------------------------------------
static inline uint8_t dx7Clamp(int v, int hi) { return (uint8_t)(v < 0 ? 0 : v > hi ? hi : v); }

static inline void dx7Unpack(const uint8_t* v, Dx7Patch& p) {
    for (int o = 0; o < 6; o++) {
        const uint8_t* b = v + o * 17; Dx7Op& q = p.op[o];
        for (int i = 0; i < 4; i++) { q.r[i] = dx7Clamp(b[i] & 0x7F, 99); q.l[i] = dx7Clamp(b[4 + i] & 0x7F, 99); }
        q.bp = dx7Clamp(b[8] & 0x7F, 99); q.ld = dx7Clamp(b[9] & 0x7F, 99); q.rd = dx7Clamp(b[10] & 0x7F, 99);
        q.lc = b[11] & 3; q.rc = (b[11] >> 2) & 3;
        q.rs = b[12] & 7; q.det = dx7Clamp((b[12] >> 3) & 15, 14);
        q.ams = b[13] & 3; q.kvs = (b[13] >> 2) & 7;
        q.ol = dx7Clamp(b[14] & 0x7F, 99);
        q.fixed = b[15] & 1; q.coarse = (b[15] >> 1) & 31; q.fine = dx7Clamp(b[16] & 0x7F, 99);
    }
    for (int i = 0; i < 4; i++) { p.pr[i] = dx7Clamp(v[102 + i] & 0x7F, 99); p.pl[i] = dx7Clamp(v[106 + i] & 0x7F, 99); }
    p.alg = v[110] & 31; p.fb = v[111] & 7; p.oks = (v[111] >> 3) & 1;
    p.lfs = dx7Clamp(v[112] & 0x7F, 99); p.lfd = dx7Clamp(v[113] & 0x7F, 99); p.lpmd = dx7Clamp(v[114] & 0x7F, 99); p.lamd = dx7Clamp(v[115] & 0x7F, 99);
    p.lfoSync = v[116] & 1; p.lfw = dx7Clamp((v[116] >> 1) & 15, 5); p.lpms = (v[116] >> 5) & 7;
    p.transpose = dx7Clamp(v[117] & 0x7F, 48);
    for (int i = 0; i < 10; i++) { const char c = (char)(v[118 + i] & 0x7F); p.name[i] = (c >= 32 && c < 127) ? c : ' '; }
    p.name[10] = 0;
}
static inline void dx7Pack(const Dx7Patch& p, uint8_t* v) {
    memset(v, 0, 128);
    for (int o = 0; o < 6; o++) {
        uint8_t* b = v + o * 17; const Dx7Op& q = p.op[o];
        for (int i = 0; i < 4; i++) { b[i] = q.r[i]; b[4 + i] = q.l[i]; }
        b[8] = q.bp; b[9] = q.ld; b[10] = q.rd; b[11] = (uint8_t)(q.lc | (q.rc << 2));
        b[12] = (uint8_t)(q.rs | (q.det << 3)); b[13] = (uint8_t)(q.ams | (q.kvs << 2));
        b[14] = q.ol; b[15] = (uint8_t)(q.fixed | (q.coarse << 1)); b[16] = q.fine;
    }
    for (int i = 0; i < 4; i++) { v[102 + i] = p.pr[i]; v[106 + i] = p.pl[i]; }
    v[110] = p.alg; v[111] = (uint8_t)(p.fb | (p.oks << 3));
    v[112] = p.lfs; v[113] = p.lfd; v[114] = p.lpmd; v[115] = p.lamd;
    v[116] = (uint8_t)(p.lfoSync | (p.lfw << 1) | (p.lpms << 5));
    v[117] = p.transpose;
    for (int i = 0; i < 10; i++) v[118 + i] = (uint8_t)(p.name[i] ? p.name[i] : ' ');
}

// ---- the 32 algorithms ----------------------------------------------------------------------------------------------
//  One byte per operator (evaluated in index order 0..5):
//    bits 0..1  output bus: 0 = the mix (a carrier), 1 / 2 = modulation bus 1 / 2
//    bit  2     ADD: add to the bus instead of replacing what is in it
//    bits 4..5  input bus: 0 = none, 1 / 2 = modulation bus 1 / 2
//    bit  6     FB_IN: this operator is modulated by the feedback path
//    bit  7     FB_OUT: this operator's output feeds the feedback path (both bits on one operator = self feedback)
//  The routing follows the DX7's published algorithm chart; the table has the layout used by Google's music-synthesizer-for-android
//  (Apache-2.0, see NOTICE.md).
#define DX_FB_IN  0x40
#define DX_FB_OUT 0x80
static const uint8_t DX7_ALG[32][6] = {
    { 0xc1, 0x11, 0x11, 0x14, 0x01, 0x14 }, { 0x01, 0x11, 0x11, 0x14, 0xc1, 0x14 }, { 0xc1, 0x11, 0x14, 0x01, 0x11, 0x14 }, { 0x41, 0x11, 0x94, 0x01, 0x11, 0x14 },
    { 0xc1, 0x14, 0x01, 0x14, 0x01, 0x14 }, { 0x41, 0x94, 0x01, 0x14, 0x01, 0x14 }, { 0xc1, 0x11, 0x05, 0x14, 0x01, 0x14 }, { 0x01, 0x11, 0xc5, 0x14, 0x01, 0x14 },
    { 0x01, 0x11, 0x05, 0x14, 0xc1, 0x14 }, { 0x01, 0x05, 0x14, 0xc1, 0x11, 0x14 }, { 0xc1, 0x05, 0x14, 0x01, 0x11, 0x14 }, { 0x01, 0x05, 0x05, 0x14, 0xc1, 0x14 },
    { 0xc1, 0x05, 0x05, 0x14, 0x01, 0x14 }, { 0xc1, 0x05, 0x11, 0x14, 0x01, 0x14 }, { 0x01, 0x05, 0x11, 0x14, 0xc1, 0x14 }, { 0xc1, 0x11, 0x02, 0x25, 0x05, 0x14 },
    { 0x01, 0x11, 0x02, 0x25, 0xc5, 0x14 }, { 0x01, 0x11, 0x11, 0xc5, 0x05, 0x14 }, { 0xc1, 0x14, 0x14, 0x01, 0x11, 0x14 }, { 0x01, 0x05, 0x14, 0xc1, 0x14, 0x14 },
    { 0x01, 0x14, 0x14, 0xc1, 0x14, 0x14 }, { 0xc1, 0x14, 0x14, 0x14, 0x01, 0x14 }, { 0xc1, 0x14, 0x14, 0x01, 0x14, 0x04 }, { 0xc1, 0x14, 0x14, 0x14, 0x04, 0x04 },
    { 0xc1, 0x14, 0x14, 0x04, 0x04, 0x04 }, { 0xc1, 0x05, 0x14, 0x01, 0x14, 0x04 }, { 0x01, 0x05, 0x14, 0xc1, 0x14, 0x04 }, { 0x04, 0xc1, 0x11, 0x14, 0x01, 0x14 },
    { 0xc1, 0x14, 0x01, 0x14, 0x04, 0x04 }, { 0x04, 0xc1, 0x11, 0x14, 0x04, 0x04 }, { 0xc1, 0x14, 0x04, 0x04, 0x04, 0x04 }, { 0xc4, 0x04, 0x04, 0x04, 0x04, 0x04 },
};
static inline bool dx7IsCarrier(int alg, int idx) { return (DX7_ALG[alg][idx] & 3) == 0; }
static inline int  dx7Carriers(int alg) { int n = 0; for (int i = 0; i < 6; i++) n += dx7IsCarrier(alg, i); return n; }

// ---- level / velocity / scaling tables (units: the log-amplitude scale, 256 units = one octave = 6 dB) -----------------
static const uint8_t DX7_SOL[20]   = { 0, 5, 9, 13, 17, 20, 23, 25, 27, 29, 31, 33, 35, 37, 39, 41, 42, 43, 45, 46 };    // output level 0..19 -> internal 0..127
static inline int dx7Sol(int l) { return l < 20 ? DX7_SOL[l] : 28 + l; }                                                 // 99 -> 127
static const uint8_t DX7_VELDATA[64] = { 0, 70, 86, 97, 106, 114, 121, 126, 132, 138, 142, 148, 152, 156, 160, 163, 166, 170, 173, 174, 178, 181, 184, 186, 189, 190, 194, 196, 198, 200, 202, 205,
                                         206, 209, 211, 214, 216, 218, 220, 222, 224, 225, 227, 229, 230, 232, 233, 235, 237, 238, 240, 241, 242, 243, 244, 246, 246, 248, 249, 250, 251, 252, 253, 254 };
static const uint8_t DX7_EXPSCALE[33] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 11, 14, 16, 19, 23, 27, 33, 39, 47, 56, 66, 80, 94, 110, 126, 142, 158, 174, 190, 206, 222, 238, 250 };

static inline int dx7VelScale(int vel, int sens) {                         // added to the operator level (units of 1/256 octave)
    const int v = DX7_VELDATA[(vel < 0 ? 0 : vel > 127 ? 127 : vel) >> 1] - 239;
    return ((sens * v + 7) >> 3) * 16;
}
static inline int dx7ScaleCurve(int group, int depth, int curve) {
    int s;
    if (curve == 0 || curve == 3) s = (group * depth * 329) >> 12;
    else { const int g = group > 32 ? 32 : group; s = (DX7_EXPSCALE[g] * depth * 329) >> 15; }
    return curve < 2 ? -s : s;
}
static inline int dx7KeyScale(int note, const Dx7Op& o) {                  // in output-level units (before <<5)
    const int off = note - (o.bp + 21);                                    // break point 0 = A-1 (MIDI 21); groups of three keys on either side
    if (off >= 0) return dx7ScaleCurve((off + 2) / 3, o.rd, o.rc);
    return dx7ScaleCurve((-off + 2) / 3, o.ld, o.lc);
}
static inline int dx7RateScale(int note, int rs) {                         // added to the 0..63 internal rate
    int x = note / 3 - 7; x = x < 0 ? 0 : x > 31 ? 31 : x;
    return (rs * x) >> 3;
}

// ---- SysEx: a 32-voice bank dump is  F0 43 0n 09 20 00 <4096 bytes> <checksum> F7 ---------------------------------------
static inline bool dx7BankChecksumOk(const uint8_t* data4096, uint8_t sum) {
    int s = 0; for (int i = 0; i < 4096; i++) s += data4096[i] & 0x7F;
    return (uint8_t)((-s) & 0x7F) == (sum & 0x7F);
}
