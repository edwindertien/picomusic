#pragma once
// ============================================================
//  piano_manager.h  —  sampled piano: 24 voices playing 4-bit ADPCM zones straight from flash
//
//  CORE 0 (loop(): MIDI, USB host, UI, serial)   noteOn / noteOff / pedal / setParam ...: all libm work happens HERE
//  CORE 1 (loop1(): audio)                       tick(): owns EVERY voice. Core 0 never touches voice state.
//
//  Core 0 -> core 1 is ONE single-producer / single-consumer ring of events (note on / note off / pedal), plus two counters
//  (panic, all-notes-off) that can never be lost. A note-on event already carries everything
//  that needs pow()/exp(): the left / right gain (velocity curve, una corda, pan) and the velocity filter coefficient, so
//  the audio core never calls into libm. Core 1 -> core 0: three words with a bit per sounding key (for the display) and
//  a few single bytes.
//
//  24 notes can sound at once (N_POLY); the pool has 32 slots so that a stolen voice can fade out in ~13 ms instead of being cut.
//
//  After the voices: volume -> effects pipeline (piano_fx.h) -> limiter (softLimit).
//
//  A voice plays one zone: the nearest recorded sample (every 3rd key), shifted by at most +-1 semitone with 4-point Hermite
//  interpolation. ADPCM is decoded on the fly (at most ONE decoded sample per output sample, since the zone rates are
//  16 / 24 / 32 kHz). Velocity 80 plays the recording as it is; softer notes are quieter and darker (one-pole low-pass),
//  louder notes are louder. The recording is stretch-tuned (bass flat, treble sharp) and keeps that tuning.
// ============================================================
#include <Arduino.h>
#include <hardware/sync.h>
#include <string.h>
#include <math.h>
#include "config.h"
#include "piano_adpcm.h"
#include "piano_bank.h"
#include "piano_params.h"
#include "piano_fx.h"

#ifdef PIANO_TRACE                         // host tests: log voice events
struct PTrace { uint32_t t; uint8_t kind, key, voice, pad; };          // kind: 1 start, 2 victim chosen (fades out), 3 end, 4 release, 5 fast release, 6 HARD cut (pool exhausted)
#endif
enum : uint8_t { EV_ON = 1, EV_OFF = 2, EV_PEDAL = 3 };      // panic and all-notes-off are COUNTERS, not events: they must never be dropped by a full ring
struct Evt { uint8_t type, a, b, c; uint16_t zone, pad; float gL, gR, lpA, inc; };      // zone: index into PIANO_ZONE; inc: playback increment (rate x pitch shift), both chosen on core 0

// The output limiter. EXACTLY linear below the knee, so a normal note is not touched at all; above it a smooth compression (unit slope at the knee,
// the curve K + (1-K) t/(1+t), t = excess / (1-K)) that never reaches full scale. It replaced x(27+x^2)/(27+9x^2), which bends from the first sample:
// 6 % gain loss at a peak of 0.45, measured as -33 dB of added distortion on a single loud note at the default volume (-22 dB on a chord).
#define LIMIT_KNEE 0.72f
static HOT float softLimit(float x) {
    float ax = x < 0.0f ? -x : x;
    if (ax <= LIMIT_KNEE) return x;
    if (!(ax < 16.0f)) ax = 16.0f;                                 // huge input (and NaN: the comparison is false): the audio core has no libm to catch it
    const float t = (ax - LIMIT_KNEE) * (1.0f / (1.0f - LIMIT_KNEE));
    const float y = LIMIT_KNEE + (1.0f - LIMIT_KNEE) * t / (1.0f + t);                      // < 0.995 for any input
    return x < 0.0f ? -y : y;
}

struct PianoVoice {
    bool     active = false, gate = false, sustained = false, rel = false, fast = false, lpOn = false;
    uint8_t  key = 0, tail = 0;
    uint32_t pos = 0, n = 0, born = 0, loopStart = 0, loopEnd = 0;
    const PianoZone* zp = nullptr;
    const int16_t* pcm = nullptr; uint32_t headN = 0;           // the PCM head of the zone (headN samples), if any                                // the zone being played (its loop state is restored from here)
    const uint8_t* p = nullptr;
    AdpcmState st;
    float    h0 = 0, h1 = 0, h2 = 0, h3 = 0, frac = 0, inc = 0, baseInc = 0;
    float    lpA = 1.0f, lpY = 0.0f, gL = 0, gR = 0, e = 1.0f, amp = 1.0f, ampStep = 0.0f;

    HOT float level() const { return e * (gL + gR); }                    // loudness estimate, for stealing and for the display

    HOT int16_t next() {                                          // the first headN samples of a zone are PCM, the rest ADPCM (which then continues from the head's history)
        int16_t s;
        if (pos < headN) { s = pcm[pos]; st.push(s); }
        else { const uint32_t q = pos - headN; const uint8_t b = p[q >> 1]; s = st.decode((q & 1) ? (b >> 4) : (b & 15)); }
        pos++; return s;
    }

    HOT float render() {
        frac += inc;
        while (frac >= 1.0f) {                                            // inc is usually < 1: at most one step
            frac -= 1.0f; h0 = h1; h1 = h2; h2 = h3;
            if (loopEnd && pos >= loopEnd) { pos = loopStart; st.restore(zp->lh, zp->lw, zp->lidx); }      // loop: jump back and restore the decoder state saved at the loop start
            if (pos < n) h3 = (float)next(); else { h3 = 0.0f; if (++tail > 4) { active = false; return 0.0f; } }   // the zone ended (its fade-out is in the data)
        }
        const float t = frac;                                             // Catmull-Rom between h1 and h2
        const float c1 = 0.5f * (h2 - h0), c2 = h0 - 2.5f * h1 + 2.0f * h2 - 0.5f * h3, c3 = 0.5f * (h3 - h0) + 1.5f * (h1 - h2);
        float y = ((c3 * t + c2) * t + c1) * t + h1;
        if (lpOn) { lpY += lpA * (y - lpY); y = lpY; }
        amp += ampStep;
        return y * (1.0f / 32768.0f) * amp;
    }
};

class PianoManager {
public:
    // ---- state read by the UI (core 0) ------------------------------------
    uint8_t pv[N_PARAMS];
    bool    softPedal = false;                                    // CC67
    bool    pedalDown = false;                                    // CC64 (what core 0 sent)
    volatile uint32_t keyMask[3] = { 0, 0, 0 };                   // bit (key - KEY_LO) set while a voice for that key is audible (core 1 publishes)
    volatile uint8_t  voicesNow = 0;                              // sounding voices (core 1 publishes)
    uint8_t instrument = 0;                                       // which PIANO_INSTRUMENT the keys play (core 0)

    // =======================================================================
    void init() {
        static_assert(PIANO_CODEC == ADPCM_CODEC, "the bank was encoded with another ADPCM codec: rebuild it with tools/bankconf");
        static_assert(PIANO_N_DEFAULTS == N_PARAMS, "the bank was generated for a different parameter table: rebuild it with tools/bankconf");
        _fastCoef = expf(-6.9f * (float)CONTROL_DIV / ((float)SAMPLE_RATE * FAST_RELEASE_S));
        _gctr = 0; _t = 0; _pedal = false;
        for (int i = 0; i < N_VOICES; i++) _v[i] = PianoVoice();
        setInstrument(0);
    }
    // choose the instrument: its knob defaults are loaded (like a patch); notes already sounding keep playing their own samples
    void setInstrument(uint8_t i) {
        if (i >= PIANO_N_INSTRUMENTS) return;
        instrument = i;
        for (int k = 0; k < N_PARAMS; k++) pv[k] = PIANO_INSTRUMENT[i].def[k];
        for (int k = 0; k < N_PARAMS; k++) setParam((uint8_t)k, pv[k]);
    }
    const char* instrumentName() const { return PIANO_INSTRUMENT[instrument].name; }

    // the zone that plays (key, velocity): the velocity layer first (the nearest one if none contains it), then the key range, then the nearest root, ties to the lower root
    static int selectZone(int inst, int key, int vel) {
        const PianoInstrument& I = PIANO_INSTRUMENT[inst]; int best = -1; int bdv = 1 << 20, bdk = 1 << 20, bdr = 1 << 20, broot = 0;
        for (int z = I.zoneFirst; z < I.zoneFirst + I.zoneCount; z++) {
            const PianoZone& Z = PIANO_ZONE[z];
            const int dv = vel < Z.vlo ? Z.vlo - vel : vel > Z.vhi ? vel - Z.vhi : 0, dk = key < Z.lo ? Z.lo - key : key > Z.hi ? key - Z.hi : 0, dr = abs(key - (int)Z.root);
            if (dv < bdv || (dv == bdv && (dk < bdk || (dk == bdk && (dr < bdr || (dr == bdr && Z.root < broot)))))) { best = z; bdv = dv; bdk = dk; bdr = dr; broot = Z.root; }
        }
        return best;
    }

    // =======================================================================
    //  Parameters (core 0)
    // =======================================================================
    void setParam(uint8_t id, uint8_t val) {
        if (id >= N_PARAMS) return;
        if (val > 127) val = 127;
        pv[id] = val;
        volGain = volGainOf(pv[P_VOL]);
        _recalcPitch();
        fx.setParams(pv);
        decCoef = decayOffOf(pv[P_DECAY]) ? 1.0f : expf(-6.9f * (float)CONTROL_DIV / ((float)SAMPLE_RATE * decayT60Of(pv[P_DECAY])));
        const float rs = relScaleOf(pv[P_REL]);
        for (int k = 0; k < N_KEYS; k++) {                        // damper time per key: long in the bass, short in the treble
            float t60 = 0.5f * powf(2.0f, -((KEY_LO + k) - 24) / 24.0f) * rs; if (t60 < 0.02f) t60 = 0.02f;
            _relCoef[k] = expf(-6.9f * (float)CONTROL_DIV / ((float)SAMPLE_RATE * t60));
        }
    }
    bool handleCC(uint8_t cc, uint8_t val) {
        if (val > 127) val = 127;
        if (cc == 120) { panic(); return true; }
        if (cc == 123) { allNotesOff(); return true; }
        if (cc == 64)  { setPedal(val >= 64); return true; }
        if (cc == 67)  { softPedal = val >= 64; return true; }
        const uint8_t id = paramForCC(cc);
        if (id == 0xFF) return false;
        setParam(id, val);
        return true;
    }
    void pitchBend(int16_t b) { _bendSemi = (float)b * (BEND_RANGE_SEMI / 8192.0f); _recalcPitch(); }

    // =======================================================================
    //  Playing (core 0): everything with pow() / exp() is computed here
    // =======================================================================
    void noteOn(uint8_t key, uint8_t vel) {
        if (vel == 0) { noteOff(key); return; }
        if (key < KEY_LO || key > KEY_HI) return;
        const int zi = selectZone(instrument, key, vel); if (zi < 0) return;
        const PianoZone& Z = PIANO_ZONE[zi];
        const float soft = softOf(pv[P_SOFT]) > (softPedal ? 1.0f : 0.0f) ? softOf(pv[P_SOFT]) : (softPedal ? 1.0f : 0.0f);
        float g = powf((float)vel / (float)Z.vref, hardExpOf(pv[P_HARD])) * (1.0f - 0.45f * soft);          // Z.vref: the velocity this zone was recorded at plays unchanged
        if (Z.gain10) g *= powf(10.0f, (float)Z.gain10 * (1.0f / 200.0f));
        float pan = ((float)key - 64.5f) * (1.0f / 43.5f); if (pan > 1.0f) pan = 1.0f; if (pan < -1.0f) pan = -1.0f;
        const float th = 0.78539816f * (1.0f + widthOf(pv[P_WIDTH]) * pan);
        // velocity filter: the recorded velocity and above play unfiltered; softer notes get darker; TONE and una corda scale it
        const int below = vel < Z.vref ? (int)Z.vref - vel : 0;
        float fc = 20000.0f * powf(2.0f, -(float)below * (8.0f / 127.0f)) * toneMulOf(pv[P_TONE]) * powf(2.0f, -3.5f * soft);
        float a = 1.0f;
        if (fc < 15000.0f) a = 1.0f - expf(-6.2831853f * fc / (float)SAMPLE_RATE);
        const float inc = ((float)Z.rate / (float)SAMPLE_RATE) * powf(2.0f, (key - (int)Z.root) / 12.0f);
        Evt e = { EV_ON, key, vel, 0, (uint16_t)zi, 0, g * 1.41421356f * cosf(th), g * 1.41421356f * sinf(th), a, inc };
        _push(e);
    }
    void noteOff(uint8_t key) { if (key >= KEY_LO && key <= KEY_HI) _push({ EV_OFF, key, 0, 0, 0, 0, 0, 0, 0, 0 }); }
    void setPedal(bool down) { pedalDown = down; _push({ EV_PEDAL, (uint8_t)down, 0, 0, 0, 0, 0, 0, 0, 0 }); }
    void allNotesOff() { offSeq = (uint8_t)(offSeq + 1); }                    // release every voice (CC123), even under the pedal
    void panic() { pedalDown = false; panicSeq = (uint8_t)(panicSeq + 1); }   // fast-release every voice and lift the pedal (CC120)

    // =======================================================================
    //  Audio (core 1)
    // =======================================================================
    HOT void tick(int16_t& outL, int16_t& outR) {
        if (_gctr == 0) _control();
        if (++_gctr >= CONTROL_DIV) _gctr = 0;
        float L = 0.0f, R = 0.0f;
        for (int i = 0; i < N_VOICES; i++) {
            PianoVoice& v = _v[i];
            if (!v.active) continue;
            const float y = v.render();
            L += y * v.gL; R += y * v.gR;
        }
        const float vg = volGain;
        L *= vg; R *= vg;
        fx.process(L, R);                                         // wah -> tremolo / auto-pan -> chorus -> reverb (nothing at all when all are off)
        outL = (int16_t)(softLimit(L) * 32767.0f);
        outR = (int16_t)(softLimit(R) * 32767.0f);
        _t++;
    }

    // ---- diagnostics (core 0; racy reads of core-1 state are fine for a display) ----
    uint8_t activeVoices() const { uint8_t n = 0; for (int i = 0; i < N_VOICES; i++) if (_v[i].active) n++; return n; }
    bool    keySounding(int key) const { const int k = key - KEY_LO; return k >= 0 && k < N_KEYS && ((keyMask[k >> 5] >> (k & 31)) & 1u); }

    volatile float volGain = 0.5f, decCoef = 1.0f, pitchMul = 1.0f;
    PianoFx fx;                                                   // the effects pipeline (state on core 1, coefficients from core 0)
#ifdef PIANO_TRACE
    PTrace   trace[16384];
    uint32_t traceN = 0;
#endif

private:
    PianoVoice _v[N_VOICES];
    Evt        _ring[EVT_RING];
    volatile uint8_t _rw = 0, _rr = 0, panicSeq = 0, offSeq = 0;
    uint8_t    _panicSeen = 0, _offSeen = 0;
    float      _relCoef[N_KEYS], _fastCoef = 0.5f;
    float      _bendSemi = 0.0f;
    uint8_t    _gctr = 0;
    bool       _pedal = false;                                    // core 1's copy
    uint32_t   _t = 0;

    void _recalcPitch() { pitchMul = powf(2.0f, (tuneCentsOf(pv[P_TUNE]) * 0.01f + _bendSemi) * (1.0f / 12.0f)); }
    void _push(const Evt& e) {                                    // producer: core 0
        const uint8_t w = _rw, nw = (uint8_t)((w + 1) & (EVT_RING - 1));
        if (nw == _rr) return;                                    // full: drop (64 events deep: only a USB flood could do this)
        _ring[w] = e; __dmb(); _rw = nw;
    }

    // ---- core 1 ----------------------------------------------------------------
#ifdef PIANO_TRACE
    HOT void _tr(uint8_t kind, uint8_t key, int voice) { if (traceN < 16384) { trace[traceN++] = { _t, kind, key, (uint8_t)voice, 0 }; } }
#else
    HOT void _tr(uint8_t, uint8_t, int) {}
#endif

    HOT void _release(PianoVoice& v, int idx, bool fast) {
        if (!v.rel) { v.rel = true; _tr(fast ? 5 : 4, v.key, idx); }
        v.gate = false; v.sustained = false; if (fast) v.fast = true;
    }

    // Soft polyphony limit. When N_POLY voices are already sounding, the victim (the quietest RELEASING voice, else the oldest) is not cut: it is
    // fast-released (15 ms fade) and keeps its slot until it has faded, while the new note starts in one of the spare slots. A hard cut is a click
    // (measured: 10x the signal's normal roughness at the cut), so it only happens if the whole pool is busy fading out.
    HOT int _alloc() {
        int live = 0;
        for (int i = 0; i < N_VOICES; i++) if (_v[i].active && !_v[i].fast) live++;
        if (live >= N_POLY) {
            int best = -1; float bl = 1e30f;
            for (int i = 0; i < N_VOICES; i++) if (_v[i].active && !_v[i].fast && _v[i].rel && _v[i].level() < bl) { bl = _v[i].level(); best = i; }
            if (best < 0) { uint32_t oldest = 0xFFFFFFFFu; for (int i = 0; i < N_VOICES; i++) if (_v[i].active && !_v[i].fast && _v[i].born < oldest) { oldest = _v[i].born; best = i; } }
            if (best >= 0) { _tr(2, _v[best].key, best); _release(_v[best], best, true); }
        }
        for (int i = 0; i < N_VOICES; i++) if (!_v[i].active) return i;
        int best = 0; float bl = 1e30f;                                            // every slot is busy: cut the quietest (rare: needs >4 voices fading at once)
        for (int i = 0; i < N_VOICES; i++) if (_v[i].level() < bl) { bl = _v[i].level(); best = i; }
        _tr(6, _v[best].key, best);
        return best;
    }

    HOT void _noteOn(const Evt& e) {
        for (int i = 0; i < N_VOICES; i++) { PianoVoice& v = _v[i]; if (v.active && v.key == e.a && !v.fast) _release(v, i, true); }     // re-struck key: the old string is damped quickly
        const int idx = _alloc(); PianoVoice& v = _v[idx]; const PianoZone& z = PIANO_ZONE[e.zone];
        v = PianoVoice();
        v.active = true; v.gate = true; v.key = e.a; v.born = _t; v.pcm = (const int16_t*)(PIANO_DATA + z.off); v.headN = z.head; v.p = PIANO_DATA + z.off + 2u * z.head; v.n = z.n; v.zp = &z; v.st.init(z.pred0, z.idx0);
        v.loopStart = z.loopStart; v.loopEnd = z.loopEnd;
        v.h0 = v.h1 = v.h2 = (float)z.pred0; v.h3 = (float)v.next();
        v.baseInc = e.inc; v.inc = v.baseInc * pitchMul;
        v.gL = e.gL; v.gR = e.gR; v.lpA = e.lpA; v.lpOn = e.lpA < 1.0f; v.e = 1.0f; v.amp = 1.0f;
        _tr(1, e.a, idx);
    }

    HOT void _control() {
        while (_rr != _rw) {                                      // events from core 0
            __dmb(); const Evt e = _ring[_rr]; _rr = (uint8_t)((_rr + 1) & (EVT_RING - 1));
            switch (e.type) {
                case EV_ON: _noteOn(e); break;
                case EV_OFF:
                    for (int i = 0; i < N_VOICES; i++) { PianoVoice& v = _v[i]; if (v.active && v.key == e.a && v.gate) { if (_pedal) { v.gate = false; v.sustained = true; } else _release(v, i, false); } }
                    break;
                case EV_PEDAL:
                    _pedal = e.a != 0;
                    if (!_pedal) for (int i = 0; i < N_VOICES; i++) { PianoVoice& v = _v[i]; if (v.active && v.sustained) _release(v, i, false); }
                    break;
                default: break;
            }
        }
        if (panicSeq != _panicSeen) { _panicSeen = panicSeq; _pedal = false; for (int i = 0; i < N_VOICES; i++) if (_v[i].active) _release(_v[i], i, true); }
        if (offSeq != _offSeen)     { _offSeen = offSeq; for (int i = 0; i < N_VOICES; i++) if (_v[i].active && !_v[i].rel) _release(_v[i], i, false); }
        const float pm = pitchMul, dc = decCoef;
        uint32_t m0 = 0, m1 = 0, m2 = 0; uint8_t cnt = 0;
        for (int i = 0; i < N_VOICES; i++) {
            PianoVoice& v = _v[i];
            if (!v.active) continue;
            float c = dc;
            if (v.rel) c *= v.fast ? _fastCoef : _relCoef[v.key - KEY_LO];
            v.e *= c;
            if (v.rel && v.e < (v.fast ? 3e-3f : 1e-4f)) { v.active = false; _tr(3, v.key, i); continue; }    // a fast fade ends at -50 dB: inaudible under the note that replaced it, and it frees the slot sooner
            v.inc = v.baseInc * pm;
            v.ampStep = (v.e - v.amp) * (1.0f / (float)CONTROL_DIV);
            cnt++;
            if (v.level() > 0.004f) { const int k = v.key - KEY_LO; if (k < 32) m0 |= 1u << k; else if (k < 64) m1 |= 1u << (k - 32); else m2 |= 1u << (k - 64); }
        }
        keyMask[0] = m0; keyMask[1] = m1; keyMask[2] = m2; voicesNow = cnt;
    }
};
