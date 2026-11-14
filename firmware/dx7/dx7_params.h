#pragma once
// ============================================================
//  dx7_params.h  —  the eight performance knobs (plus the eight effects knobs), their value mappings
//  The knobs do NOT edit the patch: they bend what the patch does, so a bank loaded by SysEx still plays as it was programmed.
//  Every knob is 0..127; 64 is neutral for the bipolar ones.
// ============================================================
#include <stdint.h>
#include <math.h>
#include "config.h"

enum ParamId : uint8_t { P_MOD, P_BRIT, P_ATK, P_REL, P_FBK, P_TUNE, P_VOL, P_VEL,               // page 1: the voice
                         P_WAH, P_WPOS, P_WLFO, P_TREM, P_TRATE, P_CHOR, P_REV, P_RSIZE,        // page 2: the effects (same as the piano)
                         N_PARAMS };
#define PARAMS_PER_PAGE 8
struct ParamInfo { const char* label; const char* name; const char* cmd; uint8_t cc; };
static const ParamInfo PARAM_INFO[N_PARAMS] = {
    { "MOD",  "Vibrato",     "mod",     1  },     // CC1 = mod wheel
    { "BRIT", "Brightness",  "bright",  74 },
    { "ATK",  "Attack",      "attack",  73 },
    { "REL",  "Release",     "rel",     72 },
    { "FBK",  "Feedback",    "fbk",     20 },
    { "TUNE", "Tune",        "tune",    21 },
    { "VOL",  "Volume",      "vol",      7 },
    { "VEL",  "Vel. sens",   "vel",     22 },
    { "WAH",  "Wah",         "wah",     80 },
    { "WPOS", "Wah pedal",   "wpos",    11 },
    { "WLFO", "Wah auto",    "wlfo",    81 },
    { "TREM", "Trem / pan",  "trem",    82 },
    { "TRAT", "Trem rate",   "trate",   83 },
    { "CHOR", "Chorus",      "chor",    93 },
    { "REV",  "Reverb",      "rev",     91 },
    { "RSIZ", "Reverb time", "rsize",   84 },
};
static const uint8_t PARAM_DEFAULT[N_PARAMS] = { 0, 64, 64, 64, 64, 64, 100, 64,   0, 64, 0, 64, 64, 0, 0, 64 };

// ---- value mappings (all on core 0) ---------------------------------------------------------------------------------
static inline float volGainOf(uint8_t v)   { const float f = v / 127.0f; return f * f * OUT_GAIN; }
static inline int   brightUnitsOf(uint8_t v){ return ((int)v - 64) * 12; }                 // added to every MODULATOR's level: +-768 units = +-3 octaves of modulation index (+-18 dB)
static inline int   attackShiftOf(uint8_t v){ return ((int)v - 64) / 4; }                  // added to the attack rate: +-16 of 63 = x16 faster / slower
static inline int   releaseShiftOf(uint8_t v){ return ((int)v - 64) / 4; }
static inline int   feedbackShiftOf(uint8_t v){ return ((int)v - 64) / 10; }               // -6..+6 steps of feedback (clamped to 0..7 later)
static inline float tuneCentsOf(uint8_t v) { return ((float)v - 64.0f) * (50.0f / 64.0f); }
static inline float velFactorOf(uint8_t v) { return (float)v / 64.0f; }                    // 1.0 = as programmed, 0 = velocity ignored, 2 = doubled
static inline float modDepthOf(uint8_t v)  { return (float)v / 127.0f; }                   // vibrato added to the patch's own pitch modulation, in the patch's PMS units

static inline uint8_t paramForCC(uint8_t cc) {
#if AKAI_REMAP
    switch (cc) {
        case AKAI_K1: return P_MOD; case AKAI_K2: return P_BRIT; case AKAI_K3: return P_ATK; case AKAI_K4: return P_REL;
        case AKAI_K5: return P_FBK; case AKAI_K6: return P_TUNE; case AKAI_K7: return P_VOL; case AKAI_K8: return P_VEL;
        default: break;
    }
#endif
    for (uint8_t i = 0; i < N_PARAMS; i++) if (PARAM_INFO[i].cc == cc) return i;
    return 0xFF;
}
