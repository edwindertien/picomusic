#pragma once
// ============================================================
//  drum_dsp.h  —  small DSP primitives for the drum voices (core 1, force-inlined, no libm)
// ============================================================
#include <stdint.h>
#include <math.h>
#include "config.h"
#include "wavetable.h"

static inline float hzToInc(float hz)      { return hz * (4294967296.0f / (float)SAMPLE_RATE); }
// per-control-tick decay coefficient for an exponential decay with time constant `tau` seconds (core 0)
static inline float expDecayCoef(float tau) { return expf(-(float)CONTROL_DIV / ((float)SAMPLE_RATE * tau)); }
// one-pole coefficient for a corner frequency (core 0)
static inline float onePoleCoef(float hz)   { return 1.0f - expf(-6.2831853f * hz / (float)SAMPLE_RATE); }

// sine of a 32-bit phase, linearly interpolated from the 4096-point table: |error| < 3e-7, harmonics < -120 dB
static HOT float sineF(uint32_t ph) {
    const uint32_t i  = ph >> PHASE_SHIFT;
    const float    fr = (float)(ph & ((1u << PHASE_SHIFT) - 1u)) * (1.0f / (float)(1u << PHASE_SHIFT));
    const float    a  = (float)Wavetable::sine(i), b = (float)Wavetable::sine(i + 1u);
    return (a + (b - a) * fr) * (1.0f / 32767.0f);
}

// white noise in [-1, 1) (xorshift32); each caller owns its state
static HOT float noiseF(uint32_t& s) {
    s ^= s << 13; s ^= s >> 17; s ^= s << 5;
    return (float)(int32_t)s * (1.0f / 2147483648.0f);
}

static HOT float lp1(float x, float a, float& z) { z += a * (x - z); return z; }        // one-pole low-pass
static HOT float hp1(float x, float a, float& z) { z += a * (x - z); return x - z; }    // one-pole high-pass

// tanh-like soft limiter (linear for small signals)
static HOT float softSat(float x) {
    if (x >  3.0f) return  1.0f;          // the curve reaches exactly 1.0 at |x| = 3 and would keep rising (1.006 at 4):
    if (x < -3.0f) return -1.0f;          // clamping at 3 guarantees |result| <= 1, so the int16 conversion can never wrap
    const float x2 = x * x;
    return x * (27.0f + x2) / (27.0f + 9.0f * x2);
}
