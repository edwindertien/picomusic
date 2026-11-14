#pragma once
// ============================================================
//  bass_tables.h  —  tables built ONCE at boot (core 0, libm allowed), read-only afterwards (core 1)
//
//  1. Ladder coefficients. The filter stage is  s += g*(sat(u) - sat(s)),  four times, and every stage is computed
//     from the PREVIOUS sample's state of the stage before it (and of s4 for the resonance feedback). Each stage is
//     therefore  H(z) = g / (z - 1 + g)  (that z already carries the one-sample delay) and the loop is simply
//         L(z) = k * H(z)^4 .
//     It self-oscillates where the phase is -180 degrees, i.e. arg(z - 1 + g) = pi/4, which has the closed form
//
//         g       = 1 - cos(w) + sin(w)                    pole coefficient that puts the resonance AT w = 2 pi f / fs
//         k_crit  = 4 sin(w)^4 / g^4                       feedback gain at which it just self-oscillates (-> 4 for low f)
//
//     (picosynth uses g = tan(pi f / fs), which puts the real pole at about HALF the nominal frequency, drifting with
//     frequency: fine for a general synth, wrong for a bass that must self-oscillate in tune and track the keyboard.)
//     Entry i belongs to f = FILTER_MIN_HZ * 2^(i / FILTER_STEPS_PER_OCTAVE).
//  2. 2^(i/256) for the fast exp2 used by the oscillator pitch.
// ============================================================
#include <stdint.h>
#include "config.h"

#define BT_NF   ((int)(FILTER_OCTAVES * FILTER_STEPS_PER_OCTAVE) + 2)
extern float BT_G[BT_NF + 1];
extern float BT_K[BT_NF + 1];
extern float BT_E2[258];
void bass_tables_init();

// 2^x for |x| < 100, error < 1e-6 relative (0.002 cent)
static HOT float exp2fast(float x) {
    if (x < -100.0f) x = -100.0f;
    if (x >  100.0f) x =  100.0f;
    int n = (int)x; if ((float)n > x) n--;
    const float fi = (x - (float)n) * 256.0f;
    const int   i  = (int)fi;
    const float m  = BT_E2[i] + (BT_E2[i + 1] - BT_E2[i]) * (fi - (float)i);
    union { uint32_t u; float f; } b; b.u = (uint32_t)(n + 127) << 23;
    return m * b.f;
}

// Filter coefficients for a cutoff given in OCTAVES above FILTER_MIN_HZ (linear interpolation in the table)
static HOT void ladderLookup(float oct, float& g, float& kcrit) {
    float x = oct * (float)FILTER_STEPS_PER_OCTAVE;
    if (x < 0.0f) x = 0.0f;
    if (x > (float)(BT_NF - 1)) x = (float)(BT_NF - 1);
    const int i = (int)x; const float fr = x - (float)i;
    g     = BT_G[i] + (BT_G[i + 1] - BT_G[i]) * fr;
    kcrit = BT_K[i] + (BT_K[i + 1] - BT_K[i]) * fr;
}
