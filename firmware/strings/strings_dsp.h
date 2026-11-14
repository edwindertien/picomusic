#pragma once
// ============================================================
//  strings_dsp.h  —  shared (all-voices) signal processing
//
//    Svf       2-pole state-variable filter (TPT / Zavalishin): the registration filters
//    Ensemble  3-tap modulated delay, taps 120 degrees apart, slow + fast LFO: THE string-machine sound
//    softSat   tanh-like output limiter (gentle: linear for small signals)
//
//  Everything here runs on core 1 inside tick(): force-inlined, no libm.
// ============================================================
#include <stdint.h>
#include <math.h>
#include "config.h"
#include "wavetable.h"

static HOT float softSat(float x) {
    if (x >  3.0f) return  1.0f;          // the curve reaches exactly 1.0 at |x| = 3 and would keep rising (1.006 at 4):
    if (x < -3.0f) return -1.0f;          // clamping at 3 guarantees |result| <= 1, so the int16 conversion can never wrap
    const float x2 = x * x;
    return x * (27.0f + x2) / (27.0f + 9.0f * x2);
}

// ---- Registration filter -----------------------------------------------------------
struct Svf {
    float a1 = 0, a2 = 0, a3 = 0, k = 1.0f, ic1 = 0, ic2 = 0;

    // core 0, at init only (calls tanf). Coefficients never change afterwards.
    void design(float hz, float q) {
        const float g = tanf(3.14159265f * hz / (float)SAMPLE_RATE);
        k = 1.0f / q;
        a1 = 1.0f / (1.0f + g * (g + k));
        a2 = g * a1;
        a3 = g * a2;
        ic1 = ic2 = 0.0f;
    }
    void reset() { ic1 = ic2 = 0.0f; }
    HOT bool sane() const { return ic1 > -1e6f && ic1 < 1e6f && ic2 > -1e6f && ic2 < 1e6f; }

    // lp = low-pass, bp = band-pass with UNITY peak gain, hp = high-pass
    HOT void tick(float v0, float& lp, float& bp, float& hp) {
        const float v3 = v0 - ic2;
        const float v1 = a1 * ic1 + a2 * v3;
        const float v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2.0f * v1 - ic1;
        ic2 = 2.0f * v2 - ic2;
        lp = v2;
        bp = k * v1;
        hp = v0 - k * v1 - v2;
    }
};

// ---- Ensemble ------------------------------------------------------------------------
//  mono in -> 3 delay taps whose delay times are swept by a slow and a fast sine LFO
//  (phases 0 / 120 / 240 degrees) -> stereo out: tap0 left, tap1 centre, tap2 right.
#define ENS_MASK       (ENS_BUF - 1)
#define ENS_D0         (ENS_D0_MS * 0.001f * (float)SAMPLE_RATE)
#define ENS_AS         (ENS_SLOW_DEPTH_MS * 0.001f * (float)SAMPLE_RATE)
#define ENS_AF         (ENS_FAST_DEPTH_MS * 0.001f * (float)SAMPLE_RATE)
#define ENS_FAST_INC   ((uint32_t)(ENS_FAST_HZ * 4294967296.0f / (float)SAMPLE_RATE))
static_assert(ENS_BUF > 4 * (int)(ENS_D0_MS * 0.001f * SAMPLE_RATE), "ensemble buffer too small");

struct Ensemble {
    float    buf[ENS_BUF];
    uint32_t w = 0, phS = 0, phF = 0;

    void clear() { for (int i = 0; i < ENS_BUF; i++) buf[i] = 0.0f; w = 0; }

    HOT void process(float x, float depth, float shimmer, float mix, uint32_t slowInc, float& L, float& R) {
        buf[w & ENS_MASK] = x;
        phS += slowInc;
        phF += ENS_FAST_INC;
        const float K = 1.0f / 32767.0f;
        float t[3];
        for (int i = 0; i < 3; i++) {
            const uint32_t o  = (uint32_t)i * 0x55555555u;                       // 120 degrees
            const float    ss = (float)Wavetable::sine((phS + o) >> PHASE_SHIFT) * K;
            const float    sf = (float)Wavetable::sine((phF + o) >> PHASE_SHIFT) * K;
            const float    d  = ENS_D0 + depth * (ENS_AS * ss + ENS_AF * shimmer * sf);   // delay, samples (> 0)
            const uint32_t n  = (uint32_t)d;
            const float    fr = d - (float)n;
            const float    a  = buf[(w - n) & ENS_MASK];
            const float    b  = buf[(w - n - 1) & ENS_MASK];
            t[i] = a + (b - a) * fr;                                             // linear interpolation
        }
        w++;
        const float dry = 1.0f - 0.45f * mix;                                    // keeps the level about constant
        const float wet = 0.62f * mix;
        L = x * dry + wet * (t[0] + 0.6f * t[1]);
        R = x * dry + wet * (t[2] + 0.6f * t[1]);
    }
};
