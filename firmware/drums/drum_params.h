#pragma once
// ============================================================
//  drum_params.h  —  instruments, the knob table, the default kit, pad maps, demo patterns
//
//  16 instruments = 16 tracks = 16 pad triggers (the MPK mini's 8 pads x 2 banks).
//  Five parameters are PER INSTRUMENT (LEVEL TUNE DECAY TONE PAN) and edit the SELECTED instrument:
//  the knob panel always shows the selected one; a pad hit selects it.
// ============================================================
#include <stdint.h>
#include <math.h>
#include "config.h"

enum InstId : uint8_t { I_BD, I_SD, I_LT, I_MT, I_HT, I_RS, I_CP, I_CB, I_CH, I_OH, I_CY, I_CL, I_MA, I_LC, I_MC, I_HC };
static const char* const INST_LABEL[N_INST] = { "BD","SD","LT","MT","HT","RS","CP","CB","CH","OH","CY","CL","MA","LC","MC","HC" };
static const char* const INST_NAME[N_INST]  = { "Bass Drum","Snare","Low Tom","Mid Tom","High Tom","Rim Shot","Hand Clap","Cowbell",
                                                "Closed Hat","Open Hat","Cymbal","Claves","Maracas","Low Conga","Mid Conga","High Conga" };

enum ParamId : uint8_t {
    P_INST, P_LEVEL, P_TUNE, P_DECAY, P_TONE, P_PAN,     // the selected instrument
    P_TEMPO, P_SWING, P_VOL, P_DRIVE,                    // machine
    N_PARAMS
};
#define N_KNOBS        N_PARAMS
#define N_INST_PARAMS  5                                  // P_LEVEL..P_PAN are stored per instrument
enum ParamGroup : uint8_t { G_INST, G_VOICE, G_TIME, G_OUT };          // order = GROUP_COL[] in lcd.cpp
enum ParamFmt   : uint8_t { F_INT, F_INST, F_BPM, F_PCT, F_PAN };

struct ParamInfo {
    const char* label;    // knob label (<= 4 chars)
    const char* name;     // name shown in the top bar
    const char* cmd;      // serial name:  set <cmd> <0-127>
    uint8_t     cc;
    uint8_t     group;
    uint8_t     col, row; // knob grid cell
    uint8_t     fmt;
};
static const ParamInfo PARAM_INFO[N_PARAMS] = {
    //  label   name          cmd       cc            group    col row fmt
    { "INST", "Instrument", "inst",   CC_INST,      G_INST,  0, 0, F_INST },
    { "LVL",  "Level",      "level",  CC_LEVEL,     G_VOICE, 1, 0, F_INT  },
    { "TUNE", "Tune",       "tune",   CC_TUNE,      G_VOICE, 2, 0, F_INT  },
    { "DEC",  "Decay",      "decay",  CC_DECAY,     G_VOICE, 3, 0, F_INT  },
    { "TONE", "Tone",       "tone",   CC_TONE,      G_VOICE, 0, 1, F_INT  },
    { "PAN",  "Pan",        "pan",    CC_PAN,       G_VOICE, 1, 1, F_PAN  },
    { "BPM",  "Tempo",      "tempo",  CC_TEMPO,     G_TIME,  4, 0, F_BPM  },
    { "SWNG", "Swing",      "swing",  CC_SWING,     G_TIME,  5, 0, F_PCT  },
    { "VOL",  "Volume",     "vol",    CC_MASTER_VOL,G_OUT,   6, 0, F_INT  },
    { "DRV",  "Drive",      "drive",  CC_DRIVE,     G_OUT,   7, 0, F_INT  },
};

// What the three musical knobs mean for each instrument (shown in the README).
//   BD  TUNE end pitch 40..80 Hz        DECAY 60..720 ms        TONE click + sweep depth
//   SD  TUNE tone pitch                 DECAY both decays       TONE noise ("snappy") vs tone
//   TOM TUNE pitch +-6 st               DECAY 80..480 ms        TONE pitch-sweep depth
//   RS  TUNE pitch                      DECAY                   TONE high vs low partial
//   CP  TUNE filter centre              DECAY tail              TONE tail vs bursts
//   CB  TUNE pitch                      DECAY tail              TONE brightness
//   CH/OH/CY  TUNE metal pitch          DECAY                   TONE high-pass corner
//   CL  TUNE pitch                      DECAY 10..40 ms         TONE pitch-sweep depth
//   MA  -                               DECAY                   TONE high-pass corner
//   CONGA TUNE pitch                    DECAY 50..250 ms        TONE pitch-sweep depth

// Default kit: LEVEL TUNE DECAY TONE PAN  (0..127; 64 = centre pan / nominal tune)
static const uint8_t KIT[N_INST][N_INST_PARAMS] = {
    { 123, 64, 45, 40,  64 },   // BD
    { 107, 64, 55, 75,  64 },   // SD
    { 104, 64, 60, 35,  36 },   // LT
    { 104, 64, 55, 35,  52 },   // MT
    { 103, 64, 50, 35,  72 },   // HT
    {   94, 64, 50, 64,  58 },   // RS
    { 102, 64, 55, 70,  70 },   // CP
    {   92, 64, 60, 64,  84 },   // CB
    {  105, 64, 30, 64,  78 },   // CH
    {   94, 64, 55, 64,  84 },   // OH
    {    77, 64, 65, 64,  92 },   // CY
    {   89, 64, 50, 64,  46 },   // CL
    {   79, 64, 40, 64,  98 },   // MA
    { 100, 64, 60, 35,  26 },   // LC
    { 100, 64, 55, 35,  40 },   // MC
    { 100, 64, 50, 35,  56 },   // HC
};
#define DEFAULT_TEMPO_KNOB   60     // 120 BPM
#define DEFAULT_SWING_KNOB    0
#define DEFAULT_VOL_KNOB    100
#define DEFAULT_DRIVE_KNOB    0

// ---- value mappings --------------------------------------------------------------------
static inline float bpmFromKnob(uint8_t v)   { return (float)(TEMPO_KNOB_BASE + v); }
static inline float swingFromKnob(uint8_t v) { return 0.5f + SWING_MAX * (float)v / 127.0f; }     // fraction of a beat pair: 0.5 = straight
static inline float driveGain(uint8_t v)     { const float f = v / 127.0f; return 1.0f + 4.0f * f * f; }

// ---- step levels (pattern memory) ------------------------------------------------------
static inline uint8_t levelToVel(uint8_t lv) { return lv == 1 ? VEL_GHOST : lv == 2 ? VEL_NORMAL : VEL_ACCENT; }
static inline uint8_t velToLevel(uint8_t v)  { return v >= 110 ? 3 : v >= 75 ? 2 : 1; }

// ---- pad maps ---------------------------------------------------------------------------
// linear: note padBase..padBase+15 -> instrument 0..15.   gm: the General MIDI drum map (for DAW tracks).
static inline int instForNote(uint8_t note, bool gm, uint8_t padBase) {
    if (!gm) return (note >= padBase && note < padBase + N_INST) ? (int)(note - padBase) : -1;
    switch (note) {
        case 35: case 36:                         return I_BD;
        case 37:                                  return I_RS;
        case 38: case 40:                         return I_SD;
        case 39:                                  return I_CP;
        case 41: case 43:                         return I_LT;
        case 45: case 47:                         return I_MT;
        case 48: case 50:                         return I_HT;
        case 42: case 44:                         return I_CH;
        case 46:                                  return I_OH;
        case 49: case 51: case 52: case 55: case 57: return I_CY;
        case 56:                                  return I_CB;
        case 75:                                  return I_CL;
        case 69: case 70:                         return I_MA;
        case 64:                                  return I_LC;
        case 63:                                  return I_MC;
        case 62:                                  return I_HC;
        default:                                  return -1;
    }
}

// ---- demo patterns: one string per track, '.' = off, 1 = ghost, 2 = normal, 3 = accent ----------
struct DemoTrack { uint8_t inst; const char* steps; };
static const DemoTrack DEMO_HOUSE[] = {
    { I_BD, "3...2...2...2..." }, { I_CP, "....2.......2..." }, { I_CH, "1.2.1.2.1.2.1..." }, { I_OH, "..............2." }, { 0xFF, 0 } };
static const DemoTrack DEMO_BOOMBAP[] = {
    { I_BD, "3.....2.2.2....." }, { I_SD, "....3.......3..1" }, { I_CH, "2.1.2.1.2.1.2.1." }, { I_RS, "...........1...." }, { 0xFF, 0 } };
static const DemoTrack DEMO_ELECTRO[] = {
    { I_BD, "3..2..2...2..1.." }, { I_SD, "....3.......3..." }, { I_CH, "1111111111111111" }, { I_CB, ".......2.......2" }, { I_OH, "..2.....2......." }, { 0xFF, 0 } };
static const DemoTrack DEMO_LATIN[] = {
    { I_BD, "2.......2......." }, { I_CL, "3..2..2...2.2..." }, { I_MA, "2.1.2.1.2.1.2.1." }, { I_LC, "....2.......2..." },
    { I_MC, ".......2.......2" }, { I_HC, "..2.......2....." }, { I_RS, "....1.......1..." }, { 0xFF, 0 } };
static const DemoTrack* const DEMO_PATTERNS[4] = { DEMO_HOUSE, DEMO_BOOMBAP, DEMO_ELECTRO, DEMO_LATIN };
static const uint8_t DEMO_SONG[] = { 0, 0, 1, 1, 2, 2, 3, 3 };

#define PAGE_COUNT 4     // GRID, INST, PATTERN, SONG (lcd.cpp)

// ---- CC -> parameter (the AKAI knobs first, so CC1..CC8 always mean the eight knobs) -------------
static inline uint8_t paramForCC(uint8_t cc) {
#if AKAI_REMAP
    switch (cc) {
        case AKAI_K1: return P_TUNE;
        case AKAI_K2: return P_DECAY;
        case AKAI_K3: return P_TONE;
        case AKAI_K4: return P_LEVEL;
        case AKAI_K5: return P_TEMPO;
        case AKAI_K6: return P_SWING;
        case AKAI_K7: return P_VOL;
        case AKAI_K8: return P_DRIVE;
        default: break;
    }
#endif
    for (uint8_t i = 0; i < N_PARAMS; i++) if (PARAM_INFO[i].cc == cc) return i;
    return 0xFF;
}
