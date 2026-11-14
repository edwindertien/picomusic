#pragma once
// ============================================================
//  stereo_fx.h  —  the effects pipeline (shared by the piano and the DX7) on the finished stereo mix (core 1; core 0 only sets the coefficients)
//
//      voices -> volume -> WAH -> TREMOLO / AUTO-PAN -> CHORUS -> REVERB -> soft limiter
//
//  * Every stage is skipped completely when its knob is at its "off" position, and with all of them off process() does not
//    touch the signal: the dry piano is bit-identical to a build without effects (a regression test checks this).
//  * Cost does not depend on the number of notes. RAM: 60,248 bytes (reverb 52 KB, chorus 8 KB, the rest is filter state). No libm on the audio core:
//    coefficients come from core 0 as single words; the one exp2 the wah needs is a 5th-order polynomial.
//  * WAH     Chamberlin state-variable band-pass, one per channel, fc 350 Hz .. 2.8 kHz (3 octaves), Q ~ 5.5. Manual position
//            (WPOS, also MIDI CC11 "expression": use a pedal) or an automatic LFO sweep (WLFO > 0).
//  * TREM    one centre-off knob: left of centre = tremolo depth, right = auto-pan depth (the left and right gains in antiphase:
//            the Rhodes suitcase sound). Rate 0.5 .. 10 Hz.
//  * CHORUS  two modulated delays (11 ms +- 3.5 ms at 0.6 Hz), left and right in antiphase.
//  * REVERB  four damped feedback combs and two all-passes per channel (Freeverb-style, different lengths left and right).
//            REV = wet level on a SQUARED taper (50 % = -12 dB, 25 % = -24 dB re 100 %: the first percent are very quiet), RSIZE = feedback (decay time ~0.5 s .. 8 s) and does
//            not change the wet level. 100 % is about -3 dB (energy) re the dry note.
// ============================================================
#include <stdint.h>
#include <math.h>
#include "config.h"

#define FX_REV_COMBS 4
#define FX_CHORUS_LEN 1024

struct StereoFx {
    // ---- written by core 0 (one aligned word each), read by core 1 ----
    volatile float    wahMix = 0.0f, wahPos = 0.5f, tremDepth = 0.0f, panDepth = 0.0f, chorMix = 0.0f, revWet = 0.0f, revFb = 0.85f;
    volatile uint32_t wahLfoInc = 0, tremInc = 0;                 // LFO phase increments (0 = off / manual)
    volatile uint8_t  on = 0;                                     // bit0 wah, bit1 tremolo, bit2 auto-pan, bit3 chorus, bit4 reverb

    // ---- core-0 side: knob values (0..127) -> coefficients ----
    void setKnobs(uint8_t wah, uint8_t wpos, uint8_t wlfo, uint8_t trem, uint8_t trate, uint8_t chor, uint8_t rev, uint8_t rsize) {
        const float f32 = 4294967296.0f / (float)SAMPLE_RATE;
        wahMix = wah / 127.0f; wahPos = wpos / 127.0f;
        wahLfoInc = wlfo ? (uint32_t)(wahRateHz(wlfo) * f32) : 0;
        tremDepth = trem < 64 ? (64 - trem) / 64.0f : 0.0f; panDepth = trem > 64 ? (trem - 64) / 63.0f : 0.0f;
        tremInc = (uint32_t)(tremRateHz(trate) * f32);
        chorMix = chor / 127.0f; revFb = revFeedbackOf(rsize);
        const float t = rev / 127.0f;
        revWet = t * t * powf(10.0f, revCompDb(rsize) / 20.0f);      // the knob is a squared (audio) taper; the table makes the wet level independent of the reverb time
        on = (uint8_t)((wah ? 1 : 0) | (trem < 64 ? 2 : 0) | (trem > 64 ? 4 : 0) | (chor ? 8 : 0) | (rev ? 16 : 0));
    }
    static float wahRateHz(uint8_t v)  { return 0.2f * powf(2.0f, v * (5.0f / 127.0f)); }          // 0.2 .. 6.4 Hz
    static float tremRateHz(uint8_t v) { return 0.5f * powf(2.0f, v * (4.3f / 127.0f)); }          // 0.5 .. 10 Hz
    static float revFeedbackOf(uint8_t v) { return 0.70f + 0.27f * v / 127.0f; }                   // 0.70 .. 0.97
    // Calibrated on PLAYED NOTES and chords (a bass note, a mid note, a treble note, a chord), NOT on a sine: a comb reverb is not level-flat, one sine can sit in a trough and understate
    // the wet level by many dB. REV_TIME_* remove the rise of the wet energy with the reverb time (more feedback integrates more energy); the input high-pass keeps bass notes from
    // exciting the comb resonances (a C2 gave +16 dB more wet energy than a C4 at the longest time).
    static constexpr float REV_HP_K = 0.021f;                                                    // the input high-pass: one pole at about 150 Hz
    // gain (dB) that brings the average wet energy of the five test signals to -3 dB re the dry energy at REV 127, measured at RSIZE 0, 32, 64, 96, 127 (interpolated between)
    static constexpr float REV_COMP_DB[5] = { 11.8f, 10.6f, 9.1f, 7.0f, 3.1f };
    static float revCompDb(uint8_t v) { const float x = v * (4.0f / 127.0f); int i = (int)x; if (i > 3) i = 3; const float fr = x - (float)i; return REV_COMP_DB[i] + (REV_COMP_DB[i + 1] - REV_COMP_DB[i]) * fr; }

    // ---- core 1 ----
    // parabolic sine approximation of a 32-bit phase, -1..1
    static HOT float lfo(uint32_t ph) { const float x = (float)(int32_t)ph * (1.0f / 2147483648.0f); return 4.0f * x * (1.0f - (x < 0 ? -x : x)); }
    static HOT float exp2x(float x) {                             // 2^x for 0 <= x < 4, error ~1.5e-4 relative
        const int n = (int)x; const float f = x - (float)n;
        const float p = 1.0f + f * (0.69314718f + f * (0.24022651f + f * (0.05550411f + f * (0.00961813f + f * 0.00133336f))));
        return p * (float)(1 << n);
    }

    HOT void process(float& l, float& r) {
        const uint8_t m = on;
        if (!m) return;                                           // everything off: the signal is not touched at all
        if (m & 1) wah(l, r);
        if (m & 6) trem(l, r, m);
        if (m & 8) chorus(l, r);
        if (m & 16) reverb(l, r);
    }

    // ---- state ----
private:
    uint32_t phWah = 0, phTrem = 0, phChor = 0; uint8_t wcnt = 0; float wf1 = 0.1f;
    float lowL = 0, bandL = 0, lowR = 0, bandR = 0;
    float chL[FX_CHORUS_LEN] = {0}, chR[FX_CHORUS_LEN] = {0}; uint16_t chW = 0;
    static constexpr int CL[FX_REV_COMBS] = { 1116, 1188, 1277, 1356 }, CR[FX_REV_COMBS] = { 1139, 1211, 1300, 1379 }, AL[2] = { 556, 441 }, AR[2] = { 579, 464 };
    float cbL[FX_REV_COMBS][1356] = {{0}}, cbR[FX_REV_COMBS][1379] = {{0}}, apL0[556] = {0}, apL1[441] = {0}, apR0[579] = {0}, apR1[464] = {0};
    float revLp = 0.0f; float cfL[FX_REV_COMBS] = {0}, cfR[FX_REV_COMBS] = {0}; uint16_t cpL[FX_REV_COMBS] = {0}, cpR[FX_REV_COMBS] = {0}, aq[4] = {0};

    HOT void wah(float& l, float& r) {
        if (wcnt == 0) {                                          // the coefficient is updated every 16 samples
            float pos = wahPos; if (wahLfoInc) pos = 0.5f + 0.5f * lfo(phWah);
            wf1 = 350.0f * exp2x(pos * 3.0f) * (6.2831853f / (float)SAMPLE_RATE);
        }
        wcnt = (wcnt + 1) & 15; phWah += wahLfoInc;
        const float q = 0.18f, mix = wahMix;
        lowL += wf1 * bandL; const float hL = l - lowL - q * bandL; bandL += wf1 * hL;
        lowR += wf1 * bandR; const float hR = r - lowR - q * bandR; bandR += wf1 * hR;
        l = l * (1.0f - mix) + bandL * q * mix; r = r * (1.0f - mix) + bandR * q * mix;        // band * q peaks at unity gain
    }
    HOT void trem(float& l, float& r, uint8_t m) {
        const float t = 0.5f + 0.5f * lfo(phTrem); phTrem += tremInc;
        if (m & 2) { const float g = 1.0f - tremDepth * t; l *= g; r *= g; }
        if (m & 4) { l *= 1.0f - panDepth * t; r *= 1.0f - panDepth * (1.0f - t); }
    }
    HOT void chorus(float& l, float& r) {
        const float s = lfo(phChor); phChor += (uint32_t)(0.6f * (4294967296.0f / (float)SAMPLE_RATE));
        chL[chW] = l; chR[chW] = r;
        const float dl = 484.0f + 154.0f * s, dr = 484.0f - 154.0f * s;
        const float tl = tap(chL, dl), tr = tap(chR, dr);
        chW = (chW + 1) & (FX_CHORUS_LEN - 1);
        const float mix = chorMix; l = l * (1.0f - 0.4f * mix) + tl * 0.6f * mix; r = r * (1.0f - 0.4f * mix) + tr * 0.6f * mix;
    }
    HOT float tap(const float* b, float d) const {
        const int di = (int)d; const float fr = d - (float)di;
        const float a = b[(chW - di) & (FX_CHORUS_LEN - 1)], c = b[(chW - di - 1) & (FX_CHORUS_LEN - 1)];
        return a + (c - a) * fr;
    }
    HOT void reverb(float& l, float& r) {
        const float mono = (l + r) * 0.5f; revLp += REV_HP_K * (mono - revLp);
        const float fb = revFb, d = 0.35f, in = (mono - revLp) * 0.03f; float oL = 0.0f, oR = 0.0f;
        for (int i = 0; i < FX_REV_COMBS; i++) {
            float y = cbL[i][cpL[i]]; cfL[i] = y * (1.0f - d) + cfL[i] * d; cbL[i][cpL[i]] = in + cfL[i] * fb; if (++cpL[i] >= CL[i]) cpL[i] = 0; oL += y;
            y = cbR[i][cpR[i]]; cfR[i] = y * (1.0f - d) + cfR[i] * d; cbR[i][cpR[i]] = in + cfR[i] * fb; if (++cpR[i] >= CR[i]) cpR[i] = 0; oR += y;
        }
        oL = allpass(apL0, AL[0], aq[0], oL); oL = allpass(apL1, AL[1], aq[1], oL); oR = allpass(apR0, AR[0], aq[2], oR); oR = allpass(apR1, AR[1], aq[3], oR);
        l += revWet * oL; r += revWet * oR;
    }
    static HOT float allpass(float* buf, int len, uint16_t& p, float x) {
        const float bo = buf[p]; const float y = bo - x; buf[p] = x + bo * 0.5f; if (++p >= len) p = 0; return y;
    }
};
