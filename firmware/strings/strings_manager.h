#pragma once
// ============================================================
//  strings_manager.h  —  polyphony, parameters, sustain pedal and the audio tick
//
//  CORE 0 (loop(): MIDI, USB host, UI, serial) — everything except tick()
//  CORE 1 (loop1(): audio)                     — tick() only
//
//  Same rules as the analog synth: no spinlock; core 0 only initialises voices that core 1 has
//  handed back (live == 0) and publishes them behind a barrier; for live voices it only posts
//  release requests. See strings_voice.h.
//
//  SIGNAL CHAIN (all on core 1)
//    voices --> buses 16' / 8' / 4' / pizz
//    16' -> BASS filter   8' -> CELLO filter, VIOLA filter   4' -> VIOLIN filter   pizz -> PIZZ filter
//    sum (x registration weights) -> TONE low-pass -> ENSEMBLE (stereo) -> volume -> soft limiter
//
//  VOICE ALLOCATION — "the new note always wins" (see the analog synth docs)
//    victims: the quietest voice whose KEY IS UP (released tail or pedal-sustained), else the
//    oldest held voice; they fade out in STEAL_RELEASE_MS while the new note starts in a free slot.
//
//  SUSTAIN PEDAL (CC64, serial `sustain`, button B): while down, a key-up does not release the
//  voice; when the pedal goes up every sustained voice is released.
// ============================================================
#include <Arduino.h>
#include <hardware/sync.h>
#include "config.h"
#include "wavetable.h"
#include "strings_params.h"
#include "strings_voice.h"
#include "strings_dsp.h"

static_assert(STRINGS_POOL <= 32, "tick() keeps one live bit per voice in a uint32_t");
static_assert(STRINGS_POOL > MAX_ACTIVE_VOICES, "the pool needs spare slots for voices that are fading out");

class StringsManager {
public:
    // ---- state read by the UI (core 0) -------------------------------------
    uint8_t      pv[N_PARAMS];          // knob values 0..127
    EngineParams ep;
    const char*  patchName = "Ensemble";

    void init() {
        for (int i = 0; i < 128; i++)
            _noteInc16[i] = hzToInc(0.5f * 440.0f * powf(2.0f, ((float)i - 69.0f) / 12.0f));   // 16' = half the note frequency
        for (int i = 0; i < 128; i++) _map[i] = NONE;
        for (int i = 0; i < STRINGS_POOL; i++) {
            _voices[i].live = 0;
            _held[i] = _dying[i] = _sust[i] = false;
            _note[i] = 255; _age[i] = 0;
        }
        _clock = 0; _pedal = false; _gctr = 0; _modRatio = 1.0f;
        _fBass.design(FILT_BASS_HZ, FILT_BASS_Q);
        _fCello.design(FILT_CELLO_HZ, FILT_CELLO_Q);
        _fViola.design(FILT_VIOLA_HZ, FILT_VIOLA_Q);
        _fViolin.design(FILT_VIOLIN_HZ, FILT_VIOLIN_Q);
        _fPizz.design(FILT_PIZZ_HZ, FILT_PIZZ_Q);
        _ens.clear(); _tone = 0.0f; _vibPhase = 0;
        loadPatch(0);
    }

    // =======================================================================
    //  Parameters (core 0)
    // =======================================================================
    void setParam(uint8_t id, uint8_t val) {
        if (id >= N_PARAMS) return;
        if (val > 127) val = 127;
        pv[id] = val;
        patchName = "Custom";
        _derive(id);
    }
    void loadPatch(uint8_t idx) {
        idx %= N_PATCHES;
        for (uint8_t i = 0; i < N_PARAMS; i++) { pv[i] = PATCHES[idx][i]; _derive(i); }
        patchName = PATCH_NAMES[idx];
        _patchIdx = idx;
    }
    uint8_t patchIndex() const { return _patchIdx; }

    bool handleCC(uint8_t cc, uint8_t val) {
        if (val > 127) val = 127;
        switch (cc) {
            case CC_SUSTAIN_PEDAL: setPedal(val >= 64); return true;
            case CC_ALL_NOTES_OFF: allNotesOff();       return true;
            case CC_ALL_SOUND_OFF: panic();             return true;
            default: break;
        }
        const uint8_t id = paramForCC(cc);
        if (id == 0xFF) return false;
        setParam(id, val);
        return true;
    }

    void pitchBend(int16_t bend) { ep.bend = (float)bend * (2.0f / 8192.0f); }   // +-2 semitones

    // =======================================================================
    //  Sustain pedal (core 0)
    // =======================================================================
    void setPedal(bool down) {
        const bool was = _pedal;
        _pedal = down;
        if (was && !down) {                                   // pedal up: release everything it was holding
            for (int i = 0; i < STRINGS_POOL; i++)
                if (_sust[i]) { _sust[i] = false; if (_voices[i].live) _voices[i].relReq = (uint8_t)(_voices[i].relReq + 1); }
        }
    }
    bool sustainActive() const { return _pedal; }

    // =======================================================================
    //  Notes (core 0)
    // =======================================================================
    void noteOn(uint8_t note, uint8_t /*velocity*/) {
        if (note > 127) return;
        if (_map[note] != NONE) _retire(_map[note]);          // same key again without a note-off

        while (_countCounted() >= MAX_ACTIVE_VOICES) {        // polyphony cap: the new note wins
            const int v = _pickVictim();
            if (v < 0) break;
            _retire(v);
        }
        int slot = _findFree();
        if (slot < 0) slot = _waitFree();
        if (slot < 0) return;
        _start(slot, note);
    }

    void noteOff(uint8_t note) {
        if (note > 127) return;
        const uint8_t v = _map[note];
        if (v == NONE) return;                                // stolen, or never started
        _map[note] = NONE;
        if (_held[v]) {
            _held[v] = false;
            if (_pedal) _sust[v] = true;                      // pedal down: keep sounding
            else        _voices[v].relReq = (uint8_t)(_voices[v].relReq + 1);
        }
    }

    void allNotesOff() {                                      // normal release of everything held or pedal-sustained
        for (int i = 0; i < STRINGS_POOL; i++) {
            if (_voices[i].live && (_held[i] || _sust[i])) {
                _held[i] = false; _sust[i] = false;
                _voices[i].relReq = (uint8_t)(_voices[i].relReq + 1);
            }
        }
        for (int i = 0; i < 128; i++) _map[i] = NONE;
    }

    void panic() {                                            // everything fades within STEAL_RELEASE_MS
        for (int i = 0; i < STRINGS_POOL; i++) if (_voices[i].live) _retire(i);
        for (int i = 0; i < 128; i++) _map[i] = NONE;
    }

    // =======================================================================
    //  Audio (core 1) — lock-free, no libm, no flash-resident callees
    // =======================================================================
    HOT void tick(int16_t& outL, int16_t& outR) {
        if (_gctr == 0) _globalControl();
        if (++_gctr >= CONTROL_DIV) _gctr = 0;

        uint32_t live = 0;                                    // which voices has core 0 published?
        for (int i = 0; i < STRINGS_POOL; i++) live |= (uint32_t)(_voices[i].live != 0) << i;
        __dmb();                                              // acquire, after the flag reads

        float b16 = 0.0f, b8 = 0.0f, b4 = 0.0f, bpz = 0.0f;
        while (live) {
            const int i = __builtin_ctz(live);
            live &= live - 1;
            _voices[i].render(ep, _modRatio, b16, b8, b4, bpz);
        }

        float lp, bp, hp, mono = 0.0f;
        if (ep.wBass   > 0.0f) { _fBass.tick(b16, lp, bp, hp);   mono += ep.wBass   * lp; }
        if (ep.wCello  > 0.0f) { _fCello.tick(b8, lp, bp, hp);   mono += ep.wCello  * lp; }
        if (ep.wViola  > 0.0f) { _fViola.tick(b8, lp, bp, hp);   mono += ep.wViola  * bp; }
        if (ep.wViolin > 0.0f) { _fViolin.tick(b4, lp, bp, hp);  mono += ep.wViolin * bp; }
        if (ep.wPizz   > 0.0f) { _fPizz.tick(bpz, lp, bp, hp);   mono += ep.wPizz   * bp; }

        _tone += ep.toneA * (mono - _tone);                   // TONE: global low-pass

        float L, R;
        _ens.process(_tone, ep.ensDepth, ep.ensShimmer, ep.ensMix, ep.ensSlowInc, L, R);

        outL = (int16_t)(softSat(L * ep.gain) * 32767.0f);
        outR = (int16_t)(softSat(R * ep.gain) * 32767.0f);
    }

    // =======================================================================
    //  Diagnostics (core 0)
    // =======================================================================
    uint8_t activeCount()  const { uint8_t n = 0; for (int i = 0; i < STRINGS_POOL; i++) if (_voices[i].live) n++; return n; }
    uint8_t countedVoices() const { return (uint8_t)_countCounted(); }
    bool        voiceLive(int i)  const { return _voices[i].live; }
    bool        voiceHeld(int i)  const { return _held[i]; }
    bool        voiceSust(int i)  const { return _sust[i]; }
    bool        voiceDying(int i) const { return _dying[i]; }
    uint8_t     voiceNote(int i)  const { return _note[i]; }
    float       voiceLevel(int i) const { return _voices[i].levelNow(); }
    const char* voiceState(int i) const { static const char* n[] = { "IDLE", "ATK ", "HOLD", "REL " }; return n[_voices[i].st & 3]; }
    float       modRatio()        const { return _modRatio; }

private:
    static constexpr uint8_t NONE = 0xFF;

    StringVoice _voices[STRINGS_POOL];
    bool        _held[STRINGS_POOL];     // key is down
    bool        _sust[STRINGS_POOL];     // key is up but the pedal holds the voice
    bool        _dying[STRINGS_POOL];    // fast release requested
    uint8_t     _note[STRINGS_POOL];
    uint32_t    _age[STRINGS_POOL];
    uint8_t     _map[128];               // note -> slot of the voice whose key is down
    float       _noteInc16[128];
    uint32_t    _clock = 0;
    uint8_t     _patchIdx = 0;
    bool        _pedal = false;

    // core-1 state
    uint8_t     _gctr = 0;
    float       _modRatio = 1.0f;
    uint32_t    _vibPhase = 0;
    Svf         _fBass, _fCello, _fViola, _fViolin, _fPizz;
    Ensemble    _ens;
    float       _tone = 0.0f;

    // ---- core 1: once per control block -----------------------------------
    HOT void _globalControl() {
        _vibPhase += ep.vibInc * (uint32_t)CONTROL_DIV;
        const float lfo  = (float)Wavetable::sine(_vibPhase >> PHASE_SHIFT) * (1.0f / 32767.0f);
        const float semi = ep.modDepth * lfo + ep.bend;
        const float a    = semi * (0.693147181f / 12.0f);     // 2^(semi/12) by a 5th-order series (|semi| <= 4)
        _modRatio = 1.0f + a * (1.0f + a * (0.5f + a * (1.0f / 6.0f + a * (1.0f / 24.0f + a * (1.0f / 120.0f)))));
        if (!_fBass.sane())   _fBass.reset();
        if (!_fCello.sane())  _fCello.reset();
        if (!_fViola.sane())  _fViola.reset();
        if (!_fViolin.sane()) _fViolin.reset();
        if (!_fPizz.sane())   _fPizz.reset();
        if (!(_tone > -1e6f && _tone < 1e6f)) _tone = 0.0f;
    }

    // ---- core 0: allocation ------------------------------------------------
    int _countCounted() const {
        int n = 0;
        for (int i = 0; i < STRINGS_POOL; i++) if (_voices[i].live && !_dying[i]) n++;
        return n;
    }
    int _findFree() const {
        for (int i = 0; i < STRINGS_POOL; i++) if (!_voices[i].live) return i;
        return -1;
    }
    // Quietest voice whose key is up (tail or pedal-sustained); else the oldest held voice.
    int _pickVictim() const {
        int best = -1; float lvl = 2.0f;
        for (int i = 0; i < STRINGS_POOL; i++) {
            if (_voices[i].live && !_dying[i] && !_held[i]) {
                const float l = _voices[i].levelNow();
                if (l < lvl) { lvl = l; best = i; }
            }
        }
        if (best >= 0) return best;
        uint32_t oldest = 0xFFFFFFFFu;
        for (int i = 0; i < STRINGS_POOL; i++)
            if (_voices[i].live && !_dying[i] && _age[i] < oldest) { oldest = _age[i]; best = i; }
        return best;
    }
    void _retire(int v) {                                     // fast release + forget the key
        if (_held[v] && _note[v] < 128 && _map[_note[v]] == v) _map[_note[v]] = NONE;
        _held[v] = false; _sust[v] = false; _dying[v] = true;
        _voices[v].fastReq = (uint8_t)(_voices[v].fastReq + 1);
    }
    int _waitFree() {                                         // pool exhausted (burst of steals): bounded wait
        const uint32_t t0 = micros();
        while ((uint32_t)(micros() - t0) < (uint32_t)(STEAL_RELEASE_MS * 1000 + 4000)) {
            const int s = _findFree();
            if (s >= 0) return s;
            delayMicroseconds(50);
        }
        return -1;
    }
    void _start(int slot, uint8_t note) {
        __dmb();                                              // acquire: core 1's last accesses to this slot are done
        _note[slot] = note; _age[slot] = ++_clock;
        _held[slot] = true; _sust[slot] = false; _dying[slot] = false;
        _map[note] = (uint8_t)slot;
        _voices[slot].start(_noteInc16[note], ep, _modRatio, _clock);
        __dmb();                                              // everything above visible before...
        _voices[slot].live = 1;                               // ...core 1 starts rendering it
    }

    // ---- core 0: knob value -> engine value -------------------------------------
    void _derive(uint8_t id) {
        const float f = pv[id] / 127.0f;
        switch (id) {
            case P_BASS:   ep.wBass   = TRIM_BASS   * f * f; ep.use16 = pv[P_BASS] > 0; break;
            case P_CELLO:  ep.wCello  = TRIM_CELLO  * f * f; ep.use8  = pv[P_CELLO] > 0 || pv[P_VIOLA] > 0; break;
            case P_VIOLA:  ep.wViola  = TRIM_VIOLA  * f * f; ep.use8  = pv[P_CELLO] > 0 || pv[P_VIOLA] > 0; break;
            case P_VIOLIN: ep.wViolin = TRIM_VIOLIN * f * f; ep.use4  = pv[P_VIOLIN] > 0; break;
            case P_PIZZ:   ep.wPizz   = TRIM_PIZZ   * f * f; ep.usePz = pv[P_PIZZ] > 0; break;
            case P_ATTACK:  ep.atkRate = envRate(attackSeconds(pv[id]));  break;
            case P_RELEASE: ep.relRate = envRate(releaseSeconds(pv[id])); break;
            case P_DETUNE:  ep.detuneRatio = powf(2.0f, detuneCents(pv[id]) / 1200.0f); ep.unison = pv[id] > 2; break;
            case P_VIB_DEPTH: ep.modDepth = f * VIB_MAX_SEMITONES; break;
            case P_VIB_RATE:  ep.vibInc = (uint32_t)hzToInc(vibHz(pv[id])); break;
            case P_PIZ_DECAY: ep.pzCoef = expf(-(float)CONTROL_DIV / ((float)SAMPLE_RATE * pizzTau(pv[id]))); break;
            case P_TONE:      ep.toneA = 1.0f - expf(-6.2831853f * toneHz(pv[id]) / (float)SAMPLE_RATE); break;
            case P_ENS_DEPTH:   ep.ensDepth = f; break;
            case P_ENS_RATE:    ep.ensSlowInc = (uint32_t)hzToInc(ensHz(pv[id])); break;
            case P_ENS_MIX:     ep.ensMix = f; break;
            case P_ENS_SHIMMER: ep.ensShimmer = f; break;
            case P_VOL:         ep.gain = OUT_GAIN * f * f; break;
            default: break;
        }
    }
};
