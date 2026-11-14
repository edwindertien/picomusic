#pragma once
// ============================================================
//  bass_voice.h  —  the one voice: 3 oscillators + sub + noise -> ladder -> amp.   Mono, Moog-style.
//
//  THREADING: DParams-like state `p` is written by core 0 (derive(), one aligned word per field, a mix of old and
//  new values for one control tick is harmless); everything else belongs to core 1.
//
//  Per control tick (16 samples): envelopes, glide, oscillator increments, filter cutoff -> (g, k), amplitude.
//  The cutoff is computed in OCTAVES:  cutOct + key * (pitch - KEY_ROOT)/12 + envDepth * envLevel.  Keyboard tracking
//  and envelope depth are therefore exact octaves, and the control path needs no libm.
//  Per sample: 5 oscillator terms, 4 ladder stages, one multiply. The ladder gets exact coefficients (bass_tables.h)
//  so RES 127 self-oscillates IN TUNE and the KEY knob makes it follow the keyboard.
// ============================================================
#include <stdint.h>
#include "config.h"
#include "bass_tables.h"
#include "bass_dsp.h"
#include "bass_params.h"

#define INC_C   (440.0f * 4294967296.0f / (float)SAMPLE_RATE)      // phase increment of 440 Hz
#define RES_PITCH_COMP 0.2022f      // above critical the saturation pulls the self-oscillation flat: ~350 cents per unit of (r - 1) at low cutoffs
#define RES_PITCH_COMP_HF 0.10f     // ... and increasingly so above ~320 Hz: multiplier 1 + 0.10 * (octaves above 320 Hz)^2

struct BParams {
    float volGain = 0.25f, drive = 1.0f;
    float m1 = 0, m2 = 0, m3 = 0, mS = 0, mN = 0;
    float semi1 = 0, semi2 = 0, semi3 = 0;
    uint8_t w1 = 0, w2 = 0, w3 = 0;
    float cutOct = 4.0f, resR = 0.0f, keyAmt = 0.0f, envOct = 0.0f, velAmt = 0.0f, accAmt = 0.0f;
    float fAtt = 1.0f, fDec = 0.9f, fSus = 0.0f, fRel = 0.9f, aAtt = 1.0f, aDec = 0.9f, aSus = 1.0f, aRel = 0.9f;
    float glideK = 1.0f, chk = 0.5f;
};

struct BassVoice {
    BParams p;
    // ---- state (core 1) ----
    bool     gate = false, accent = false, panicking = false;
    float    vel = 1.0f, pitch = 36.0f, tgt = 36.0f;
    uint32_t ph1 = 0, ph2 = 0, ph3 = 0, phS = 0, inc1 = 0, inc2 = 0, inc3 = 0, rng = 1;
    float    dt1 = 0, dt2 = 0, dt3 = 0;
    Env      fEnv, aEnv;
    Ladder   lp;
    float    amp = 0.0f, ampStep = 0.0f;

    // =====================================================================
    //  core 0: knob values -> coefficients
    // =====================================================================
    void derive(const uint8_t* v) {
        p.w1 = (uint8_t)waveOf(v[P_WAV1]); p.w2 = (uint8_t)waveOf(v[P_WAV2]); p.w3 = (uint8_t)waveOf(v[P_WAV3]);
        p.semi1 = octOf(v[P_OCT1]) * 12.0f;
        p.semi2 = octOf(v[P_OCT2]) * 12.0f + centsOf(v[P_DET2]) * 0.01f;
        p.semi3 = octOf(v[P_OCT3]) * 12.0f + centsOf(v[P_DET3]) * 0.01f;
        p.m1 = mixOf(v[P_MIX1]); p.m2 = mixOf(v[P_MIX2]); p.m3 = mixOf(v[P_MIX3]); p.mS = mixOf(v[P_SUB]); p.mN = mixOf(v[P_NOISE]);
        const float vf = v[P_VOL] / 127.0f;
        p.volGain = vf * vf * OUT_GAIN;
        p.drive = driveOf(v[P_DRV]);
        p.cutOct = cutOctOf(v[P_CUT]); p.keyAmt = v[P_KEY] / 127.0f; p.envOct = envOctsOf(v[P_ENV]);
        p.velAmt = v[P_VEL] / 127.0f; p.accAmt = v[P_ACC] / 127.0f;
        p.resR = resRatioOf(v[P_RES]);
        p.fAtt = rate(attackSecOf(v[P_FA])); p.aAtt = rate(attackSecOf(v[P_AA]));
        p.fDec = decayCoef(timeSecOf(v[P_FD])); p.fRel = decayCoef(timeSecOf(v[P_FR]));
        p.aDec = decayCoef(timeSecOf(v[P_AD])); p.aRel = decayCoef(timeSecOf(v[P_AR]));
        p.fSus = v[P_FS] / 127.0f; p.aSus = v[P_AS] / 127.0f;
        p.glideK = v[P_GLIDE] < 1 ? 1.0f : 1.0f - decayCoef(glideSecOf(v[P_GLIDE]));       // fraction of the remaining distance per tick
        p.chk = decayCoef(0.012f);
    }
    // phase increment for a pitch in semitones; capped at ~17 kHz (note 127 plus +2 octaves would otherwise be ~50 kHz: past Nyquist, and an overflow of uint32)
    static HOT uint32_t incFor(float semis) {
        float x = (semis - 69.0f) * (1.0f / 12.0f);
        if (x > 5.3f) x = 5.3f;
        return (uint32_t)(INC_C * exp2fast(x));
    }
    static float rate(float seconds) { const float r = (float)CONTROL_DIV / ((float)SAMPLE_RATE * seconds); return r > 1.0f ? 1.0f : r; }

    // =====================================================================
    //  core 1
    // =====================================================================
    HOT bool active() const { return aEnv.stage != E_IDLE; }

    void noteOn(float note, float vel01, bool legato, bool acc) {
        vel = vel01; accent = acc; tgt = note; panicking = false;
        const bool glide = legato && gate;                       // glide and no retrigger only while another note is still held
        if (!glide) pitch = note;
        if (!active()) { lp.reset(lp.g); ph1 = ph2 = ph3 = phS = 0; }    // clean start from silence
        gate = true;
        if (!glide) { fEnv.trigger(); aEnv.trigger(); }
    }
    void gateOff() { gate = false; fEnv.release(); aEnv.release(); }
    void panic()   { gate = false; panicking = true; fEnv.release(); aEnv.release(); }

    HOT void control(float bend) {
        fEnv.step(p.fAtt, p.fDec, p.fSus, p.fRel);
        aEnv.step(p.aAtt, p.aDec, p.aSus, panicking ? p.chk : p.aRel);
        pitch += (tgt - pitch) * p.glideK;
        if (!active()) return;
        const float pp = pitch + bend;
        inc1 = incFor(pp + p.semi1);
        inc2 = incFor(pp + p.semi2);
        inc3 = incFor(pp + p.semi3);
        dt1 = (float)inc1 * (1.0f / 4294967296.0f); dt2 = (float)inc2 * (1.0f / 4294967296.0f); dt3 = (float)inc3 * (1.0f / 4294967296.0f);

        float depth = p.envOct * (1.0f + 1.5f * p.accAmt * (accent ? 1.0f : 0.0f)) * (1.0f - 0.7f * p.velAmt * (1.0f - vel));
        float xo = p.cutOct + p.keyAmt * (pp - (float)KEY_ROOT) * (1.0f / 12.0f) + depth * fEnv.level;
        if (xo < 0.0f) xo = 0.0f;
        if (xo > FILTER_OCTAVES) xo = FILTER_OCTAVES;
        float g, kc; ladderLookup(xo, g, kc);
        if (p.resR > 1.0f) {                                                              // keep the self-oscillation on pitch: the saturation pulls it flat,
            const float d = xo > 4.0f ? xo - 4.0f : 0.0f;                                  // and more so as the cutoff rises (measured, see test_dsp.py)
            g *= 1.0f + RES_PITCH_COMP * (p.resR - 1.0f) * (1.0f + RES_PITCH_COMP_HF * d * d);
        }
        lp.k = p.resR * kc;
        lp.setTarget(g);
        if (!lp.sane()) lp.reset(g);                                                     // numerical blow-up: reset instead of screaming

        const float ampTgt = aEnv.level * p.volGain * (1.0f + 0.5f * p.accAmt * (accent ? 1.0f : 0.0f)) * (1.0f - p.velAmt * (1.0f - vel));
        ampStep = (ampTgt - amp) * (1.0f / (float)CONTROL_DIV);
    }

    HOT float render() {
        ph1 += inc1; ph2 += inc2; ph3 += inc3; phS += inc1 >> 1;
        float x = 0.0f;
        if (p.m1 > 0.0f) x += p.m1 * wave(p.w1, ph1, dt1);
        if (p.m2 > 0.0f) x += p.m2 * wave(p.w2, ph2, dt2);
        if (p.m3 > 0.0f) x += p.m3 * wave(p.w3, ph3, dt3);
        if (p.mS > 0.0f) x += p.mS * oscSquare(phS, dt1 * 0.5f);
        if (p.mN > 0.0f) x += p.mN * noiseF(rng);
        const float y = lp.tick(x * p.drive * INPUT_SCALE);
        amp += ampStep;
        return y * amp;
    }
    static HOT float wave(uint8_t w, uint32_t ph, float dt) { return w == 0 ? oscSaw(ph, dt) : w == 1 ? oscSquare(ph, dt) : oscTri(ph); }
};
