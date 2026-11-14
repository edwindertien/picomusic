#pragma once
// ============================================================
//  strings_params.h  —  every user-facing parameter, in one table
//  (same pattern as the analog synth: panel, MIDI, serial and patches all go through setParam())
// ============================================================
#include <stdint.h>
#include <math.h>
#include "config.h"

// Order = joystick navigation order (grouped, not strictly row-major).
enum ParamId : uint8_t {
    P_BASS, P_CELLO, P_VIOLA, P_VIOLIN, P_PIZZ,          // registrations (footage buses -> filters)
    P_ATTACK, P_RELEASE, P_DETUNE,                       // note envelope + unison detune
    P_VIB_DEPTH, P_VIB_RATE, P_PIZ_DECAY,                // vibrato, pizzicato decay
    P_TONE, P_ENS_DEPTH, P_ENS_RATE, P_ENS_MIX, P_ENS_SHIMMER,   // tone + ensemble
    P_VOL,
    N_PARAMS
};
#define N_KNOBS  N_PARAMS    // every parameter has a knob

// Order must match GROUP_COL[] in lcd.cpp
enum ParamGroup : uint8_t { G_REG, G_ENV, G_MOD, G_ENS, G_OUT };
enum ParamFmt   : uint8_t { F_INT, F_SECONDS, F_HZ, F_CENTS };

struct ParamInfo {
    const char* label;    // knob label (<= 4 chars)
    const char* name;     // name shown in the top bar
    const char* cmd;      // serial name:  set <cmd> <0-127>
    uint8_t     cc;       // primary MIDI CC
    uint8_t     group;
    uint8_t     col, row; // knob grid cell
    uint8_t     fmt;      // how the value is shown
};

static const ParamInfo PARAM_INFO[N_PARAMS] = {
    //  label   name            cmd       cc               group   col row fmt
    { "BASS", "Bass 16'",     "bass",   CC_BASS,        G_REG,  0, 0, F_INT },
    { "CELL", "Cello 8'",     "cello",  CC_CELLO,       G_REG,  1, 0, F_INT },
    { "VLA",  "Viola 8'",     "viola",  CC_VIOLA,       G_REG,  2, 0, F_INT },
    { "VLN",  "Violin 4'",    "violin", CC_VIOLIN,      G_REG,  3, 0, F_INT },
    { "PIZZ", "Pizzicato",    "pizz",   CC_PIZZ,        G_REG,  0, 1, F_INT },
    { "ATK",  "Attack",       "atk",    CC_ATTACK,      G_ENV,  1, 1, F_SECONDS },
    { "REL",  "Release",      "rel",    CC_RELEASE,     G_ENV,  2, 1, F_SECONDS },
    { "DET",  "Unison Detune","det",    CC_DETUNE,      G_ENV,  3, 1, F_CENTS },
    { "VIB",  "Vibrato Depth","vib",    CC_VIB_DEPTH,   G_MOD,  0, 2, F_INT },
    { "VRT",  "Vibrato Rate", "vrate",  CC_VIB_RATE,    G_MOD,  1, 2, F_HZ },
    { "PDEC", "Pizz Decay",   "pdec",   CC_PIZ_DECAY,   G_ENV,  2, 2, F_SECONDS },
    { "TONE", "Tone",         "tone",   CC_TONE,        G_OUT,  4, 0, F_INT },
    { "DEPT", "Ensemble Depth","edepth",CC_ENS_DEPTH,   G_ENS,  5, 0, F_INT },
    { "RATE", "Ensemble Rate","erate",  CC_ENS_RATE,    G_ENS,  6, 0, F_HZ },
    { "MIX",  "Ensemble Mix", "emix",   CC_ENS_MIX,     G_ENS,  7, 0, F_INT },
    { "SHIM", "Ensemble Shimmer","eshim",CC_ENS_SHIMMER,G_ENS,  4, 1, F_INT },
    { "VOL",  "Volume",       "vol",    CC_MASTER_VOL,  G_OUT,  4, 2, F_INT },
};

// ---- value mappings (shared by the engine and the display) -------------------
static inline float attackSeconds(uint8_t v)  { return 0.004f * powf(600.0f, v / 127.0f); }   // 4 ms .. 2.4 s
static inline float releaseSeconds(uint8_t v) { return 0.02f  * powf(300.0f, v / 127.0f); }   // 20 ms .. 6 s
static inline float pizzTau(uint8_t v)        { return 0.05f  * powf(20.0f,  v / 127.0f); }   // 50 ms .. 1 s
static inline float vibHz(uint8_t v)          { return 0.5f   * powf(16.0f,  v / 127.0f); }   // 0.5 .. 8 Hz
static inline float ensHz(uint8_t v)          { return 0.15f  * powf(8.0f,   v / 127.0f); }   // 0.15 .. 1.2 Hz
static inline float detuneCents(uint8_t v)    { return v * (18.0f / 127.0f); }                // 0 .. 18 cents
static inline float toneHz(uint8_t v)         { return 600.0f * powf(2.0f, v * (5.0f / 127.0f)); }   // 600 Hz .. 19 kHz

// ---- Patches: complete parameter sets ----------------------------------------
#define N_PATCHES 4
static const char* const PATCH_NAMES[N_PATCHES] = { "Ensemble", "Cello", "Violins", "Pizz" };
static const uint8_t PATCHES[N_PATCHES][N_PARAMS] = {
    //  BASS CELL VLA VLN PIZZ | ATK REL DET | VIB VRT PDEC | TONE DEPT RATE MIX SHIM | VOL
    {     40,  90,  70, 90,   0,   70, 80, 50,   0,  60,  60,   100, 100,  40, 110,  70,    90 },  // Ensemble: the classic string-machine sound
    {    110, 120,  40,  0,   0,   60, 70, 30,   0,  60,  60,    80,  80,  30,  90,  50,    85 },  // Cello: bass + cello, dark
    {      0,   0,  70, 120,  0,   90, 95, 60,  25,  70,  60,   110, 110,  50, 110,  80,    72 },  // Violins: slow, bright, light vibrato
    {      0,   0,  30,  20, 120,   0, 40, 20,   0,  60,  55,   105,  70,  40,  85,  50,    112 },  // Pizz: plucked
};

// ---- CC -> parameter ----------------------------------------------------------
static inline uint8_t paramForCC(uint8_t cc) {
    if (cc == CC_EXPRESSION) return P_VOL;
#if AKAI_REMAP
    switch (cc) {
        case AKAI_K1: return P_TONE;
        case AKAI_K2: return P_ATTACK;
        case AKAI_K3: return P_RELEASE;
        case AKAI_K4: return P_ENS_DEPTH;
        case AKAI_K5: return P_ENS_RATE;
        case AKAI_K6: return P_ENS_MIX;
        case AKAI_K7: return P_VOL;
        case AKAI_K8: return P_VIB_DEPTH;
        default: break;
    }
#endif
    for (uint8_t i = 0; i < N_PARAMS; i++)
        if (PARAM_INFO[i].cc == cc) return i;
    return 0xFF;
}
