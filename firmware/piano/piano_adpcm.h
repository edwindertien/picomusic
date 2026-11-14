#pragma once
// ============================================================
//  piano_adpcm.h  —  "codec 2": 4-bit ADPCM with a 4-tap adaptive predictor. Shared by the firmware decoder and the host-side
//  encoder (tools/adpcm_tool.cpp), so the two can never disagree about the tables, the predictor or the reconstruction rule.
//
//  * The quantiser is IMA ADPCM: the same 89-entry step table and index adaptation, 1 sign bit + 3 magnitude bits.
//  * The PREDICTOR is new. Plain IMA predicts "the previous sample"; a piano note is a few decaying sinusoids that four taps
//    anticipate far better, so the residual the 4 bits have to code is much smaller, and so is the quantisation noise:
//    measured on the shipped grand, +5 .. +11 dB SNR (C4 38.3 -> 45.5 dB, C7 28.5 -> 39.7 dB).
//  * Backward-adaptive, integer only (so the PC encoder and the Pico agree to the bit): Q13 weights, a sign-error LMS update
//    w[k] += sign(r - p) * h[k] >> LMS_SHIFT on the reconstructed signal, no side information in the stream.
//  * A stream is a run of bytes, two samples per byte, LOW nibble first. A zone starts from init(first sample, step index 0);
//    a loop restores the full state saved at its start (restore()).
//  * A zone may START with a few milliseconds of plain 16-bit PCM (the hammer strike: the one place where 4 bits cannot follow the signal, measured 27-29 dB SNR
//    against 46 dB elsewhere). push() feeds those samples into the predictor history, so the ADPCM stream that follows starts from the true history.
//  Cost in the decoder: 4 multiply-accumulates (64-bit) + 4 shift-adds per sample.
// ============================================================
#include <stdint.h>

#define ADPCM_CODEC 3                               // 3 = codec 2 (IMA quantiser + 4-tap LMS predictor) with zones that may start with a PCM head, see piano_bank.h
#define LMS_TAPS 4
#ifndef LMS_SHIFT
#define LMS_SHIFT 7
#endif
#define LMS_WMAX 32000                              // weights are stored as int16 in the bank (a loop state): keep them inside

static const int16_t ADPCM_STEP[89] = {
    7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 50, 55, 60, 66, 73, 80, 88, 97, 107, 118, 130, 143, 157, 173, 190, 209, 230, 253, 279, 307,
    337, 371, 408, 449, 494, 544, 598, 658, 724, 796, 876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428, 4871, 5358,
    5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767 };
static const int8_t ADPCM_INDEX[16] = { -1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8 };

struct AdpcmState {
    int32_t pred = 0, idx = 0;                      // pred == h[0]: the last reconstructed sample
    int32_t h[LMS_TAPS] = { 0, 0, 0, 0 };           // reconstructed history, h[0] newest
    int32_t w[LMS_TAPS] = { 1 << 13, 0, 0, 0 };     // predictor weights, Q13 (start as "the previous sample", like IMA)

    inline void init(int32_t first, int32_t index) { pred = first; idx = index; for (int k = 0; k < LMS_TAPS; k++) { h[k] = first; w[k] = k ? 0 : (1 << 13); } }
    inline void push(int16_t s) { for (int k = LMS_TAPS - 1; k > 0; k--) h[k] = h[k - 1]; h[0] = s; pred = s; }      // a PCM sample passes by: the history follows it, weights and step index stay
    inline void restore(const int16_t* hh, const int16_t* ww, int32_t index) { pred = hh[0]; idx = index; for (int k = 0; k < LMS_TAPS; k++) { h[k] = hh[k]; w[k] = ww[k]; } }

    inline int32_t predict() const {
        int64_t s = 0; for (int k = 0; k < LMS_TAPS; k++) s += (int64_t)w[k] * h[k];
        int32_t p = (int32_t)(s >> 13); if (p > 32767) p = 32767; else if (p < -32768) p = -32768; return p;
    }
    // reconstruct one sample from a 4-bit code
    inline int16_t decode(uint8_t code) {
        const int32_t p = predict(), step = ADPCM_STEP[idx];
        int32_t vp = step >> 3;
        if (code & 4) vp += step;
        if (code & 2) vp += step >> 1;
        if (code & 1) vp += step >> 2;
        int32_t r = p + ((code & 8) ? -vp : vp);
        if (r > 32767) r = 32767; else if (r < -32768) r = -32768;
        idx += ADPCM_INDEX[code & 15];
        if (idx < 0) idx = 0; else if (idx > 88) idx = 88;
        const int e = (r > p) - (r < p);            // sign of what the quantiser added to the prediction
        if (e) for (int k = 0; k < LMS_TAPS; k++) { int32_t nw = w[k] + ((e * h[k]) >> LMS_SHIFT); if (nw > LMS_WMAX) nw = LMS_WMAX; else if (nw < -LMS_WMAX) nw = -LMS_WMAX; w[k] = nw; }
        for (int k = LMS_TAPS - 1; k > 0; k--) h[k] = h[k - 1];
        h[0] = r; pred = r;
        return (int16_t)r;
    }
    // the matching encoder step (host tools only)
    inline uint8_t encode(int16_t x) {
        int32_t diff = (int32_t)x - predict(); const int sign = diff < 0; if (sign) diff = -diff;
        int32_t step = ADPCM_STEP[idx]; uint8_t code = 0;
        if (diff >= step) { code |= 4; diff -= step; } step >>= 1;
        if (diff >= step) { code |= 2; diff -= step; } step >>= 1;
        if (diff >= step) { code |= 1; }
        code |= (uint8_t)(sign << 3);
        decode(code);                               // advance the state exactly as the decoder will
        return code;
    }
};
