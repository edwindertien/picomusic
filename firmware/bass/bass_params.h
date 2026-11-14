#pragma once
// ============================================================
//  bass_params.h  —  parameters, value mappings, patches, demo patterns
//
//  33 parameters on two knob pages (OSC, FILT). Every knob is 0..127. Mapping functions are shared by the voice, the
//  display and the tests.
// ============================================================
#include <stdint.h>
#include <math.h>
#include "config.h"

enum ParamId : uint8_t {
    // OSC page
    P_WAV1, P_WAV2, P_WAV3, P_OCT1, P_OCT2, P_OCT3, P_DET2, P_DET3,
    P_MIX1, P_MIX2, P_MIX3, P_SUB, P_NOISE, P_GLIDE, P_VOL,
    // FILT page
    P_CUT, P_RES, P_DRV, P_KEY, P_ENV, P_VEL, P_ACC, P_GATE,
    P_FA, P_FD, P_FS, P_FR, P_AA, P_AD, P_AS, P_AR,
    P_TEMPO, P_SWING,
    N_PARAMS
};
enum ParamGroup : uint8_t { G_OSC, G_MIX, G_FILT, G_FENV, G_AENV, G_PLAY };      // colour order, see lcd.cpp
enum ParamFmt   : uint8_t { F_INT, F_WAVE, F_OCT, F_CENTS, F_GLIDE, F_HZ, F_PCT, F_OCTS, F_TIME, F_BPM };
struct ParamInfo { const char* label; const char* name; const char* cmd; uint8_t cc, group, page, col, row, fmt; };
static const ParamInfo PARAM_INFO[N_PARAMS] = {
    //  label   name            cmd      cc  group    page col row fmt
    { "WAV1", "Osc1 wave",   "wav1",   14, G_OSC,  0, 0, 0, F_WAVE },
    { "WAV2", "Osc2 wave",   "wav2",   15, G_OSC,  0, 1, 0, F_WAVE },
    { "WAV3", "Osc3 wave",   "wav3",   16, G_OSC,  0, 2, 0, F_WAVE },
    { "OCT1", "Osc1 octave", "oct1",   17, G_OSC,  0, 3, 0, F_OCT  },
    { "OCT2", "Osc2 octave", "oct2",   18, G_OSC,  0, 4, 0, F_OCT  },
    { "OCT3", "Osc3 octave", "oct3",   19, G_OSC,  0, 5, 0, F_OCT  },
    { "DET2", "Osc2 detune", "det2",   20, G_OSC,  0, 6, 0, F_CENTS},
    { "DET3", "Osc3 detune", "det3",   21, G_OSC,  0, 7, 0, F_CENTS},
    { "MIX1", "Osc1 level",  "mix1",   22, G_MIX,  0, 0, 1, F_INT  },
    { "MIX2", "Osc2 level",  "mix2",   23, G_MIX,  0, 1, 1, F_INT  },
    { "MIX3", "Osc3 level",  "mix3",   24, G_MIX,  0, 2, 1, F_INT  },
    { "SUB",  "Sub level",   "sub",    25, G_MIX,  0, 3, 1, F_INT  },
    { "NOIS", "Noise level", "noise",  26, G_MIX,  0, 4, 1, F_INT  },
    { "GLID", "Glide",       "glide",   5, G_PLAY, 0, 5, 1, F_GLIDE},
    { "VOL",  "Volume",      "vol",     7, G_PLAY, 0, 6, 1, F_INT  },
    { "CUT",  "Cutoff",      "cut",    74, G_FILT, 1, 0, 0, F_HZ   },
    { "RES",  "Resonance",   "res",    71, G_FILT, 1, 1, 0, F_PCT  },
    { "DRV",  "Drive",       "drive",  27, G_FILT, 1, 2, 0, F_INT  },
    { "KEY",  "Key track",   "key",    28, G_FILT, 1, 3, 0, F_PCT  },
    { "ENV",  "Env amount",  "env",    29, G_FENV, 1, 4, 0, F_OCTS },
    { "VEL",  "Velocity",    "vel",    30, G_FENV, 1, 5, 0, F_PCT  },
    { "ACC",  "Accent",      "acc",    31, G_FENV, 1, 6, 0, F_PCT  },
    { "GATE", "Gate length", "gate",   32, G_PLAY, 1, 7, 0, F_PCT  },
    { "F-A",  "Filter att",  "fa",     52, G_FENV, 1, 0, 1, F_TIME },
    { "F-D",  "Filter dec",  "fd",     53, G_FENV, 1, 1, 1, F_TIME },
    { "F-S",  "Filter sus",  "fs",     54, G_FENV, 1, 2, 1, F_PCT  },
    { "F-R",  "Filter rel",  "fr",     55, G_FENV, 1, 3, 1, F_TIME },
    { "A-A",  "Amp att",     "aa",     73, G_AENV, 1, 4, 1, F_TIME },
    { "A-D",  "Amp dec",     "ad",     75, G_AENV, 1, 5, 1, F_TIME },
    { "A-S",  "Amp sus",     "as",     56, G_AENV, 1, 6, 1, F_PCT  },
    { "A-R",  "Amp rel",     "ar",     72, G_AENV, 1, 7, 1, F_TIME },
    { "BPM",  "Tempo",       "tempo",  57, G_PLAY, 1, 0, 2, F_BPM  },
    { "SWNG", "Swing",       "swing",  58, G_PLAY, 1, 1, 2, F_PCT  },
};

// ---- value mappings (0..127 -> physical) ---------------------------------------------------------
static inline int   waveOf(uint8_t v)    { return (v * 3) >> 7; }                       // 0 saw, 1 square, 2 triangle
static inline int   octOf(uint8_t v)     { return (int)(v >> 5) - 1; }                  // -1, 0, +1, +2 octaves
static inline float centsOf(uint8_t v)   { return ((float)v - 64.0f) * (50.0f / 64.0f); } // +-50 cents
static inline float mixOf(uint8_t v)     { const float f = v / 127.0f; return f * f; }
static inline float cutOctOf(uint8_t v)  { return (float)v * (FILTER_OCTAVES / 127.0f); }  // octaves above FILTER_MIN_HZ
static inline float cutHzOf(uint8_t v)   { return FILTER_MIN_HZ * powf(2.0f, cutOctOf(v)); }
static inline float resRatioOf(uint8_t v){ return 1.08f * powf(v / 127.0f, 1.6f); }       // feedback / critical feedback: > 1 self-oscillates
static inline float driveOf(uint8_t v)   { const float f = v / 127.0f; return 1.0f + 5.0f * f * f; }
static inline float envOctsOf(uint8_t v) { return v * (6.0f / 127.0f); }                  // filter envelope depth in octaves
static inline float attackSecOf(uint8_t v){ return 0.001f * powf(2000.0f, v / 127.0f); }  // 1 ms .. 2 s
static inline float timeSecOf(uint8_t v) { return 0.005f * powf(600.0f, v / 127.0f); }    // 5 ms .. 3 s: time to within 1 % of the target
static inline float glideSecOf(uint8_t v){ return 0.01f * powf(300.0f, v / 127.0f); }     // 10 ms .. 3 s (0 = off)
static inline float gateFracOf(uint8_t v){ return 0.10f + 0.85f * v / 127.0f; }           // 10 .. 95 % of a step
static inline float bpmFromKnob(uint8_t v)   { return (float)(TEMPO_KNOB_BASE + v); }
static inline float swingFromKnob(uint8_t v) { return 0.5f + SWING_MAX * (float)v / 127.0f; }
static inline float decayCoef(float seconds) { return expf(-4.6f * (float)CONTROL_DIV / ((float)SAMPLE_RATE * seconds)); }   // per control tick

// ---- sequence step data --------------------------------------------------------------------------
enum : uint8_t { F_GATE = 1, F_SLIDE = 2, F_ACCENT = 4 };
#define NOTE_MIN 12
#define NOTE_MAX 96
static const char* const NOTE_NAMES[12] = { "C","C#","D","D#","E","F","F#","G","G#","A","A#","B" };
static inline void noteName(int n, char* out) {                                   // MIDI 36 = C2
    const int o = n / 12 - 1; const char* s = NOTE_NAMES[n % 12];
    int k = 0; while (*s) out[k++] = *s++;
    if (o < 0) { out[k++] = '-'; out[k++] = (char)('0' + (-o)); } else out[k++] = (char)('0' + o);
    out[k] = 0;
}
// "C2" "C#2" "D#3s" "C2a" "G1sa" and "." for a rest.  s = slide into the next step, a = accent
static inline bool parseStep(const char* t, uint8_t& note, uint8_t& flags) {
    note = 36; flags = 0;
    if (t[0] == '.' || t[0] == 0) return true;
    static const int semi[7] = { 9, 11, 0, 2, 4, 5, 7 };                         // A B C D E F G
    if (t[0] < 'A' || t[0] > 'G') return false;
    int n = semi[t[0] - 'A']; int i = 1;
    if (t[i] == '#') { n++; i++; }
    if (t[i] < '0' || t[i] > '9') return false;
    n += (t[i] - '0' + 1) * 12; i++;
    flags = F_GATE;
    for (; t[i]; i++) { if (t[i] == 's') flags |= F_SLIDE; else if (t[i] == 'a') flags |= F_ACCENT; else return false; }
    note = (uint8_t)n; return true;
}

// ---- patches: values in ParamId order; TEMPO and SWING are NOT changed by a patch ------------------
#define N_PATCHES 4
struct Patch { const char* name; uint8_t v[N_PARAMS]; };
static const Patch PATCHES[N_PATCHES] = {
  { "Moog Bass", {  10,  10,  60,  40,  40,   8,  72,  56,    100,  90,  60,   0,   0,  30,  98,
                    45,  50,  25,  50,  80,  40,  60,  70,      0,  60,  25,  45,    0,  80, 110,  35,    60, 0 } },
  { "Sub",       { 100, 100,  60,  40,  40,   8,  64,  64,    110,   0,   0,  90,   0,  20,     84,
                    40,  10,  10,  40,  30,  30,  40,  85,      0,  50,  60,  40,    4,  70, 120,  40,    60, 0 } },
  { "Acid",      {  10,  10,  60,  40,  40,   8,  64,  64,    115,   0,   0,   0,   0,  35, 105,
                    30, 108,  60,  15,  95,  30, 110,  55,      0,  40,   0,  30,    0,  70, 110,  25,    60, 0 } },
  { "Pluck",     {  60,  10,  60,  40,  40,   8,  70,  64,    110,  90,   0,  60,   0,   0, 127,
                    38,  60,  40,  60,  90,  60,  70,  80,      0,  40,   0,  30,    0,  60,  45,  35,    60, 0 } },
};

// ---- demo patterns (16 steps each) ------------------------------------------------------------------
#define N_DEMO 4
static const char* const DEMO_STEPS[N_DEMO][N_STEPS] = {
    { "C2","." ,"C2","." ,"." ,"C3a",".","C2", "D#2",".","D#2",".", "F2",".","G2s","G1" },                   // Groove
    { "C2","C2","C3a",".", "C2","D#2s","F2",".", "G1a","C2",".","C3s", "D#3",".","C2a","." },                // Acid
    { "C1",".",".","C1",   ".",".","C1",".",    "G1",".",".","G1",   ".",".","F1","." },                      // Sub pulse
    { "C2","C3","C2","C3a","D#2","D#3","D#2","D#3a","F2","F3","F2","F3a","G1","G2","G1","G2a" },            // Octave bounce
};
static const char* const DEMO_NAMES[N_DEMO] = { "Groove", "Acid", "Sub pulse", "Octaves" };
static const uint8_t DEMO_PATCH[N_DEMO] = { 0, 2, 1, 3 };

#define PAGE_COUNT 4     // OSC, FILT, SEQ, SETUP (lcd.cpp)

// ---- CC -> parameter (the AKAI knobs first) -----------------------------------------------------------------
static inline uint8_t paramForCC(uint8_t cc) {
#if AKAI_REMAP
    switch (cc) {
        case AKAI_K1: return P_CUT;  case AKAI_K2: return P_RES; case AKAI_K3: return P_ENV; case AKAI_K4: return P_FD;
        case AKAI_K5: return P_GLIDE; case AKAI_K6: return P_DRV; case AKAI_K7: return P_VOL; case AKAI_K8: return P_ACC;
        default: break;
    }
#endif
    for (uint8_t i = 0; i < N_PARAMS; i++) if (PARAM_INFO[i].cc == cc) return i;
    return 0xFF;
}
