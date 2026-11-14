#pragma once
// ============================================================
//  bass_manager.h  —  the bass synth: mono key handling, patches, 16 x 16-step sequencer, transport
//
//  CORE 0 (loop(): MIDI, USB host, UI, serial)   everything except tick()
//  CORE 1 (loop1(): audio)                       tick(): voice events, voice, SEQUENCER
//
//  The sequencer runs on core 1 and counts SAMPLES, so step times are sample-exact. Single-writer mailboxes (no locks):
//
//    core 0 -> core 1   voice event   evtWord = seq<<24 | velocity<<16 | note<<8 | flags     (ONE aligned 32-bit store)
//                       transport     ctlCmd, barrier, ++ctlSeq        (START / STOP / CONTINUE)
//                       pattern       reqPat, barrier, ++reqPatSeq     (queued to the bar end while playing)
//                       MIDI clock    ++clkSeq per 0xF8                (consumed as a tick count)
//                       panic         ++panicSeq
//    core 1 -> core 0   position      stepNow, patNow, playing, gateNow, noteNow     (single bytes, for the display)
//
//  The voice event is a STATE ("gate on this note, legato or not") rather than an edge, so if two events arrive within
//  one control tick (0.36 ms) only the newest survives and nothing is left inconsistent.
//
//  Pattern memory (stepNote, stepFlg, patLen) is written by core 0 only and read by core 1: independent bytes.
//  Step flags: 1 gate (0 = rest), 2 SLIDE (glide into the next step, gate held, no retrigger), 4 ACCENT.
//  Mono keyboard: last-note priority; releasing the top key returns to the previous held key LEGATO (glide, no retrigger).
//  Glide and the no-retrigger behaviour only happen while a gate is already on (overlapping notes), as on a Moog.
// ============================================================
#include <Arduino.h>
#include <hardware/sync.h>
#include <string.h>
#include "config.h"
#include "bass_tables.h"
#include "bass_dsp.h"
#include "bass_params.h"
#include "bass_voice.h"

#ifdef BASS_TRACE                         // host tests: log every voice event
struct BTrace { uint32_t t; uint8_t kind, note, vel, flags, pat, step; };      // kind: 0 gate off, 1 note on, 2 legato note
#endif
enum : uint8_t { CMD_START = 1, CMD_STOP = 2, CMD_CONT = 3 };
#define STACK_N 8

class BassManager {
public:
    // ---- state read by the UI (core 0) ------------------------------------
    uint8_t  pv[N_PARAMS];
    uint8_t  patch = 0;
    uint8_t  stepNote[N_PATTERNS][N_STEPS];
    uint8_t  stepFlg[N_PATTERNS][N_STEPS];
    uint8_t  patLen[N_PATTERNS];
    uint8_t  selPat = 0, cursor = 0;                   // pattern shown / edited; step cursor (UI + step-write)
    float    bpm = 120.0f;
    volatile bool    extSync = false, stepWrite = false;
    volatile uint8_t keyMode = 1;                      // 0 keys play over the sequence, 1 keys TRANSPOSE it while it plays
    volatile int8_t  transpose = 0;
    volatile uint8_t stepNow = 0, patNow = 0, playing = 0, gateNow = 0, noteNow = 0;    // core 1 publishes
    volatile float   bendSemi = 0.0f;

    // =======================================================================
    void init() {
        loadDemo();
        _step = 0; _patCur = 0; _cnt = 0; _playing = false; _pendingPat = 0xFF; _gctr = 0;
    }
    const char* patchName() const { return PATCHES[patch].name; }

    // factory state: patch 0, four demo patterns, 120 BPM
    void loadDemo() {
        memset(stepNote, 36, sizeof stepNote); memset(stepFlg, 0, sizeof stepFlg);
        for (int p = 0; p < N_PATTERNS; p++) patLen[p] = N_STEPS;
        for (int p = 0; p < N_DEMO; p++) for (int s = 0; s < N_STEPS; s++) parseStep(DEMO_STEPS[p][s], stepNote[p][s], stepFlg[p][s]);
        pv[P_TEMPO] = 60; pv[P_SWING] = 0; bpm = bpmFromKnob(60); _updateTiming(); selPat = 0; cursor = 0; extSync = false; stepWrite = false; keyMode = 1; transpose = 0;
        loadPatch(0);
    }

    void loadPatch(uint8_t i) {                         // everything except tempo and swing
        if (i >= N_PATCHES) return;
        patch = i;
        for (int k = 0; k < N_PARAMS; k++) if (k != P_TEMPO && k != P_SWING) pv[k] = PATCHES[i].v[k];
        _applyAll();
    }

    // =======================================================================
    //  Parameters (core 0)
    // =======================================================================
    void setParam(uint8_t id, uint8_t val) {
        if (id >= N_PARAMS) return;
        if (val > 127) val = 127;
        pv[id] = val;
        if (id == P_TEMPO) { bpm = bpmFromKnob(val); _updateTiming(); }
        else if (id == P_SWING) swing = swingFromKnob(val);
        else if (id == P_GATE) gateFrac = gateFracOf(val);
        else _voice.derive(pv);
    }
    void setBpm(float b) {
        if (b < 30.0f) b = 30.0f;
        if (b > 300.0f) b = 300.0f;
        bpm = b; const float k = b - (float)TEMPO_KNOB_BASE;
        pv[P_TEMPO] = (uint8_t)(k < 0.0f ? 0 : k > 127.0f ? 127 : (int)(k + 0.5f));
        _updateTiming();
    }
    bool handleCC(uint8_t cc, uint8_t val) {
        if (val > 127) val = 127;
        if (cc == 120) { panic(); return true; }
        if (cc == 123) { allNotesOff(); return true; }
        if (cc == 64)  { setPedal(val >= 64); return true; }
        const uint8_t id = paramForCC(cc);
        if (id == 0xFF) return false;
        setParam(id, val);
        return true;
    }
    void pitchBend(int16_t bend) { bendSemi = (float)bend * (BEND_RANGE_SEMI / 8192.0f); }

    // =======================================================================
    //  Keyboard (core 0): mono, last-note priority, legato return
    // =======================================================================
    void noteOn(uint8_t n, uint8_t vel) {
        if (vel == 0) { noteOff(n); return; }
        if (n > 127) return;
        if (stepWrite) _writeNote(n, vel);
        if (keyMode == 1 && playing) { transpose = (int8_t)((int)n - KEY_ROOT); return; }     // keys transpose the running sequence
        _stkRemove(n);
        const bool legato = _stkN > 0;                                                          // another key is still down
        _stkPush(n, vel);
        _post(true, n, vel, legato, vel >= ACCENT_VEL);
        _sounding = n;
    }
    void noteOff(uint8_t n) {
        for (int i = 0; i < _stkN; i++) if (_stk[i].note == n) {
            if (_pedal) { _stk[i].rel = true; return; }
            _stkRemoveAt(i); _resolve(); return;
        }
    }
    void setPedal(bool down) {
        if (down == _pedal) return;
        _pedal = down;
        if (down) { if (stepWrite) _writeRest(); return; }                                      // in step-write the pedal enters a rest
        for (int i = _stkN - 1; i >= 0; i--) if (_stk[i].rel) _stkRemoveAt(i);
        _resolve();
    }
    void allNotesOff() { _stkN = 0; _pedal = false; _sounding = 255; _post(false, 0, 0, false, false); }
    void panic()       { allNotesOff(); panicSeq = (uint8_t)(panicSeq + 1); }
    uint8_t heldKeys() const { return _stkN; }

    // =======================================================================
    //  Transport (core 0)
    // =======================================================================
    void play()     { _pushPat(selPat); _cmd(CMD_START); }
    void stop()     { _cmd(CMD_STOP); }
    void resume()   { _cmd(CMD_CONT); }
    void togglePlay() { if (playing) stop(); else play(); }
    void midiRealtime(uint8_t status) {                 // 0xF8 clock, 0xFA start, 0xFB continue, 0xFC stop
        switch (status) {
            case 0xF8: clkSeq = (uint8_t)(clkSeq + 1); break;
            case 0xFA: play();   break;
            case 0xFB: resume(); break;
            case 0xFC: stop();   break;
            default: break;
        }
    }

    // =======================================================================
    //  Patterns (core 0)
    // =======================================================================
    void setPattern(uint8_t p) { if (p < N_PATTERNS) { selPat = p; if (cursor >= patLen[p]) cursor = 0; _pushPat(p); } }
    void setLength(uint8_t len) { if (len >= 1 && len <= N_STEPS) { patLen[selPat] = len; if (cursor >= len) cursor = (uint8_t)(len - 1); } }
    void setStep(uint8_t step, uint8_t note, uint8_t flags) {
        if (step >= N_STEPS || flags > 7) return;
        stepNote[selPat][step] = (uint8_t)(note < NOTE_MIN ? NOTE_MIN : note > NOTE_MAX ? NOTE_MAX : note);
        stepFlg[selPat][step] = flags;
    }
    void shiftNote(uint8_t step, int delta) {            // UP / DOWN in the editor; on a rest it switches the step on
        if (step >= N_STEPS) return;
        int n = (int)stepNote[selPat][step] + delta;
        stepNote[selPat][step] = (uint8_t)(n < NOTE_MIN ? NOTE_MIN : n > NOTE_MAX ? NOTE_MAX : n);
        stepFlg[selPat][step] |= F_GATE;
    }
    void cycleFlags(uint8_t step) {                      // rest -> note -> accent -> slide -> slide+accent -> rest
        if (step >= N_STEPS) return;
        uint8_t& f = stepFlg[selPat][step];
        switch (f & 7) { case 0: f = F_GATE; break; case F_GATE: f = F_GATE | F_ACCENT; break; case F_GATE | F_ACCENT: f = F_GATE | F_SLIDE; break;
                         case F_GATE | F_SLIDE: f = F_GATE | F_SLIDE | F_ACCENT; break; default: f = 0; break; }
    }
    void clearPattern(uint8_t p) { if (p < N_PATTERNS) { memset(stepNote[p], 36, sizeof stepNote[p]); memset(stepFlg[p], 0, sizeof stepFlg[p]); patLen[p] = N_STEPS; } }
    void copyPattern(uint8_t from, uint8_t to) {
        if (from < N_PATTERNS && to < N_PATTERNS && from != to) { memcpy(stepNote[to], stepNote[from], N_STEPS); memcpy(stepFlg[to], stepFlg[from], N_STEPS); patLen[to] = patLen[from]; }
    }

    // =======================================================================
    //  Storage (core 0): plain bytes + CRC32. See bass_storage.h for the file wrapper.
    // =======================================================================
    static constexpr size_t SAVE_SIZE = 4 + 4 + N_PARAMS + 1 + 4 + 1 + 1 + N_PATTERNS + 2 * N_PATTERNS * N_STEPS + 4;

    size_t serialize(uint8_t* b, size_t cap) const {
        if (cap < SAVE_SIZE) return 0;
        size_t n = 0;
        b[n++] = 'B'; b[n++] = 'A'; b[n++] = 'S'; b[n++] = 1;
        b[n++] = N_PARAMS; b[n++] = N_STEPS; b[n++] = N_PATTERNS; b[n++] = 0;
        memcpy(b + n, pv, N_PARAMS); n += N_PARAMS;
        b[n++] = patch;
        memcpy(b + n, &bpm, 4); n += 4;
        b[n++] = (uint8_t)((extSync ? 1 : 0) | (keyMode ? 2 : 0));
        b[n++] = selPat;
        memcpy(b + n, patLen, sizeof patLen); n += sizeof patLen;
        memcpy(b + n, stepNote, sizeof stepNote); n += sizeof stepNote;
        memcpy(b + n, stepFlg, sizeof stepFlg); n += sizeof stepFlg;
        const uint32_t c = crc32(b, n);
        memcpy(b + n, &c, 4); n += 4;
        return n;
    }
    bool deserialize(const uint8_t* b, size_t len) {       // false (and NOTHING changed) unless the image is valid and complete
        if (len != SAVE_SIZE) return false;
        if (b[0] != 'B' || b[1] != 'A' || b[2] != 'S' || b[3] != 1) return false;
        if (b[4] != N_PARAMS || b[5] != N_STEPS || b[6] != N_PATTERNS) return false;
        uint32_t c; memcpy(&c, b + len - 4, 4);
        if (c != crc32(b, len - 4)) return false;
        size_t n = 8;
        memcpy(pv, b + n, N_PARAMS); n += N_PARAMS;
        const uint8_t pa = b[n++];
        float nb; memcpy(&nb, b + n, 4); n += 4;
        const uint8_t fl = b[n++], sp = b[n++];
        memcpy(patLen, b + n, sizeof patLen); n += sizeof patLen;
        memcpy(stepNote, b + n, sizeof stepNote); n += sizeof stepNote;
        memcpy(stepFlg, b + n, sizeof stepFlg);
        for (int k = 0; k < N_PARAMS; k++) if (pv[k] > 127) pv[k] = 127;
        for (int p = 0; p < N_PATTERNS; p++) {
            if (patLen[p] < 1 || patLen[p] > N_STEPS) patLen[p] = N_STEPS;
            for (int s = 0; s < N_STEPS; s++) {
                if (stepNote[p][s] < NOTE_MIN) stepNote[p][s] = NOTE_MIN; else if (stepNote[p][s] > NOTE_MAX) stepNote[p][s] = NOTE_MAX;
                stepFlg[p][s] &= 7;
            }
        }
        patch = pa < N_PATCHES ? pa : 0; extSync = fl & 1; keyMode = (fl & 2) ? 1 : 0; selPat = sp < N_PATTERNS ? sp : 0; cursor = 0;
        _applyAll();
        setBpm(nb);
        return true;
    }
    static uint32_t crc32(const uint8_t* d, size_t n) {
        uint32_t c = 0xFFFFFFFFu;
        for (size_t i = 0; i < n; i++) { c ^= d[i]; for (int k = 0; k < 8; k++) c = (c >> 1) ^ (0xEDB88320u & (uint32_t)(-(int32_t)(c & 1u))); }
        return ~c;
    }

    // =======================================================================
    //  Audio (core 1)
    // =======================================================================
    HOT void tick(int16_t& outL, int16_t& outR) {
        if (_gctr == 0) _control();
        if (++_gctr >= CONTROL_DIV) _gctr = 0;
        if (_playing && !extSync) {                         // sample-exact internal clock
            _cnt -= 1.0f;
            if (_cnt <= 0.0f) _fireAndAdvance();
        }
        if (_gateCnt != 0 && --_gateCnt == 0) _gateOff();   // sequencer gate length, sample-exact
        const float y = _voice.active() ? _voice.render() : 0.0f;
        const int16_t o = (int16_t)(softSat(y) * 32767.0f);
        outL = o; outR = o;
        _t++;
    }

    bool voiceActive() const { return _voice.active(); }
    float voicePitch() const { return _voice.pitch; }

    // sequencer inputs written by core 0, read by core 1 (single words)
    volatile float stepLen = SAMPLE_RATE * 15.0f / 120.0f;    // samples per 16th note
    volatile float swing = 0.5f;
    volatile float gateFrac = 0.7f;
#ifdef BASS_TRACE
    BTrace   trace[8192];
    uint32_t traceN = 0;
#endif

private:
    BassVoice _voice;
    // core 0 -> core 1 mailboxes
    volatile uint32_t evtWord = 0;
    volatile uint8_t  ctlCmd = 0, ctlSeq = 0, reqPat = 0, reqPatSeq = 0, clkSeq = 0, panicSeq = 0;
    uint8_t  _evtSeq = 0;
    // core-0 key stack
    struct Key { uint8_t note, vel; bool rel; };
    Key      _stk[STACK_N];
    uint8_t  _stkN = 0, _sounding = 255;
    bool     _pedal = false;
    // core-1 state
    uint8_t  _evtSeen = 0, _ctlSeen = 0, _reqPatSeen = 0, _clkSeen = 0, _panicSeen = 0, _gctr = 0;
    bool     _playing = false, _prevSlide = false, _firstTick = false, _haveTick = false;
    uint8_t  _step = 0, _patCur = 0, _pendingPat = 0xFF, _tickPhase = 0;
    float    _cnt = 0.0f, _curDur = 0.0f, _tickInt = 0.0f;
    uint32_t _gateCnt = 0, _t = 0, _lastTickT = 0;

    void _applyAll() {                                  // NOT the tempo: a patch must leave a fractional BPM alone (deserialize and loadDemo set it)
        _voice.derive(pv);
        swing = swingFromKnob(pv[P_SWING]); gateFrac = gateFracOf(pv[P_GATE]);
    }
    void _updateTiming() { stepLen = (float)SAMPLE_RATE * 15.0f / bpm; }
    void _cmd(uint8_t c) { ctlCmd = c; __dmb(); ctlSeq = (uint8_t)(ctlSeq + 1); }
    void _pushPat(uint8_t p) { reqPat = p; __dmb(); reqPatSeq = (uint8_t)(reqPatSeq + 1); }
    void _post(bool gate, uint8_t note, uint8_t vel, bool legato, bool accent) {
        _evtSeq = (uint8_t)(_evtSeq + 1);
        const uint32_t fl = (gate ? 1u : 0u) | (legato ? 2u : 0u) | (accent ? 4u : 0u);
        evtWord = ((uint32_t)_evtSeq << 24) | ((uint32_t)(vel & 0x7F) << 16) | ((uint32_t)(note & 0x7F) << 8) | fl;
    }
    // ---- key stack ----
    void _stkRemoveAt(int i) { for (int k = i; k < _stkN - 1; k++) _stk[k] = _stk[k + 1]; _stkN--; }
    void _stkRemove(uint8_t n) { for (int i = 0; i < _stkN; i++) if (_stk[i].note == n) { _stkRemoveAt(i); return; } }
    void _stkPush(uint8_t n, uint8_t v) { if (_stkN == STACK_N) _stkRemoveAt(0); _stk[_stkN++] = { n, v, false }; }
    void _resolve() {                                    // after a key left the stack: what should sound now?
        if (_stkN == 0) { _sounding = 255; _post(false, 0, 0, false, false); return; }
        const Key& top = _stk[_stkN - 1];
        if (top.note != _sounding) { _sounding = top.note; _post(true, top.note, top.vel, true, top.vel >= ACCENT_VEL); }   // back to the previous key: legato
    }
    void _writeNote(uint8_t n, uint8_t vel) {            // step-write: the keyboard enters notes at the cursor
        const uint8_t len = patLen[selPat];
        stepNote[selPat][cursor] = (uint8_t)(n < NOTE_MIN ? NOTE_MIN : n > NOTE_MAX ? NOTE_MAX : n);
        stepFlg[selPat][cursor] = (uint8_t)(F_GATE | (vel >= ACCENT_VEL ? F_ACCENT : 0));
        cursor = (uint8_t)((cursor + 1) % len);
    }
    void _writeRest() { stepFlg[selPat][cursor] = 0; cursor = (uint8_t)((cursor + 1) % patLen[selPat]); }

    // ---- core 1 ---------------------------------------------------------------
    HOT void _voiceOn(uint8_t note, uint8_t vel, bool legato, bool accent) {
#ifdef BASS_TRACE
        const bool lg = legato && _voice.gate;
#endif
        _voice.noteOn((float)note, (float)vel * (1.0f / 127.0f), legato, accent);
        gateNow = 1; noteNow = note;
#ifdef BASS_TRACE
        if (traceN < 8192) { trace[traceN] = { _t, (uint8_t)(lg ? 2 : 1), note, vel, (uint8_t)(accent ? 1 : 0), _patCur, stepNow }; traceN++; }
#endif
    }
    HOT void _gateOff() {
        if (!_voice.gate) return;
        _voice.gateOff(); gateNow = 0; _gateCnt = 0;
#ifdef BASS_TRACE
        if (traceN < 8192) { trace[traceN] = { _t, 0, noteNow, 0, 0, _patCur, stepNow }; traceN++; }
#endif
    }

    HOT void _control() {
        const uint32_t w = evtWord;                         // keyboard event
        if ((uint8_t)(w >> 24) != _evtSeen) {
            _evtSeen = (uint8_t)(w >> 24);
            const uint8_t fl = (uint8_t)w;
            if (fl & 1) { _gateCnt = 0; _voiceOn((uint8_t)(w >> 8) & 0x7F, (uint8_t)(w >> 16) & 0x7F, (fl & 2) != 0, (fl & 4) != 0); }
            else _gateOff();
        }
        const uint8_t rs = reqPatSeq;
        if (rs != _reqPatSeen) {
            __dmb(); _reqPatSeen = rs;
            const uint8_t rp = reqPat < N_PATTERNS ? reqPat : 0;
            if (!_playing) { _patCur = rp; patNow = rp; } else _pendingPat = rp;
        }
        const uint8_t cs = ctlSeq;
        if (cs != _ctlSeen) {
            __dmb(); _ctlSeen = cs;
            switch (ctlCmd) {
                case CMD_START: _playing = true; playing = 1; _step = 0; _pendingPat = 0xFF; patNow = _patCur; _cnt = 0.0f; _tickPhase = 0; _firstTick = true; _prevSlide = false; _tickInt = 0.0f; _haveTick = false; break;
                case CMD_STOP:  _playing = false; playing = 0; _prevSlide = false; _gateOff(); break;           // the held note must not hang
                case CMD_CONT:  _playing = true; playing = 1; _cnt = 0.0f; _tickPhase = 0; _firstTick = true; _haveTick = false; break;
                default: break;
            }
        }
        const uint8_t ps = panicSeq;
        if (ps != _panicSeen) { _panicSeen = ps; _voice.panic(); gateNow = 0; _gateCnt = 0; }
        uint8_t n = (uint8_t)(clkSeq - _clkSeen);
        _clkSeen = clkSeq;
        while (n--) _clockTick();
        _voice.control(bendSemi);
    }

    HOT void _fireStep(uint8_t s, float dur) {
        const uint8_t fl = stepFlg[_patCur][s];
        stepNow = s; patNow = _patCur;
        if (fl & F_GATE) {
            int n = (int)stepNote[_patCur][s] + transpose;
            if (n < 0) n = 0;
            if (n > 127) n = 127;
            _voiceOn((uint8_t)n, (fl & F_ACCENT) ? VEL_ACCENT : VEL_NORMAL, _prevSlide, (fl & F_ACCENT) != 0);
            if (fl & F_SLIDE) _gateCnt = 0;                                   // slide: hold the gate through to the next step
            else { float g = dur * gateFrac; if (g < 1.0f) g = 1.0f; _gateCnt = (uint32_t)g; }
        } else _gateOff();
        _prevSlide = (fl & F_GATE) && (fl & F_SLIDE);
    }
    HOT void _advance() {
        uint8_t len = patLen[_patCur];
        if (len < 1 || len > N_STEPS) len = N_STEPS;
        _step++;
        if (_step >= len) { _step = 0; if (_pendingPat != 0xFF) { _patCur = _pendingPat; _pendingPat = 0xFF; } }     // pattern change on the bar line
    }
    HOT void _fireAndAdvance() {                                              // internal clock
        const uint8_t cur = _step;
        const float L = stepLen, sw = swing;
        const float dur = (cur & 1) ? 2.0f * L * (1.0f - sw) : 2.0f * L * sw;
        _curDur = dur;
        _fireStep(cur, dur);
        _cnt += dur;
        _advance();
    }
    HOT void _clockTick() {                                                   // external clock: 24 ppqn, one step per 6 ticks
        if (!_playing || !extSync) return;
        if (_haveTick) {                                                       // smoothed tick interval -> gate length under external clock
            const uint32_t iv = _t - _lastTickT;
            if (iv > 0 && iv < (uint32_t)SAMPLE_RATE) _tickInt = (_tickInt == 0.0f) ? (float)iv : _tickInt * 0.8f + (float)iv * 0.2f;
        }
        _lastTickT = _t; _haveTick = true;
        _tickPhase++;
        if (_firstTick || _tickPhase >= 6) {
            _firstTick = false; _tickPhase = 0;
            _fireStep(_step, _tickInt > 0.0f ? 6.0f * _tickInt : stepLen);
            _advance();
        }
    }
};
