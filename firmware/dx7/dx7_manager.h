#pragma once
// ============================================================
//  dx7_manager.h  —  six-operator FM synthesizer: 12 voices (N_POLY), each six sine operators wired by one of 32 algorithms
//
//  CORE 0 (loop(): MIDI, USB host, UI, serial)   noteOn / noteOff / setParam / setPatch / loadBank ...: all libm work (pow, exp) happens HERE.
//  CORE 1 (loop1(): audio)                       tick(): owns EVERY voice. Core 0 never touches voice state.
//
//  A note-on event carries everything a voice needs, already worked out on core 0: per operator the phase increment, the output
//  level (output level + key level scaling + velocity sensitivity, in log units), the four envelope targets and rates. Core 1 only
//  adds, multiplies and looks tables up (no libm). The patch can therefore be changed or replaced while notes sound.
//
//  Log units: 256 = one octave of amplitude (6 dB). An operator's amplitude is 2^((A - 3840)/256) with A = envelope level + level
//  offset, so A = 3840 is amplitude 1.0. A carrier outputs that amplitude; a modulator shifts the phase of the next operator by
//  2 x its amplitude in cycles (4 pi radians at full level, the DX7's maximum index). Feedback = (y[n-1] + y[n-2]) x 2^(fb-7) / 4 cycles.
//  The level scale, velocity and key-scaling tables and the algorithm routing follow the published DX7 behaviour; the envelope
//  shapes and the LFO are approximations (see README), so a Yamaha cartridge will sound close but not identical.
//
//  After the voices: volume -> effects (shared/stereo_fx.h: wah, tremolo / auto-pan, chorus, reverb) -> limiter (softLimit).
// ============================================================
#include <Arduino.h>
#include <hardware/sync.h>
#include <string.h>
#include <math.h>
#include "config.h"
#include "dx7_patch.h"
#include "dx7_params.h"
#include "dx7_presets.h"
#include "stereo_fx.h"

struct OpNote { uint32_t inc; int16_t base; int16_t lv[4]; uint8_t qr[4]; uint8_t ams, pad; };
struct NoteData { OpNote op[6]; uint8_t alg, fb; };
enum : uint8_t { EV_ON = 1, EV_OFF = 2, EV_PEDAL = 3 };      // panic and all-notes-off are COUNTERS, not events: they must never be dropped by a full ring
struct Evt { uint8_t type, key, vel, pad; NoteData nd; };

#define LIMIT_KNEE 0.72f
static HOT float softLimit(float x) {                          // exactly linear below the knee, smooth compression above, never reaches full scale
    float ax = x < 0.0f ? -x : x;
    if (ax <= LIMIT_KNEE) return x;
    if (!(ax < 16.0f)) ax = 16.0f;
    const float t = (ax - LIMIT_KNEE) * (1.0f / (1.0f - LIMIT_KNEE));
    const float y = LIMIT_KNEE + (1.0f - LIMIT_KNEE) * t / (1.0f + t);
    return x < 0.0f ? -y : y;
}

#define DX_SINE_N   4096
#define DX_AMP_N    4352
#define DX_FAST_UNITS 300.0f              // a stolen / re-struck voice fades by this many log units per control tick (4032 units in ~5 ms)

struct Dx7Voice {
    bool     active = false, gate = false, sustained = false, fast = false, fresh = false;
    uint8_t  key = 0, alg = 0;
    uint32_t born = 0;
    NoteData nd;
    uint8_t  st[6];                        // envelope stage per operator: 0 attack, 1 decay, 2 sustain, 3 release
    float    lv[6];                        // envelope level per operator (log units)
    uint32_t ph[6], inc[6];
    float    amp[6], step[6];
    float    y0 = 0, y1 = 0, fbScale = 0, fade = 0;
};

class Dx7Manager {
public:
    // ---- state read by the UI (core 0) ------------------------------------
    uint8_t  pv[N_PARAMS];
    bool     pedalDown = false;
    volatile uint8_t voicesNow = 0;        // sounding voices (core 1 publishes)
    volatile uint8_t cpuHint = 0;
    uint8_t  patchNo = 0, nBank = DX_N_PRESETS;
    Dx7Patch patch;                        // the voice that new notes play
    Dx7Patch bank[32];

    // =======================================================================
    void init() {
        for (int i = 0; i < DX_SINE_N; i++) SINE[i] = sinf(6.28318530718f * (float)i / (float)DX_SINE_N);
        for (int a = 0; a < DX_AMP_N; a++) AMPT[a] = exp2f(((float)a - 3840.0f) / 256.0f);
        for (int q = 0; q < 64; q++) RATEU[q] = (float)(4 + (q & 3)) * (float)(1 << (q >> 2)) * 2.69f / (float)(SAMPLE_RATE / CONTROL_DIV);   // log units per control tick
        for (int f = 0; f < 8; f++) FBSC[f] = f == 0 ? 0.0f : 0.25f * exp2f((float)(f - 7));
        for (int i = 0; i < 32; i++) dx7Preset(i, bank[i]);
        nBank = DX_N_PRESETS; _t = 0; _pedal = false; _lfoPh = 0; _lfoHeld = 0.0f; _rng = 0x1234567u;
        for (int i = 0; i < N_VOICES; i++) _v[i] = Dx7Voice();
        memcpy(pv, PARAM_DEFAULT, sizeof pv);
        _bendSemi = 0.0f;
        setPatch(0);
        for (int k = P_WAH; k < N_PARAMS; k++) setParam((uint8_t)k, pv[k]);
        for (int k = 0; k < P_WAH; k++) setParam((uint8_t)k, pv[k]);
    }

    // ---- patches ----------------------------------------------------------
    void setPatch(uint8_t n) {
        if (n >= nBank) n = (uint8_t)(n % nBank);
        patchNo = n; patch = bank[n]; _pushGlobals();
    }
    const char* patchName() const { return patch.name; }
    // a DX7 32-voice bank dump (the 4096 data bytes between the header and the checksum): replaces the bank, keeps the program number
    void loadBank(const uint8_t* data) {
        for (int i = 0; i < 32; i++) dx7Unpack(data + 128 * i, bank[i]);
        nBank = 32; setPatch(patchNo);
    }

    // ---- performance --------------------------------------------------------
    void noteOn(uint8_t key, uint8_t vel) {
        if (key < KEY_LO || key > KEY_HI) return;
        if (vel == 0) { noteOff(key); return; }
        Evt e; e.type = EV_ON; e.key = key; e.vel = vel; e.pad = 0; _build(key, vel, e.nd); _push(e);
    }
    void noteOff(uint8_t key) { if (key >= KEY_LO && key <= KEY_HI) { Evt e; memset(&e, 0, sizeof e); e.type = EV_OFF; e.key = key; _push(e); } }
    void setPedal(bool down) { pedalDown = down; Evt e; memset(&e, 0, sizeof e); e.type = EV_PEDAL; e.key = down; _push(e); }
    void pitchBend(int16_t b) { _bendSemi = (float)b * (BEND_RANGE_SEMI / 8192.0f); _pushGlobals(); }
    void panic()       { pedalDown = false; panicSeq = (uint8_t)(panicSeq + 1); }
    void allNotesOff() { offSeq = (uint8_t)(offSeq + 1); }

    void setParam(uint8_t id, uint8_t v) {
        if (id >= N_PARAMS) return;
        if (v > 127) v = 127;
        pv[id] = v;
        if (id >= P_WAH) _fx.setKnobs(pv[P_WAH], pv[P_WPOS], pv[P_WLFO], pv[P_TREM], pv[P_TRATE], pv[P_CHOR], pv[P_REV], pv[P_RSIZE]);
        else _pushGlobals();                       // volume, vibrato; the rest is read when a note starts
    }
    bool handleCC(uint8_t cc, uint8_t v) {
        if (cc == 64) { setPedal(v >= 64); return true; }
        if (cc == 120) { panic(); return true; }
        if (cc == 123) { allNotesOff(); return true; }
        const uint8_t id = paramForCC(cc);
        if (id == 0xFF) return false;
        setParam(id, v); return true;
    }
    bool voiceSounding() const { return voicesNow != 0; }

    // =======================================================================
    //  CORE 1
    // =======================================================================
    HOT void tick(int16_t& outL, int16_t& outR) {
        if ((_t & (CONTROL_DIV - 1)) == 0) _control();
        _t++;
        float s = 0.0f;
        for (int i = 0; i < N_VOICES; i++) if (_v[i].active) s += _voice(_v[i]);
        s *= _gain;
        float l = s, r = s;
        _fx.process(l, r);
        l = softLimit(l); r = softLimit(r);
        outL = (int16_t)(l * 32767.0f); outR = (int16_t)(r * 32767.0f);
    }

    // ---- test hooks ------------------------------------------------------------
    int   activeVoices() const { int n = 0; for (int i = 0; i < N_VOICES; i++) n += _v[i].active; return n; }
    float opAmp(int voice, int op) const { return _v[voice].amp[op]; }

private:
    inline static float SINE[DX_SINE_N + 1];
    inline static float AMPT[DX_AMP_N];
    inline static float RATEU[64];
    inline static float FBSC[8];

    Dx7Voice   _v[N_VOICES];
    StereoFx   _fx;
    Evt        _ring[EVT_RING];
    volatile uint8_t _rw = 0, _rr = 0, panicSeq = 0, offSeq = 0;
    uint8_t    _panicSeen = 0, _offSeen = 0;
    bool       _pedal = false;
    uint32_t   _t = 0, _lfoPh = 0, _rng = 1;
    float      _lfoHeld = 0.0f, _lfoVal = 0.0f, _bendSemi = 0.0f;
    // core 0 -> core 1 single words, each valid by itself
    volatile float    _gain = 0.0f, _bendMul = 1.0f, _pmSemi = 0.0f, _amUnits = 0.0f, _delayInc = 1.0f;
    volatile uint32_t _lfoInc = 0;
    volatile uint8_t  _lfoWave = 0;

    // ---- core 0 ----------------------------------------------------------------
    void _push(const Evt& e) {
        const uint8_t w = _rw, nw = (uint8_t)((w + 1) & (EVT_RING - 1));
        if (nw == _rr) return;                                    // full: drop (64 events deep: only a USB flood could do this)
        _ring[w] = e; __dmb(); _rw = nw;
    }
    static float exp2s(float x) { return exp2f(x); }
    void _pushGlobals() {
        _gain = volGainOf(pv[P_VOL]);
        _bendMul = exp2s(_bendSemi * (1.0f / 12.0f));
        static const float PMS[8] = { 0.0f, 0.0264f, 0.0534f, 0.0889f, 0.1612f, 0.2769f, 0.4967f, 1.0f };
        const float pms = PMS[patch.lpms & 7];
        _pmSemi = pms * 12.0f * (float)patch.lpmd / 99.0f + modDepthOf(pv[P_MOD]) * 0.6f;
        _amUnits = 2048.0f * (float)patch.lamd / 99.0f;
        const float s = (float)patch.lfs / 99.0f;
        const float hz = 0.06f + 49.0f * powf(s, 2.3f);
        _lfoInc = (uint32_t)(hz * (4294967296.0f / (float)SAMPLE_RATE));
        const float delayS = (float)patch.lfd / 99.0f * 3.0f;
        _delayInc = delayS < 0.01f ? 1.0f : 1.0f / (delayS * (float)(SAMPLE_RATE / CONTROL_DIV));
        _lfoWave = patch.lfw;
    }
    void _build(int note, int vel, NoteData& nd) const {
        const Dx7Patch& p = patch;
        int key = note + (int)p.transpose - 24; key = key < 0 ? 0 : key > 127 ? 127 : key;
        const float base = 440.0f * exp2s(((float)key - 69.0f) * (1.0f / 12.0f) + tuneCentsOf(pv[P_TUNE]) * (1.0f / 1200.0f));
        const float velK = velFactorOf(pv[P_VEL]);
        const int  fbv = (int)p.fb + feedbackShiftOf(pv[P_FBK]);
        nd.alg = p.alg & 31; nd.fb = (uint8_t)(fbv < 0 ? 0 : fbv > 7 ? 7 : fbv);
        for (int o = 0; o < 6; o++) {
            const Dx7Op& q = p.op[o]; OpNote& n = nd.op[o];
            float f;
            if (!q.fixed) f = base * (q.coarse ? (float)q.coarse : 0.5f) * (1.0f + (float)q.fine * 0.01f) * exp2s((float)((int)q.det - 7) * (1.5f / 1200.0f));
            else          f = powf(10.0f, (float)(q.coarse & 3) + (float)q.fine * 0.01f);
            if (f > 0.45f * (float)SAMPLE_RATE) f = 0.45f * (float)SAMPLE_RATE;
            n.inc = (uint32_t)(f * (4294967296.0f / (float)SAMPLE_RATE));
            int sol = dx7Sol(q.ol) + dx7KeyScale(key, q);
            sol = sol < 0 ? 0 : sol > 127 ? 127 : sol;
            int sens = (int)((float)q.kvs * velK + 0.5f); sens = sens > 7 ? 7 : sens;
            int b = (sol << 5) + dx7VelScale(vel, sens);
            if (b < 0) b = 0;
            if (!dx7IsCarrier(nd.alg, o)) { b += brightUnitsOf(pv[P_BRIT]); if (b < 0) b = 0; }
            n.base = (int16_t)(b - 4256);
            const int rs = dx7RateScale(key, q.rs);
            for (int i = 0; i < 4; i++) {
                n.lv[i] = (int16_t)((dx7Sol(q.l[i]) >> 1) << 6);
                int qr = ((q.r[i] * 41) >> 6) + rs;
                if (i == 0) qr += attackShiftOf(pv[P_ATK]);
                if (i == 3) qr += releaseShiftOf(pv[P_REL]);
                n.qr[i] = (uint8_t)(qr < 0 ? 0 : qr > 63 ? 63 : qr);
            }
            n.ams = q.ams & 3; n.pad = 0;
        }
    }

    // ---- core 1 ----------------------------------------------------------------
    HOT void _release(Dx7Voice& v, bool fastFade) {
        v.gate = false; v.sustained = false;
        for (int o = 0; o < 6; o++) v.st[o] = 3;
        if (fastFade) v.fast = true;
    }
    HOT void _start(const Evt& e) {
        for (int i = 0; i < N_VOICES; i++) { Dx7Voice& v = _v[i]; if (v.active && v.key == e.key && !v.fast) _release(v, true); }   // a re-struck key: the old note fades quickly
        int live = 0, victim = -1;
        for (int i = 0; i < N_VOICES; i++) if (_v[i].active && !_v[i].fast) live++;
        if (live >= N_POLY) {                                             // steal: the oldest released voice, else the oldest
            uint32_t oldest = 0xFFFFFFFFu, oldestRel = 0xFFFFFFFFu; int ir = -1, io = -1;
            for (int i = 0; i < N_VOICES; i++) { const Dx7Voice& v = _v[i]; if (!v.active || v.fast) continue;
                if (v.born < oldest) { oldest = v.born; io = i; }
                if (!v.gate && !v.sustained && v.born < oldestRel) { oldestRel = v.born; ir = i; } }
            victim = ir >= 0 ? ir : io;
            if (victim >= 0) _release(_v[victim], true);
        }
        int slot = -1;
        for (int i = 0; i < N_VOICES; i++) if (!_v[i].active) { slot = i; break; }
        if (slot < 0) { uint32_t oldest = 0xFFFFFFFFu; for (int i = 0; i < N_VOICES; i++) if (_v[i].born < oldest) { oldest = _v[i].born; slot = i; } }   // pool exhausted: hard cut of the oldest
        Dx7Voice& v = _v[slot];
        v = Dx7Voice();
        v.active = true; v.gate = true; v.key = e.key; v.born = _t; v.nd = e.nd; v.alg = e.nd.alg; v.fresh = true;
        v.fbScale = FBSC[e.nd.fb & 7];
        for (int o = 0; o < 6; o++) { v.st[o] = 0; v.lv[o] = (float)e.nd.op[o].lv[3]; v.ph[o] = 0; v.inc[o] = e.nd.op[o].inc; v.amp[o] = 0.0f; v.step[o] = 0.0f; }
    }

    HOT float _lfoNow() {
        const uint32_t ph = _lfoPh; const float u = (float)(ph >> 8) * (1.0f / 16777216.0f);
        switch (_lfoWave) {
            case 0: { const float a = u < 0.5f ? 0.5f - u : u - 0.5f; return 4.0f * a - 1.0f; }       // triangle
            case 1: return 1.0f - 2.0f * u;                                                           // saw down
            case 2: return 2.0f * u - 1.0f;                                                           // saw up
            case 3: return u < 0.5f ? 1.0f : -1.0f;                                                   // square
            case 4: return StereoFx::lfo(ph);                                                         // sine
            default: return _lfoHeld;                                                                 // sample & hold
        }
    }

    HOT void _control() {
        while (_rr != _rw) {                                      // events from core 0
            __dmb(); const Evt e = _ring[_rr]; _rr = (uint8_t)((_rr + 1) & (EVT_RING - 1));
            switch (e.type) {
                case EV_ON: _start(e); break;
                case EV_OFF:
                    for (int i = 0; i < N_VOICES; i++) { Dx7Voice& v = _v[i]; if (v.active && v.key == e.key && v.gate) { if (_pedal) { v.gate = false; v.sustained = true; } else _release(v, false); } }
                    break;
                case EV_PEDAL:
                    _pedal = e.key != 0;
                    if (!_pedal) for (int i = 0; i < N_VOICES; i++) { Dx7Voice& v = _v[i]; if (v.active && v.sustained) _release(v, false); }
                    break;
                default: break;
            }
        }
        if (panicSeq != _panicSeen) { _panicSeen = panicSeq; _pedal = false; for (int i = 0; i < N_VOICES; i++) if (_v[i].active) _release(_v[i], true); }
        if (offSeq != _offSeen)     { _offSeen = offSeq; for (int i = 0; i < N_VOICES; i++) if (_v[i].active && (_v[i].gate || _v[i].sustained)) _release(_v[i], false); }

        const uint32_t prev = _lfoPh; _lfoPh += _lfoInc * CONTROL_DIV;
        if (_lfoPh < prev) { _rng ^= _rng << 13; _rng ^= _rng >> 17; _rng ^= _rng << 5; _lfoHeld = (float)(int32_t)_rng * (1.0f / 2147483648.0f); }
        const float lfo = _lfoNow();
        const float bend = _bendMul, pmSemi = _pmSemi, amU = _amUnits, dInc = _delayInc;
        uint8_t cnt = 0;
        for (int i = 0; i < N_VOICES; i++) {
            Dx7Voice& v = _v[i];
            if (!v.active) continue;
            v.fade += dInc; if (v.fade > 1.0f) v.fade = 1.0f;
            float mulf = bend;
            if (pmSemi != 0.0f) { float y = lfo * pmSemi * v.fade * (1.0f / 12.0f); y = y > 1.0f ? 1.0f : y < -1.0f ? -1.0f : y; mulf *= 0.5f * StereoFx::exp2x(y + 1.0f); }
            if (mulf > 2.0f) mulf = 2.0f;
            const float am = amU * 0.5f * (1.0f + lfo) * v.fade;
            bool audible = false;
            for (int o = 0; o < 6; o++) {
                const OpNote& n = v.nd.op[o];
                float cur = v.lv[o]; uint8_t st = v.st[o];
                if (v.fast) { cur -= DX_FAST_UNITS; if (cur < 0.0f) cur = 0.0f; }
                else {
                    const float tgt = (float)n.lv[st], u = RATEU[n.qr[st]];
                    if (cur < tgt) { float up = u * (17.0f - cur * (1.0f / 256.0f)) * 0.25f; if (up < u * 0.3f) up = u * 0.3f; cur += up; if (cur >= tgt) { cur = tgt; if (st < 2) st++; } }
                    else if (cur > tgt) { cur -= u; if (cur <= tgt) { cur = tgt; if (st < 2) st++; } }
                    else if (st < 2) st++;
                }
                v.lv[o] = cur; v.st[o] = st;
                int A = (int)cur + n.base - (int)(am * (float)n.ams * (1.0f / 3.0f));
                A = A < 16 ? 16 : A > DX_AMP_N - 1 ? DX_AMP_N - 1 : A;
                const float ta = AMPT[A];
                if (v.fresh) { v.amp[o] = ta; v.step[o] = 0.0f; } else v.step[o] = (ta - v.amp[o]) * (1.0f / (float)CONTROL_DIV);
                float f = (float)n.inc * mulf; if (f > 4.0e9f) f = 4.0e9f;
                v.inc[o] = (uint32_t)f;
                if (dx7IsCarrier(v.alg, o) && A > 330) audible = true;
            }
            v.fresh = false;
            if (!audible && (v.fast || (!v.gate && !v.sustained))) { v.active = false; continue; }
            cnt++;
        }
        voicesNow = cnt;
    }

    HOT float _voice(Dx7Voice& v) {
        const uint8_t* alg = DX7_ALG[v.alg];
        float bus[2] = { 0.0f, 0.0f }; bool has[2] = { false, false };
        float out = 0.0f;
        for (int o = 0; o < 6; o++) {
            const uint8_t f = alg[o];
            const int inb = (f >> 4) & 3, outb = f & 3;
            float m = inb ? bus[inb - 1] * 2.0f : 0.0f;
            if (f & DX_FB_IN) m += (v.y0 + v.y1) * v.fbScale;
            const uint32_t ph = v.ph[o] + ((uint32_t)(int32_t)(m * 16777216.0f) << 8);
            const float s = SINE[ph >> 20] * v.amp[o];
            v.amp[o] += v.step[o]; v.ph[o] += v.inc[o];
            if (f & DX_FB_OUT) { v.y1 = v.y0; v.y0 = s; }
            if (outb == 0) out += s;
            else { const int b = outb - 1; if ((f & 4) && has[b]) bus[b] += s; else bus[b] = s; has[b] = true; }
        }
        return out;
    }
};
