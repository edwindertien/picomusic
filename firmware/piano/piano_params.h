#pragma once
// ============================================================
//  piano_params.h  —  the eight knobs, their value mappings, keyboard geometry
//  Every knob is 0..127. The mappings are shared by core 0 (which turns them into coefficients), the display and the tests.
// ============================================================
#include <stdint.h>
#include <math.h>
#include "config.h"

enum ParamId : uint8_t { P_VOL, P_TONE, P_HARD, P_REL, P_DECAY, P_WIDTH, P_TUNE, P_SOFT,           // page 1: the piano
                         P_WAH, P_WPOS, P_WLFO, P_TREM, P_TRATE, P_CHOR, P_REV, P_RSIZE,        // page 2: the effects
                         N_PARAMS };
#define PARAMS_PER_PAGE 8
enum ParamFmt : uint8_t { F_INT, F_PCT, F_CENTS, F_TIME, F_TONE, F_LFO, F_TREM, F_RATE, F_REVT };
struct ParamInfo { const char* label; const char* name; const char* cmd; uint8_t cc, col, row, fmt; };
static const ParamInfo PARAM_INFO[N_PARAMS] = {
    //  label   name            cmd      cc  col row fmt
    { "VOL",  "Volume",      "vol",     7, 0, 0, F_INT   },
    { "TONE", "Tone",        "tone",   74, 1, 0, F_TONE  },
    { "HARD", "Hardness",    "hard",   20, 2, 0, F_PCT   },
    { "REL",  "Damper",      "rel",    72, 3, 0, F_PCT   },
    { "DEC",  "Decay",       "decay",  21, 4, 0, F_TIME  },
    { "WIDE", "Stereo width","width",  22, 5, 0, F_PCT   },
    { "TUNE", "Tune",        "tune",   23, 6, 0, F_CENTS },
    { "SOFT", "Una corda",   "soft",   24, 7, 0, F_PCT   },
    { "WAH",  "Wah",         "wah",    80, 0, 0, F_PCT   },
    { "WPOS", "Wah pedal",   "wpos",   11, 1, 0, F_PCT   },       // CC11 = expression pedal
    { "WLFO", "Wah auto",    "wlfo",   81, 2, 0, F_LFO   },
    { "TREM", "Trem / pan",  "trem",   82, 3, 0, F_TREM  },
    { "TRAT", "Trem rate",   "trate",  83, 4, 0, F_RATE  },
    { "CHOR", "Chorus",      "chor",   93, 5, 0, F_PCT   },
    { "REV",  "Reverb",      "rev",    91, 6, 0, F_PCT   },
    { "RSIZ", "Reverb time", "rsize",  84, 7, 0, F_REVT  },
};
static const uint8_t PARAM_DEFAULT[N_PARAMS] = { 100, 64, 64, 64, 127, 64, 64, 0,   0, 64, 0, 64, 64, 0, 0, 64 };

// ---- value mappings -------------------------------------------------------------------
static inline float volGainOf(uint8_t v)  { const float f = v / 127.0f; return f * f * OUT_GAIN; }
static inline float toneMulOf(uint8_t v)  { return v < 64 ? powf(2.0f, ((float)v - 64.0f) * (4.0f / 64.0f)) : powf(2.0f, ((float)v - 64.0f) * (3.0f / 63.0f)); }   // 64 = neutral; 0 = cutoff / 16 (1.25 kHz); 127 = x8 (soft notes brighter)
static inline float hardExpOf(uint8_t v)  { return 0.9f + 1.0f * v / 127.0f; }                                  // velocity -> gain exponent, 0.9 .. 1.9 (default 64: 1.4)
static inline float relScaleOf(uint8_t v) { return powf(2.0f, ((float)v - 64.0f) * (3.0f / 127.0f)); }          // damper time x0.35 .. x2.8
static inline bool  decayOffOf(uint8_t v) { return v >= 124; }                                                  // 127 = the sample's own decay
static inline float decayT60Of(uint8_t v) { return 1.0f * powf(40.0f, v / 124.0f); }                            // extra decay, time to -60 dB: 1 s .. 40 s
static inline float widthOf(uint8_t v)    { return 0.6f * v / 127.0f; }                                         // 0 = mono, 1.0 = hard-panned ends of the keyboard (0.6 max)
static inline float tuneCentsOf(uint8_t v){ return ((float)v - 64.0f) * (50.0f / 64.0f); }
static inline float softOf(uint8_t v)     { return v / 127.0f; }

// ---- keyboard geometry (display and tests) -----------------------------------------------
static inline bool keyIsBlack(int k) { const int n = k % 12; return n == 1 || n == 3 || n == 6 || n == 8 || n == 10; }
static inline int  whiteIndexOf(int k) {                                       // number of white keys BELOW key k: a white key's own index (A0 = 0); for a black key, the index of the white key just above it
    int w = 0; for (int i = KEY_LO; i < k; i++) if (!keyIsBlack(i)) w++; return w; }
#define N_WHITE 52

// ---- CC -> parameter (the AKAI knobs first) ----------------------------------------------------
static inline uint8_t paramForCC(uint8_t cc) {
#if AKAI_REMAP
    switch (cc) {
        case AKAI_K1: return P_TONE; case AKAI_K2: return P_HARD; case AKAI_K3: return P_REL; case AKAI_K4: return P_DECAY;
        case AKAI_K5: return P_WIDTH; case AKAI_K6: return P_TUNE; case AKAI_K7: return P_VOL; case AKAI_K8: return P_SOFT;
        default: break;
    }
#endif
    for (uint8_t i = 0; i < N_PARAMS; i++) if (PARAM_INFO[i].cc == cc) return i;
    return 0xFF;
}
