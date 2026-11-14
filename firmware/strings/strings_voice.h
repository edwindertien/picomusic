#pragma once
// ============================================================
//  strings_voice.h  —  one note of the string machine
//
//    one phase accumulator  --> saw 16'  (phase)
//                           --> saw  8'  (phase << 1)   phase-locked octaves, like a divider chain
//                           --> saw  4'  (phase << 2)
//    optional 2nd 8' saw, detuned (unison)
//    pizzicato: (8' + 4') x its own exponential decay x a gate that follows the key
//    all x the note envelope (linear attack / hold / linear release, applied squared)
//
//  The voice does NOT filter: it adds its footages to four shared buses (16', 8', 4', pizz);
//  the registration filters and the ensemble live in StringsManager. So a voice costs about
//  1-3 PolyBLEP saws per sample, independent of the registration filters.
//
//  THREADING CONTRACT (identical to the analog synth)
//    live == 0 : the voice belongs to core 0. It may write ANY field, then publishes with
//                __dmb(); live = 1;
//    live == 1 : the voice belongs to core 1. Core 0 may only increment relReq / fastReq and
//                read level. Core 1 sets live = 0 itself (last access) when the envelope ended.
// ============================================================
#include <stdint.h>
#include <hardware/sync.h>        // __dmb()
#include "config.h"
#include "strings_params.h"

static inline float hzToInc(float hz) { return hz * (4294967296.0f / (float)SAMPLE_RATE); }
static inline float envRate(float seconds) { return 1.0f / (seconds * (float)SAMPLE_RATE); }

#define STEAL_RATE  (1000.0f / ((float)STEAL_RELEASE_MS * (float)SAMPLE_RATE))
#define PZ_ATK_RATE (1000.0f / (PZ_ATTACK_MS * (float)SAMPLE_RATE))

// Engine values derived from the knobs by core 0 (StringsManager::_derive). Single words; core 1 reads.
struct EngineParams {
    // registrations: weight after the filters (0 = off) and which footage buses are needed
    float    wBass = 0, wCello = 0, wViola = 0, wViolin = 0, wPizz = 0;
    bool     use16 = false, use8 = false, use4 = false, usePz = false;
    // note envelope / unison / pizzicato
    float    atkRate = 0.01f, relRate = 0.001f;
    bool     unison = false;
    float    detuneRatio = 1.0f;
    float    pzCoef = 0.9f;                 // pizzicato decay per control tick
    // modulation
    float    modDepth = 0.0f;               // vibrato depth, semitones
    uint32_t vibInc = 0;
    float    bend = 0.0f;                   // pitch bend, semitones
    // shared sections
    float    toneA = 0.5f;                  // tone low-pass coefficient
    float    ensDepth = 1.0f, ensShimmer = 0.5f, ensMix = 1.0f;
    uint32_t ensSlowInc = 0;
    float    gain = 0.01f;                  // output gain (full scale = 1.0)
};

// ---- PolyBLEP saw ----------------------------------------------------------------------
static HOT float polyBlep(float t, float dt) {
    if (t < dt)          { const float x = t / dt;            return x + x - x * x - 1.0f; }
    if (t > 1.0f - dt)   { const float x = (t - 1.0f) / dt;   return x * x + x + x + 1.0f; }
    return 0.0f;
}
static HOT float sawBlep(float t, float dt) { return 2.0f * t - 1.0f - polyBlep(t, dt); }

struct StringVoice {
    enum : uint8_t { IDLE = 0, ATTACK, HOLD, RELEASE };

    // ---- shared between cores ----
    volatile uint8_t live = 0, relReq = 0, fastReq = 0;

    // ---- core 0 while !live, core 1 while live ----
    uint32_t ph = 0, ph2 = 0, inc16 = 0, inc8b = 0;
    float    noteInc16 = 0.0f;
    float    dt16 = 0, dt8 = 0, dt4 = 0, dt8b = 0;
    bool     ok16 = false, ok8 = false, ok4 = false, ok8b = false;     // footage below ~0.45 fs?
    uint8_t  st = IDLE;
    float    level = 0.0f, atk = 0.01f, rel = 0.001f;
    float    amp = 0, ampTgt = 0, ampStep = 0;
    float    gate = 0, pz = 1.0f, pzAmp = 0, pzTgt = 0, pzStep = 0;
    uint8_t  relSeen = 0, fastSeen = 0, ctr = 1;

    float levelNow() const { return level; }     // core 0 may read (stale is fine)

    // ---- core 0, only while live == 0 ----
    void start(float inc16_, const EngineParams& p, float modRatio, uint32_t seed) {
        noteInc16 = inc16_;
        ph  = seed * 2654435761u;                // random start phases: no coherent doubling when a key is retriggered
        ph2 = (seed ^ 0x9E3779B9u) * 2246822519u;
        st = ATTACK; level = 0.0f; atk = p.atkRate; rel = p.relRate;
        amp = ampTgt = ampStep = 0.0f;
        gate = 0.0f; pz = 1.0f; pzAmp = pzTgt = pzStep = 0.0f;
        relSeen = relReq; fastSeen = fastReq;
        control(p, modRatio);                    // prime the first block
        ctr = CONTROL_DIV;
    }

    // ---- core 1: once per CONTROL_DIV samples ----
    HOT void control(const EngineParams& p, float modRatio) {
        if (st == IDLE) {                        // finished during the previous block: amp has ramped to zero
            __dmb();
            live = 0;                            // LAST access to this voice
            return;
        }
        const uint8_t fr = fastReq, rr = relReq;
        if (fr != fastSeen)      { fastSeen = fr; relSeen = rr; rel = STEAL_RATE; st = RELEASE; }
        else if (rr != relSeen)  { relSeen = rr;  rel = p.relRate; st = RELEASE; }   // release time read at key-up

        if (st == ATTACK)        { level += atk * (float)CONTROL_DIV; if (level >= 1.0f) { level = 1.0f; st = HOLD; } }
        else if (st == RELEASE)  { level -= rel * (float)CONTROL_DIV; if (level <= 0.0f) { level = 0.0f; st = IDLE; } }

        amp = ampTgt;                            // main amplitude: level squared (closer to an exponential release)
        ampTgt = level * level;
        ampStep = (ampTgt - amp) * (1.0f / (float)CONTROL_DIV);

        if (st == RELEASE) { gate -= rel * (float)CONTROL_DIV; if (gate < 0.0f) gate = 0.0f; }
        else               { gate += PZ_ATK_RATE * (float)CONTROL_DIV; if (gate > 1.0f) gate = 1.0f; }
        pz *= p.pzCoef;
        if (pz < 1e-5f) pz = 0.0f;
        pzAmp = pzTgt;
        pzTgt = pz * gate;
        pzStep = (pzTgt - pzAmp) * (1.0f / (float)CONTROL_DIV);

        const float base = noteInc16 * modRatio;               // 16' phase increment
        inc16 = (uint32_t)base;
        dt16 = base * (1.0f / 4294967296.0f);
        dt8  = dt16 * 2.0f;
        dt4  = dt16 * 4.0f;
        ok16 = dt16 < 0.45f; ok8 = dt8 < 0.45f; ok4 = dt4 < 0.45f;
        inc8b = (uint32_t)(base * 2.0f * p.detuneRatio);
        dt8b  = dt8 * p.detuneRatio;
        ok8b  = dt8b < 0.45f;
    }

    // ---- core 1: once per sample; adds this voice to the footage buses ----
    HOT void render(const EngineParams& p, float modRatio, float& b16, float& b8, float& b4, float& bpz) {
        if (--ctr == 0) {
            ctr = CONTROL_DIV;
            control(p, modRatio);
            if (!live) return;
        }
        const float K = 1.0f / 4294967296.0f;
        ph += inc16;
        float s8 = 0.0f, s4 = 0.0f;
        if (p.use16 && ok16) b16 += sawBlep((float)ph * K, dt16) * amp;
        if ((p.use8 || p.usePz) && ok8) {
            s8 = sawBlep((float)(ph << 1) * K, dt8);
            if (p.unison) { ph2 += inc8b; if (ok8b) s8 = 0.5f * (s8 + sawBlep((float)ph2 * K, dt8b)); }
            if (p.use8) b8 += s8 * amp;
        }
        if ((p.use4 || p.usePz) && ok4) {
            s4 = sawBlep((float)(ph << 2) * K, dt4);
            if (p.use4) b4 += s4 * amp;
        }
        if (p.usePz) bpz += 0.5f * (s8 + s4) * pzAmp;
        amp += ampStep;
        pzAmp += pzStep;
    }
};
