#pragma once
// ============================================================
//  dx7_presets.h  —  twelve factory voices, written for this firmware from the usual FM recipes (nothing is copied from any
//  Yamaha cartridge). They are what the synthesizer plays after power-up; a 32-voice SysEx bank replaces them (see midi_handler.h).
//  They have been tuned by arithmetic and measurement in the host tests, not by ear on the tower: adjust them to taste.
// ============================================================
#include "dx7_patch.h"

#define DX_N_PRESETS 12

static inline Dx7Op dxOp(uint8_t coarse, uint8_t fine, uint8_t ol, uint8_t r1, uint8_t r2, uint8_t r3, uint8_t r4,
                         uint8_t l1, uint8_t l2, uint8_t l3, uint8_t l4, uint8_t kvs = 0, uint8_t det = 7) {
    Dx7Op o; memset(&o, 0, sizeof o);
    o.r[0] = r1; o.r[1] = r2; o.r[2] = r3; o.r[3] = r4; o.l[0] = l1; o.l[1] = l2; o.l[2] = l3; o.l[3] = l4;
    o.bp = 39; o.coarse = coarse; o.fine = fine; o.ol = ol; o.kvs = kvs; o.det = det;
    return o;
}
static inline Dx7Op dxOff() { return dxOp(1, 0, 0, 99, 99, 99, 99, 99, 99, 99, 0); }
static inline void dxSet(Dx7Patch& p, int opNo, const Dx7Op& o) { p.op[6 - opNo] = o; }       // opNo 1..6 as on the DX7's panel

static inline void dx7Preset(int n, Dx7Patch& p) {
    memset(&p, 0, sizeof p);
    for (int i = 0; i < 6; i++) p.op[i] = dxOff();
    for (int i = 0; i < 4; i++) { p.pr[i] = 99; p.pl[i] = 50; }
    p.transpose = 24; p.lfs = 35; p.lfw = 4; p.lfoSync = 1;
    const char* nm = "INIT";
    switch (((n % DX_N_PRESETS) + DX_N_PRESETS) % DX_N_PRESETS) {
    case 0:  nm = "E.PIANO 1";      // algorithm 5: three carrier / modulator pairs; the tine is the 14:1 modulator
        p.alg = 4; p.fb = 6; p.lpms = 3;
        dxSet(p, 1, dxOp(1, 0, 99, 96, 25, 25, 67, 99, 75, 0, 0, 2));
        dxSet(p, 2, dxOp(14, 0, 58, 95, 50, 35, 78, 99, 75, 0, 0, 5));
        dxSet(p, 3, dxOp(1, 0, 92, 96, 25, 25, 67, 99, 75, 0, 0, 2, 8));
        dxSet(p, 4, dxOp(1, 0, 89, 95, 29, 20, 50, 99, 95, 0, 0, 3));
        dxSet(p, 5, dxOp(1, 0, 88, 96, 25, 25, 67, 99, 75, 0, 0, 2, 6));
        dxSet(p, 6, dxOp(1, 0, 79, 95, 50, 35, 78, 99, 75, 0, 0, 3));
        break;
    case 1:  nm = "FM BASS";        // transposed an octave down; a short, hard modulator gives the pluck
        p.alg = 4; p.fb = 5; p.transpose = 12;
        dxSet(p, 1, dxOp(1, 0, 99, 99, 60, 50, 60, 99, 90, 80, 0, 2));
        dxSet(p, 2, dxOp(1, 0, 80, 99, 70, 30, 70, 99, 55, 20, 0, 4));
        dxSet(p, 3, dxOp(1, 0, 90, 99, 50, 50, 60, 99, 85, 75, 0, 2, 8));
        dxSet(p, 4, dxOp(2, 0, 62, 99, 65, 30, 65, 99, 40, 10, 0, 3));
        break;
    case 2:  nm = "BRASS 1";        // the modulators swell after the carrier: the "blat" of brass
        p.alg = 4; p.fb = 6; p.lpms = 3; p.lfd = 30; p.lpmd = 8;
        dxSet(p, 1, dxOp(1, 0, 99, 75, 40, 40, 50, 99, 95, 90, 0, 3));
        dxSet(p, 2, dxOp(1, 0, 80, 62, 45, 40, 50, 90, 99, 80, 0, 5));
        dxSet(p, 3, dxOp(1, 0, 92, 75, 40, 40, 50, 99, 95, 90, 0, 3, 9));
        dxSet(p, 4, dxOp(1, 0, 82, 62, 45, 40, 50, 90, 99, 80, 0, 5));
        dxSet(p, 5, dxOp(1, 0, 88, 75, 40, 40, 50, 99, 95, 90, 0, 3, 5));
        dxSet(p, 6, dxOp(1, 0, 78, 62, 45, 40, 50, 90, 99, 80, 0, 5));
        break;
    case 3:  nm = "MARIMBA";        // the 4:1 modulator dies in a few milliseconds: the stick; the carrier rings on
        p.alg = 4; p.fb = 0;
        dxSet(p, 1, dxOp(1, 0, 99, 99, 62, 40, 55, 99, 40, 0, 0, 3));
        dxSet(p, 2, dxOp(4, 0, 72, 99, 80, 40, 70, 99, 0, 0, 0, 4));
        dxSet(p, 3, dxOp(1, 0, 70, 99, 62, 40, 55, 99, 35, 0, 0, 3));
        dxSet(p, 4, dxOp(10, 0, 55, 99, 85, 40, 70, 99, 0, 0, 0, 4));
        break;
    case 4:  nm = "TUB.BELLS";      // inharmonic modulator ratios 3.5 (3 x 1.17) and 7.2 (7 x 1.03)
        p.alg = 4; p.fb = 3;
        dxSet(p, 1, dxOp(1, 0, 99, 99, 25, 20, 40, 99, 85, 0, 0, 2));
        dxSet(p, 2, dxOp(3, 17, 75, 99, 30, 20, 45, 99, 60, 0, 0, 3));
        dxSet(p, 3, dxOp(2, 0, 80, 99, 25, 20, 40, 99, 80, 0, 0, 2));
        dxSet(p, 4, dxOp(7, 3, 62, 99, 35, 20, 45, 99, 55, 0, 0, 3));
        break;
    case 5:  nm = "CLAV";
        p.alg = 4; p.fb = 6;
        dxSet(p, 1, dxOp(1, 0, 99, 99, 70, 40, 85, 99, 70, 0, 0, 4));
        dxSet(p, 2, dxOp(3, 0, 70, 99, 80, 40, 85, 99, 0, 0, 0, 5));
        dxSet(p, 3, dxOp(1, 0, 80, 99, 70, 40, 85, 99, 65, 0, 0, 4, 9));
        dxSet(p, 4, dxOp(5, 0, 55, 99, 85, 40, 85, 99, 0, 0, 0, 5));
        break;
    case 6:  nm = "ORGAN";          // algorithm 32: six carriers = six drawbars (16', 8', 5 1/3', 4', 2', 1')
        p.alg = 31; p.fb = 3;
        { const uint8_t ratio[6] = { 0, 1, 3, 2, 4, 8 }, vol[6] = { 80, 95, 70, 90, 65, 55 };
          for (int i = 0; i < 6; i++) dxSet(p, i + 1, dxOp(ratio[i], 0, vol[i], 99, 99, 99, 85, 99, 99, 99, 0)); }
        break;
    case 7:  nm = "FLUTE";
        p.alg = 4; p.fb = 5; p.lpms = 3; p.lfd = 40; p.lpmd = 10;
        dxSet(p, 1, dxOp(1, 0, 99, 70, 60, 60, 60, 99, 97, 90, 0, 2));
        dxSet(p, 2, dxOp(1, 0, 48, 70, 60, 60, 60, 80, 70, 60, 0, 4));
        dxSet(p, 3, dxOp(1, 0, 30, 99, 99, 99, 99, 0, 0, 0, 0));
        dxSet(p, 4, dxOp(1, 0, 0, 99, 99, 99, 99, 0, 0, 0, 0));
        break;
    case 8:  nm = "SAW LEAD";       // algorithm 32: operator 6 modulates itself (feedback 7 = a rich saw), operator 5 doubles it a little off
        p.alg = 31; p.fb = 7; p.lpms = 3; p.lfd = 50; p.lpmd = 12; p.lfs = 40;
        dxSet(p, 6, dxOp(1, 0, 97, 90, 40, 50, 60, 99, 95, 90, 0, 2));
        dxSet(p, 5, dxOp(1, 0, 92, 90, 40, 50, 60, 99, 95, 90, 0, 2, 10));
        break;
    case 9:  nm = "STRINGS";
        p.alg = 31; p.fb = 5; p.lpms = 2; p.lfd = 10; p.lpmd = 8; p.lfs = 28;
        dxSet(p, 6, dxOp(1, 0, 94, 55, 35, 99, 45, 99, 95, 90, 0, 1, 6));
        dxSet(p, 5, dxOp(1, 0, 96, 55, 35, 99, 45, 99, 95, 90, 0, 1, 9));
        dxSet(p, 4, dxOp(1, 0, 92, 52, 35, 99, 45, 99, 95, 90, 0, 1, 7));
        dxSet(p, 3, dxOp(2, 0, 80, 50, 35, 99, 45, 99, 95, 90, 0, 1, 8));
        break;
    case 10: nm = "HARPSICH";
        p.alg = 4; p.fb = 6;
        dxSet(p, 1, dxOp(1, 0, 99, 99, 55, 40, 80, 99, 20, 0, 0, 3));
        dxSet(p, 2, dxOp(5, 0, 75, 99, 75, 40, 85, 99, 0, 0, 0, 5));
        dxSet(p, 3, dxOp(2, 0, 80, 99, 55, 40, 80, 99, 18, 0, 0, 3, 8));
        dxSet(p, 4, dxOp(7, 0, 62, 99, 78, 40, 85, 99, 0, 0, 0, 5));
        break;
    case 11: nm = "VIBES";          // amplitude modulation (the motor tremolo): AMS on the carriers, depth 40
        p.alg = 4; p.fb = 0; p.lamd = 40; p.lfs = 32; p.lfw = 4;
        { Dx7Op c = dxOp(1, 0, 99, 99, 33, 28, 50, 99, 85, 0, 0, 2); c.ams = 2; dxSet(p, 1, c);
          Dx7Op m = dxOp(4, 0, 60, 99, 60, 30, 55, 99, 0, 0, 0, 4); dxSet(p, 2, m);
          Dx7Op c2 = dxOp(1, 0, 80, 99, 33, 28, 50, 99, 80, 0, 0, 2, 8); c2.ams = 2; dxSet(p, 3, c2);
          Dx7Op m2 = dxOp(10, 0, 50, 99, 70, 30, 55, 99, 0, 0, 0, 4); dxSet(p, 4, m2); }
        break;
    }
    for (int i = 0; i < 10; i++) p.name[i] = ' ';
    for (int i = 0; i < 10 && nm[i]; i++) p.name[i] = nm[i];
    p.name[10] = 0;
}
