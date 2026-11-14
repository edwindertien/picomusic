#pragma once
// ============================================================
//  drum_voices.h  —  the 16 synthesised drum voices (TR-808-style, digital approximations)
//
//  Seven synthesis types cover the 16 instruments:
//    T_SINE   BD LT MT HT CL LC MC HC : sine + pitch envelope + decay (+ a short noise click)
//    T_SNARE  SD                      : two detuned sines (tone) + high-passed noise (snappy)
//    T_RIM    RS                      : two short high partials + click
//    T_CLAP   CP                      : band-passed noise, 4 quick bursts then a tail
//    T_BELL   CB                      : two square waves (540 / 800 Hz) -> band-pass, strike + tail
//    T_METAL  CH OH CY                : six square oscillators at the 808's metallic ratios -> high-pass
//    T_NOISE  MA                      : high-passed noise with a short attack
//
//  THREADING: each voice has a trigger mailbox (core 0: write trigVel, barrier, ++trigSeq; core 1
//  compares with trigSeen). Everything else is written by core 1 only; the DParams words are written
//  by core 0 when a knob changes, one aligned word each. All filters are ONE-POLE with a single
//  coefficient, so a knob turn can never tear a filter state.
//
//  Envelopes are exponential: advanced once per control tick (16 samples) and linearly interpolated.
// ============================================================
#include <stdint.h>
#include <math.h>
#include "config.h"
#include "drum_dsp.h"
#include "drum_params.h"

enum DrumType : uint8_t { T_SINE, T_SNARE, T_RIM, T_CLAP, T_BELL, T_METAL, T_NOISE };
static const uint8_t INST_TYPE[N_INST] = { T_SINE, T_SNARE, T_SINE, T_SINE, T_SINE, T_RIM, T_CLAP, T_BELL,
                                           T_METAL, T_METAL, T_METAL, T_SINE, T_NOISE, T_SINE, T_SINE, T_SINE };
static const float INST_BASE_HZ[N_INST] = { 56.0f, 0, 88.0f, 130.0f, 190.0f, 0, 0, 0, 0, 0, 0, 2500.0f, 0, 205.0f, 273.0f, 365.0f };
#define METAL_GAIN 10.0f           // makeup gain: six high-passed squares leave little energy
static const float METAL_HZ[6] = { 205.3f, 304.4f, 369.6f, 522.7f, 540.0f, 800.0f };   // the 808's hat oscillators

// Derived, per-instrument sound parameters (core 0 writes; core 1 reads)
struct DParams {
    float gL = 0.5f, gR = 0.5f;                 // level x pan
    float f1 = 100.0f, f2 = 100.0f, sweep = 1.0f;
    float c1 = 0.9f, c2 = 0.9f, c3 = 0.9f, cp = 0.9f, chk = 0.5f;   // decay coefficients per control tick (amp1, amp2, amp3, pitch, choke)
    float g1 = 1.0f, g2 = 1.0f;                 // component gains
    float k1 = 0.1f, k2 = 0.1f;                 // one-pole coefficients
};

struct DrumVoice {
    // ---- trigger mailbox ----
    volatile uint8_t trigSeq = 0, trigVel = 0;
    // ---- sound parameters ----
    DParams p;
    // ---- state (core 1) ----
    bool     live = false, choked = false;
    uint8_t  trigSeen = 0, type = T_SINE;
    uint32_t ph1 = 0, ph2 = 0, inc1 = 0, inc2 = 0, rng = 12345u;
    uint32_t mph[6] = {0}, minc[6] = {0};
    float    pe = 1.0f, vel = 0.0f;
    float    e[3] = {0}, a[3] = {0}, t[3] = {0}, d[3] = {0};
    float    z[3] = {0};
    int      ct = 0;

    // =====================================================================
    //  core 0: knob values -> sound parameters
    // =====================================================================
    void derive(int inst, const uint8_t* v) {
        const float lvl = v[0] / 127.0f;
        const float tune = ((float)v[1] - 64.0f) / 127.0f;            // -0.5 .. +0.5
        const float dec = v[2] / 127.0f, tone = v[3] / 127.0f;
        const float pan = v[4] / 127.0f * 1.57079633f;
        const float tf = powf(2.0f, tune);                            // 0.71 .. 1.41
        p.gL = lvl * lvl * VOICE_GAIN * cosf(pan);
        p.gR = lvl * lvl * VOICE_GAIN * sinf(pan);
        p.chk = expDecayCoef(CHOKE_MS * 0.001f);
        switch (inst) {
            case I_BD:
                p.f1 = INST_BASE_HZ[I_BD] * tf;  p.sweep = 2.0f + 2.5f * tone;
                p.cp = expDecayCoef(KICK_SWEEP_MS * 0.001f);
                p.c1 = expDecayCoef(0.06f * powf(12.0f, dec));        // 60 .. 720 ms
                p.g1 = 0.1f + 0.5f * tone;  p.c2 = expDecayCoef(0.003f);
                break;
            case I_LT: case I_MT: case I_HT:
                p.f1 = INST_BASE_HZ[inst] * tf;  p.sweep = 1.0f + 0.7f * tone;
                p.cp = expDecayCoef(TOM_SWEEP_MS * 0.001f);
                p.c1 = expDecayCoef(0.08f * powf(6.0f, dec));         // 80 .. 480 ms
                p.g1 = 0.12f;  p.c2 = expDecayCoef(0.002f);
                break;
            case I_LC: case I_MC: case I_HC:
                p.f1 = INST_BASE_HZ[inst] * tf;  p.sweep = 1.0f + 0.35f * tone;
                p.cp = expDecayCoef(0.012f);
                p.c1 = expDecayCoef(0.05f * powf(5.0f, dec));         // 50 .. 250 ms
                p.g1 = 0.10f;  p.c2 = expDecayCoef(0.002f);
                break;
            case I_CL:
                p.f1 = INST_BASE_HZ[I_CL] * powf(2.0f, tune * 0.6f);  p.sweep = 1.0f + 0.1f * tone;
                p.cp = expDecayCoef(0.005f);
                p.c1 = expDecayCoef(0.010f * powf(4.0f, dec));        // 10 .. 40 ms
                p.g1 = 0.15f;  p.c2 = expDecayCoef(0.001f);
                break;
            case I_SD: {
                const float ds = 0.6f + 1.6f * dec;
                p.f1 = 185.0f * powf(2.0f, tune * 0.8f);  p.f2 = p.f1 * 1.78f;  p.sweep = 1.15f;  p.cp = expDecayCoef(0.01f);
                p.c1 = expDecayCoef(0.06f * ds);  p.c2 = expDecayCoef(0.04f * ds);  p.c3 = expDecayCoef(0.11f * ds);
                p.g1 = 1.0f - 0.45f * tone;  p.g2 = 0.25f + 0.95f * tone;
                p.k1 = onePoleCoef(1800.0f);
                break; }
            case I_RS: {
                const float ds = 0.5f + 1.5f * dec, tr = powf(2.0f, tune * 0.5f);
                p.f1 = 1700.0f * tr;  p.f2 = 470.0f * tr;
                p.c1 = expDecayCoef(0.004f * ds);  p.c2 = expDecayCoef(0.007f * ds);  p.c3 = expDecayCoef(0.0015f);
                p.g1 = 0.3f + 0.7f * tone;  p.g2 = 1.0f - 0.5f * tone;
                break; }
            case I_CP: {
                const float ds = 0.5f + 1.5f * dec, tr = powf(2.0f, tune * 0.8f);
                p.c1 = expDecayCoef(0.006f);  p.c2 = expDecayCoef(0.07f * ds);
                p.g1 = 0.35f + 0.65f * tone;  p.g2 = 3.0f;
                p.k1 = onePoleCoef(750.0f * tr);  p.k2 = onePoleCoef(2400.0f * tr);
                break; }
            case I_CB: {
                const float tr = powf(2.0f, tune * 0.5f);
                p.f1 = 540.0f * tr;  p.f2 = 800.0f * tr;
                p.c1 = expDecayCoef(0.008f);  p.c2 = expDecayCoef(0.09f * (0.4f + 2.6f * dec));
                p.g1 = 0.5f;  p.g2 = 0.5f;
                p.k1 = onePoleCoef(350.0f);  p.k2 = onePoleCoef(1500.0f + 3500.0f * tone);
                break; }
            case I_CH:
                p.f1 = tf;  p.c1 = expDecayCoef(0.012f + 0.06f * dec);  p.c2 = expDecayCoef(0.01f);
                p.g1 = 0.0f;  p.g2 = METAL_GAIN;  p.k1 = onePoleCoef(5500.0f + 3500.0f * tone);  p.k2 = onePoleCoef(9000.0f);
                break;
            case I_OH:
                p.f1 = tf;  p.c1 = expDecayCoef(0.12f + 0.5f * dec);  p.c2 = expDecayCoef(0.02f);
                p.g1 = 0.4f * METAL_GAIN;  p.g2 = 0.8f * METAL_GAIN;  p.k1 = onePoleCoef(5000.0f + 3500.0f * tone);  p.k2 = onePoleCoef(9000.0f);
                break;
            case I_CY:
                p.f1 = tf;  p.c1 = expDecayCoef(0.20f + 1.0f * dec);  p.c2 = expDecayCoef(0.05f);
                p.g1 = 0.5f * METAL_GAIN;  p.g2 = 0.7f * METAL_GAIN;  p.k1 = onePoleCoef(2800.0f + 3000.0f * tone);  p.k2 = onePoleCoef(8000.0f);
                break;
            case I_MA:
                p.c1 = expDecayCoef(0.012f + 0.05f * dec);  p.g1 = 2.0f;
                p.k1 = onePoleCoef(2500.0f + 4500.0f * tone);  p.k2 = onePoleCoef(8000.0f);
                break;
            default: break;
        }
    }

    // =====================================================================
    //  core 1
    // =====================================================================
    void trigger(int inst, float v01, uint32_t seed) {
        type = INST_TYPE[inst];
        vel = v01; live = true; choked = false; ct = 0; pe = 1.0f;
        rng = rng * 1664525u + 1013904223u + seed * 2654435761u;
        z[0] = z[1] = z[2] = 0.0f; ph1 = ph2 = 0;
        e[0] = e[1] = 1.0f; e[2] = 0.0f;
        switch (type) {
            case T_SINE:  inc1 = (uint32_t)hzToInc(p.f1 * p.sweep); break;
            case T_SNARE: inc1 = (uint32_t)hzToInc(p.f1 * p.sweep); inc2 = (uint32_t)hzToInc(p.f2 * p.sweep); e[2] = 1.0f; break;
            case T_RIM:   inc1 = (uint32_t)hzToInc(p.f1); inc2 = (uint32_t)hzToInc(p.f2); e[2] = 1.0f; break;
            case T_CLAP:  e[0] = 1.0f; e[1] = 0.7f * p.g1; break;
            case T_BELL:  inc1 = (uint32_t)hzToInc(p.f1); inc2 = (uint32_t)hzToInc(p.f2); break;
            case T_METAL:
                for (int k = 0; k < 6; k++) { minc[k] = (uint32_t)hzToInc(METAL_HZ[k] * p.f1); rng = rng * 1664525u + 1013904223u; mph[k] = rng; }
                break;
            case T_NOISE: e[0] = 0.0f; break;
            default: break;
        }
        for (int k = 0; k < 3; k++) { t[k] = e[k] * vel; a[k] = t[k]; d[k] = 0.0f; }
    }

    // once per control tick
    HOT void control() {
        ct++;
        for (int k = 0; k < 3; k++) a[k] = t[k];
        const float c1 = choked ? p.chk : p.c1, c2 = choked ? p.chk : p.c2, c3 = choked ? p.chk : p.c3;
        switch (type) {
            case T_SINE:
                pe *= p.cp;  inc1 = (uint32_t)hzToInc(p.f1 * (1.0f + (p.sweep - 1.0f) * pe));
                e[0] *= c1;  e[1] *= c2;
                live = e[0] > 1e-3f;
                break;
            case T_SNARE: {
                pe *= p.cp;  const float m = 1.0f + (p.sweep - 1.0f) * pe;
                inc1 = (uint32_t)hzToInc(p.f1 * m);  inc2 = (uint32_t)hzToInc(p.f2 * m);
                e[0] *= c1;  e[1] *= c2;  e[2] *= c3;
                live = e[0] > 1e-3f || e[1] > 1e-3f || e[2] > 1e-3f;
                break; }
            case T_RIM:
                e[0] *= c1;  e[1] *= c2;  e[2] *= c3;
                live = e[0] > 1e-3f || e[1] > 1e-3f;
                break;
            case T_CLAP:
                if (ct == 30 || ct == 60 || ct == 90) e[0] = 1.0f;       // the quick re-bursts, ~11 ms apart
                e[0] *= c1;  e[1] *= c2;
                live = ct < 95 || e[0] > 3e-3f || e[1] > 3e-3f;
                break;
            case T_BELL:
                e[0] *= c1;  e[1] *= c2;
                live = e[1] > 1e-3f;
                break;
            case T_METAL:
                e[0] *= c1;  e[1] *= c2;
                live = e[0] > 2e-3f;
                break;
            case T_NOISE:
                if (ct <= 10) e[0] = (float)ct * 0.1f; else e[0] *= c1;     // short attack, then decay
                live = ct <= 10 || e[0] > 1e-3f;
                break;
            default: break;
        }
        for (int k = 0; k < 3; k++) { t[k] = e[k] * vel; d[k] = (t[k] - a[k]) * (1.0f / (float)CONTROL_DIV); }
    }

    // once per sample, only while live
    HOT float render() {
        float s = 0.0f;
        switch (type) {
            case T_SINE:
                ph1 += inc1;
                s = sineF(ph1) * a[0];
                if (a[1] > 1e-4f) s += noiseF(rng) * a[1] * p.g1;            // click
                break;
            case T_SNARE: {
                ph1 += inc1;  ph2 += inc2;
                s = (sineF(ph1) * a[0] + 0.6f * sineF(ph2) * a[1]) * p.g1;
                s += hp1(noiseF(rng), p.k1, z[0]) * a[2] * p.g2;
                break; }
            case T_RIM:
                ph1 += inc1;  ph2 += inc2;
                s = sineF(ph1) * a[0] * p.g1 + sineF(ph2) * a[1] * p.g2 + noiseF(rng) * a[2] * 0.3f;
                break;
            case T_CLAP: {
                float n = hp1(noiseF(rng), p.k1, z[0]);
                n = lp1(n, p.k2, z[1]);  n = lp1(n, p.k2, z[2]);
                s = n * (a[0] + a[1]) * p.g2;
                break; }
            case T_BELL: {
                ph1 += inc1;  ph2 += inc2;
                float x = (float)((((int32_t)ph1 >> 31) | 1) + (((int32_t)ph2 >> 31) | 1)) * 0.5f;
                x = hp1(x, p.k1, z[0]);  x = lp1(x, p.k2, z[1]);  x = lp1(x, p.k2, z[2]);
                s = x * (a[0] * p.g1 + a[1] * p.g2);
                break; }
            case T_METAL: {
                int sum = 0;
                for (int k = 0; k < 6; k++) { mph[k] += minc[k]; sum += ((int32_t)mph[k] >> 31) | 1; }
                float x = (float)sum * (1.0f / 6.0f);
                x = hp1(x, p.k1, z[0]);  x = hp1(x, p.k1, z[1]);  x = lp1(x, p.k2, z[2]);
                s = x * (a[0] * p.g2 + a[1] * p.g1);
                break; }
            case T_NOISE: {
                float n = hp1(noiseF(rng), p.k1, z[0]);
                n = lp1(n, p.k2, z[1]);  n = lp1(n, p.k2, z[2]);
                s = n * a[0] * p.g1;
                break; }
            default: break;
        }
        a[0] += d[0]; a[1] += d[1]; a[2] += d[2];
        return s;
    }
};
