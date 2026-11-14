#pragma once
// ============================================================
//  bass_dsp.h  —  oscillators (PolyBLEP), ladder filter, envelope, limiter. Core 1, force-inlined, no libm.
// ============================================================
#include <stdint.h>
#include "config.h"
#include "bass_tables.h"

// PolyBLEP: removes most of the aliasing of a hard edge. t = phase 0..1, dt = phase increment per sample.
static HOT float polyBlep(float t, float dt) {
    if (t < dt)         { t /= dt;          return t + t - t * t - 1.0f; }
    if (t > 1.0f - dt)  { t = (t - 1.0f) / dt; return t * t + t + t + 1.0f; }
    return 0.0f;
}
static HOT float oscSaw(uint32_t ph, float dt) {
    const float t = (float)ph * (1.0f / 4294967296.0f);
    return 2.0f * t - 1.0f - polyBlep(t, dt);
}
static HOT float oscSquare(uint32_t ph, float dt) {                    // 50 % pulse
    const float t = (float)ph * (1.0f / 4294967296.0f);
    float t2 = t + 0.5f; if (t2 >= 1.0f) t2 -= 1.0f;
    return (t < 0.5f ? 1.0f : -1.0f) + polyBlep(t, dt) - polyBlep(t2, dt);
}
static HOT float oscTri(uint32_t ph) {                                  // naive: harmonics fall as 1/n^2, aliasing is negligible
    const float t = (float)ph * (1.0f / 4294967296.0f);
    return 4.0f * (t < 0.5f ? t : 1.0f - t) - 1.0f;
}
static HOT float noiseF(uint32_t& s) { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return (float)(int32_t)s * (1.0f / 2147483648.0f); }

// tanh-like Pade [3/2]; reaches exactly 1.0 at |x| = 3 and is clamped there (see the string machine's gotcha: a clamp at 4 overshoots)
static HOT float softSat(float x) {
    if (x >  3.0f) return  1.0f;
    if (x < -3.0f) return -1.0f;
    const float x2 = x * x;
    return x * (27.0f + x2) / (27.0f + 9.0f * x2);
}

// ---- Moog-style 4-pole ladder (Huovilainen topology), exact coefficients from bass_tables ----
struct Ladder {
    float s1 = 0, s2 = 0, s3 = 0, s4 = 0;
    float g = 0.05f, gStep = 0.0f, k = 0.0f;
    HOT void setTarget(float gT) { gStep = (gT - g) * (1.0f / (float)CONTROL_DIV); }
    void reset(float g0) { s1 = s2 = s3 = s4 = 0.0f; g = g0; gStep = 0.0f; }
    HOT bool sane() const { return s1 > -16.0f && s1 < 16.0f && s2 > -16.0f && s2 < 16.0f && s3 > -16.0f && s3 < 16.0f && s4 > -16.0f && s4 < 16.0f; }
    HOT float tick(float in) {
        g += gStep;
        const float t0 = softSat(in - k * s4);
        const float t1 = softSat(s1), t2 = softSat(s2), t3 = softSat(s3), t4 = softSat(s4);
        s1 += g * (t0 - t1);
        s2 += g * (t1 - t2);
        s3 += g * (t2 - t3);
        s4 += g * (t3 - t4);
        return s4;
    }
};

// ---- ADSR: linear attack, exponential decay / release (like an analog RC), advanced once per control tick ----
enum : uint8_t { E_IDLE, E_ATTACK, E_DECAY, E_SUSTAIN, E_RELEASE };
struct Env {
    float level = 0.0f; uint8_t stage = E_IDLE;
    HOT void trigger()  { stage = E_ATTACK; }
    HOT void release()  { if (stage != E_IDLE) stage = E_RELEASE; }
    HOT void kill()     { stage = E_IDLE; level = 0.0f; }
    // aRate: attack increment per tick; dC / rC: per-tick decay coefficients; sus: sustain level
    HOT void step(float aRate, float dC, float sus, float rC) {
        switch (stage) {
            case E_ATTACK:  level += aRate; if (level >= 1.0f) { level = 1.0f; stage = E_DECAY; } break;
            case E_DECAY:   level = sus + (level - sus) * dC; if (level - sus < 1e-4f && level - sus > -1e-4f) { level = sus; stage = E_SUSTAIN; } break;
            case E_SUSTAIN: level = sus; break;
            case E_RELEASE: level *= rC; if (level < 1e-4f) { level = 0.0f; stage = E_IDLE; } break;
            default: break;
        }
    }
};
