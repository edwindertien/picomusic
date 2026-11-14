#pragma once
// ============================================================
//  drum_manager.h  —  the drum machine: voices, sequencer, patterns, song, transport
//
//  CORE 0 (loop(): MIDI, USB host, UI, serial)   everything except tick()
//  CORE 1 (loop1(): audio)                       tick(): trigger pick-up, voices, SEQUENCER
//
//  The sequencer runs on core 1 and counts SAMPLES, so step times are sample-exact and independent of
//  the MIDI polling jitter on core 0. Single-writer mailboxes in both directions (no locks):
//
//    core 0 -> core 1   pad hit      voice.trigVel, barrier, ++voice.trigSeq     (one per instrument)
//                       transport    ctlCmd, barrier, ++ctlSeq                    (START / STOP / CONTINUE)
//                       pattern      reqPat, barrier, ++reqPatSeq                 (queued to the bar end while playing)
//                       MIDI clock   ++clkSeq per 0xF8                            (consumed as a tick count)
//                       panic        ++panicSeq
//    core 1 -> core 0   recorded     RecEvt ring (producer core 1, consumer core 0)
//                       position     stepNow, patNow, songPosNow, playing         (single bytes, for the display)
//
//  Pattern memory (pat, patLen, song) is written by core 0 only and read by core 1: every cell is an
//  independent byte, so a read can at worst see an edit one step early or late.
//
//  Step levels: 0 off, 1 ghost, 2 normal, 3 accent  ->  velocity 55 / 100 / 127.
//  Swing: even steps last 2*L*s, odd steps 2*L*(1-s) (s = 0.5 straight .. 0.75), L = one 16th note.
// ============================================================
#include <Arduino.h>
#include <hardware/sync.h>
#include <string.h>
#include "config.h"
#include "wavetable.h"
#include "drum_dsp.h"
#include "drum_params.h"
#include "drum_voices.h"

#ifdef DRUM_TRACE                         // host tests: log every voice start (sample, instrument, velocity, pattern, step)
struct TraceEv { uint32_t t; uint8_t inst, vel, pat, step; };
#endif

struct RecEvt { uint8_t pat, step, inst, vel; };
#define REC_RING 32
enum : uint8_t { CMD_START = 1, CMD_STOP = 2, CMD_CONT = 3 };

class DrumManager {
public:
    // ---- state read by the UI (core 0) ------------------------------------
    uint8_t  pv[N_PARAMS];
    uint8_t  kit[N_INST][N_INST_PARAMS];
    uint8_t  sel = 0;                                  // selected instrument (the knobs edit this one)
    uint8_t  pat[N_PATTERNS][N_INST][N_STEPS];
    uint8_t  patLen[N_PATTERNS];
    uint8_t  song[SONG_MAX];                           // pattern index, or 0xFF = end / empty
    uint8_t  selPat = 0;                               // pattern the UI edits; the next START plays it
    uint8_t  padBase = PAD_BASE_NOTE;
    bool     padGm = false;
    float    bpm = 120.0f;
    volatile bool    songMode = false, extSync = false, recording = false;
    volatile uint8_t stepNow = 0, patNow = 0, songPosNow = 0, playing = 0;     // core 1 publishes

    // =======================================================================
    //  init / defaults
    // =======================================================================
    void init() {
        loadDemo();
        for (int i = 0; i < N_INST; i++) { _v[i].live = false; _v[i].type = INST_TYPE[i]; }
        _gctr = 0; _step = 0; _patCur = 0; _songPos = 0; _cnt = 0; _playing = false; _pendingPat = 0xFF; _seed = 1;
    }

    // factory state: default kit, four demo patterns, a demo song, 120 BPM
    void loadDemo() {
        memset(pat, 0, sizeof pat);
        for (int p = 0; p < N_PATTERNS; p++) patLen[p] = N_STEPS;
        for (int p = 0; p < 4; p++)
            for (const DemoTrack* t = DEMO_PATTERNS[p]; t->inst != 0xFF; t++)
                for (int s = 0; s < N_STEPS && t->steps[s]; s++) pat[p][t->inst][s] = (t->steps[s] >= '1' && t->steps[s] <= '3') ? (uint8_t)(t->steps[s] - '0') : 0;
        for (int i = 0; i < SONG_MAX; i++) song[i] = 0xFF;
        for (unsigned i = 0; i < sizeof DEMO_SONG; i++) song[i] = DEMO_SONG[i];
        memcpy(kit, KIT, sizeof kit);
        pv[P_TEMPO] = DEFAULT_TEMPO_KNOB; pv[P_SWING] = DEFAULT_SWING_KNOB; pv[P_VOL] = DEFAULT_VOL_KNOB; pv[P_DRIVE] = DEFAULT_DRIVE_KNOB;
        selPat = 0; songMode = false; extSync = false; padGm = false; padBase = PAD_BASE_NOTE; sel = 0;
        _applyAll();
    }

    // =======================================================================
    //  Parameters (core 0)
    // =======================================================================
    void selectInst(uint8_t i) {
        if (i >= N_INST) return;
        sel = i; pv[P_INST] = (uint8_t)(i * 8);
        for (int k = 0; k < N_INST_PARAMS; k++) pv[P_LEVEL + k] = kit[i][k];
    }

    void setParam(uint8_t id, uint8_t val) {
        if (id >= N_PARAMS) return;
        if (val > 127) val = 127;
        switch (id) {
            case P_INST:  selectInst(val >> 3); break;
            case P_LEVEL: case P_TUNE: case P_DECAY: case P_TONE: case P_PAN:
                kit[sel][id - P_LEVEL] = val; pv[id] = val; _v[sel].derive(sel, kit[sel]); break;
            case P_TEMPO: pv[id] = val; bpm = bpmFromKnob(val); _updateTiming(); break;
            case P_SWING: pv[id] = val; swing = swingFromKnob(val); break;
            case P_VOL:   pv[id] = val; volGain = (val / 127.0f) * (val / 127.0f); break;
            case P_DRIVE: pv[id] = val; drive = driveGain(val); break;
            default: break;
        }
    }

    void setBpm(float b) {                                  // serial: 30..300 (the knob covers 60..187)
        if (b < 30.0f) b = 30.0f;
        if (b > 300.0f) b = 300.0f;
        bpm = b;
        const float k = b - (float)TEMPO_KNOB_BASE;
        pv[P_TEMPO] = (uint8_t)(k < 0.0f ? 0 : k > 127.0f ? 127 : (int)(k + 0.5f));
        _updateTiming();
    }

    bool handleCC(uint8_t cc, uint8_t val) {
        if (val > 127) val = 127;
        if (cc == CC_ALL_SOUND_OFF) { panic(); return true; }
        if (cc == CC_ALL_NOTES_OFF) return true;
        const uint8_t id = paramForCC(cc);
        if (id == 0xFF) return false;
        setParam(id, val);
        return true;
    }

    // =======================================================================
    //  Pads and triggers (core 0)
    // =======================================================================
    void hit(uint8_t inst, uint8_t vel) {
        if (inst >= N_INST || vel == 0) return;
        _v[inst].trigVel = vel > 127 ? 127 : vel;
        __dmb();
        _v[inst].trigSeq = (uint8_t)(_v[inst].trigSeq + 1);
    }
    int padHit(uint8_t note, uint8_t vel) {                 // returns the instrument, or -1 if the note is not a pad
        const int i = instForNote(note, padGm, padBase);
        if (i < 0) return -1;
        hit((uint8_t)i, vel);
        selectInst((uint8_t)i);
        return i;
    }
    void panic() { panicSeq = (uint8_t)(panicSeq + 1); }

    // =======================================================================
    //  Transport (core 0)
    // =======================================================================
    void play()     { _pushPat(selPat); _cmd(CMD_START); }
    void stop()     { _cmd(CMD_STOP); }
    void resume()   { _cmd(CMD_CONT); }
    void togglePlay() { if (playing) stop(); else play(); }
    void setRecord(bool on) { recording = on; }
    void midiRealtime(uint8_t status) {                     // 0xF8 clock, 0xFA start, 0xFB continue, 0xFC stop
        switch (status) {
            case 0xF8: clkSeq = (uint8_t)(clkSeq + 1); break;
            case 0xFA: play();   break;
            case 0xFB: resume(); break;
            case 0xFC: stop();   break;
            default: break;
        }
    }

    // =======================================================================
    //  Patterns and song (core 0)
    // =======================================================================
    void setPattern(uint8_t p) { if (p < N_PATTERNS) { selPat = p; _pushPat(p); } }    // while playing: takes effect at the bar end
    void setLength(uint8_t len) { if (len >= 1 && len <= N_STEPS) patLen[selPat] = len; }
    uint8_t getStep(uint8_t inst, uint8_t step) const { return (inst < N_INST && step < N_STEPS) ? pat[selPat][inst][step] : 0; }
    void setStep(uint8_t inst, uint8_t step, uint8_t level) { if (inst < N_INST && step < N_STEPS && level <= 3) pat[selPat][inst][step] = level; }
    void cycleStep(uint8_t inst, uint8_t step) {             // off -> normal -> accent -> ghost -> off
        static const uint8_t next[4] = { 2, 0, 3, 1 };          // indexed by the CURRENT level: off->normal, ghost->off, normal->accent, accent->ghost
        setStep(inst, step, next[getStep(inst, step) & 3]);
    }
    void clearPattern(uint8_t p) { if (p < N_PATTERNS) { memset(pat[p], 0, sizeof pat[p]); patLen[p] = N_STEPS; } }
    void copyPattern(uint8_t from, uint8_t to) { if (from < N_PATTERNS && to < N_PATTERNS && from != to) { memcpy(pat[to], pat[from], sizeof pat[to]); patLen[to] = patLen[from]; } }
    void setSongSlot(uint8_t slot, uint8_t p) { if (slot < SONG_MAX) song[slot] = (p < N_PATTERNS) ? p : 0xFF; }
    uint8_t songLength() const { uint8_t n = 0; while (n < SONG_MAX && song[n] != 0xFF) n++; return n; }

    // apply what the audio core recorded (call from loop())
    void service() {
        while (_rr != _rw) {
            __dmb();
            const RecEvt e = _ring[_rr];
            _rr = (uint8_t)((_rr + 1) % REC_RING);
            if (e.pat < N_PATTERNS && e.inst < N_INST && e.step < N_STEPS) pat[e.pat][e.inst][e.step] = velToLevel(e.vel);
        }
    }

    // =======================================================================
    //  Storage (core 0): plain bytes + CRC32. See drum_storage.h for the file wrapper.
    // =======================================================================
    static constexpr size_t SAVE_SIZE = 4 + 5 + N_INST * N_INST_PARAMS + 4 + 4 + 2 + N_PATTERNS + N_PATTERNS * N_INST * N_STEPS + SONG_MAX + 4;

    size_t serialize(uint8_t* b, size_t cap) const {
        if (cap < SAVE_SIZE) return 0;
        size_t n = 0;
        b[n++] = 'D'; b[n++] = 'R'; b[n++] = 'M'; b[n++] = 1;
        b[n++] = N_INST; b[n++] = N_STEPS; b[n++] = N_PATTERNS; b[n++] = SONG_MAX; b[n++] = N_INST_PARAMS;
        memcpy(b + n, kit, sizeof kit); n += sizeof kit;
        memcpy(b + n, &bpm, 4); n += 4;
        b[n++] = pv[P_SWING]; b[n++] = pv[P_VOL]; b[n++] = pv[P_DRIVE]; b[n++] = (uint8_t)((songMode ? 1 : 0) | (extSync ? 2 : 0) | (padGm ? 4 : 0));
        b[n++] = padBase; b[n++] = selPat;
        memcpy(b + n, patLen, sizeof patLen); n += sizeof patLen;
        memcpy(b + n, pat, sizeof pat); n += sizeof pat;
        memcpy(b + n, song, sizeof song); n += sizeof song;
        const uint32_t c = crc32(b, n);
        memcpy(b + n, &c, 4); n += 4;
        return n;
    }

    // returns false (and changes NOTHING) if the data is not a valid, complete, matching save
    bool deserialize(const uint8_t* b, size_t len) {
        if (len != SAVE_SIZE) return false;
        if (b[0] != 'D' || b[1] != 'R' || b[2] != 'M' || b[3] != 1) return false;
        if (b[4] != N_INST || b[5] != N_STEPS || b[6] != N_PATTERNS || b[7] != SONG_MAX || b[8] != N_INST_PARAMS) return false;
        uint32_t c; memcpy(&c, b + len - 4, 4);
        if (c != crc32(b, len - 4)) return false;
        size_t n = 9;
        memcpy(kit, b + n, sizeof kit); n += sizeof kit;
        float nb; memcpy(&nb, b + n, 4); n += 4;
        const uint8_t sw = b[n++], vo = b[n++], dr = b[n++], fl = b[n++], pb = b[n++], sp = b[n++];
        memcpy(patLen, b + n, sizeof patLen); n += sizeof patLen;
        memcpy(pat, b + n, sizeof pat); n += sizeof pat;
        memcpy(song, b + n, sizeof song);
        for (int p = 0; p < N_PATTERNS; p++) if (patLen[p] < 1 || patLen[p] > N_STEPS) patLen[p] = N_STEPS;
        for (int i = 0; i < SONG_MAX; i++) if (song[i] >= N_PATTERNS) song[i] = 0xFF;
        for (int p = 0; p < N_PATTERNS; p++) for (int i = 0; i < N_INST; i++) for (int s = 0; s < N_STEPS; s++) if (pat[p][i][s] > 3) pat[p][i][s] = 0;
        for (int i = 0; i < N_INST; i++) for (int k = 0; k < N_INST_PARAMS; k++) if (kit[i][k] > 127) kit[i][k] = 127;
        songMode = fl & 1; extSync = (fl & 2) != 0; padGm = (fl & 4) != 0; padBase = pb; selPat = sp < N_PATTERNS ? sp : 0;
        pv[P_SWING] = sw > 127 ? 127 : sw; pv[P_VOL] = vo > 127 ? 127 : vo; pv[P_DRIVE] = dr > 127 ? 127 : dr;
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
        float mL = 0.0f, mR = 0.0f;
        for (int i = 0; i < N_INST; i++) {
            DrumVoice& v = _v[i];
            if (v.live) { const float o = v.render(); mL += o * v.p.gL; mR += o * v.p.gR; }
        }
        const float dg = drive * OUT_GAIN;
        outL = (int16_t)(softSat(mL * dg) * volGain * 32767.0f);
        outR = (int16_t)(softSat(mR * dg) * volGain * 32767.0f);
#ifdef DRUM_TRACE
        _sampleCtr++;
#endif
    }

    // =======================================================================
    //  Diagnostics (core 0)
    // =======================================================================
    uint8_t liveCount() const { uint8_t n = 0; for (int i = 0; i < N_INST; i++) if (_v[i].live) n++; return n; }
    bool    voiceLive(int i) const { return _v[i].live; }
#ifdef DRUM_TRACE
    TraceEv  trace[8192];
    uint32_t traceN = 0;
    uint32_t _sampleCtr = 0;
#endif

    // sequencer inputs written by core 0, read by core 1 (single words)
    volatile float stepLen = SAMPLE_RATE * 15.0f / 120.0f;    // samples per 16th note
    volatile float swing = 0.5f;
    volatile float volGain = 0.6f, drive = 1.0f;

private:
    DrumVoice _v[N_INST];

    // core 0 -> core 1 mailboxes
    volatile uint8_t ctlCmd = 0, ctlSeq = 0, reqPat = 0, reqPatSeq = 0, clkSeq = 0, panicSeq = 0;
    // core 1 -> core 0 ring
    RecEvt           _ring[REC_RING];
    volatile uint8_t _rw = 0, _rr = 0;

    // core-1 state
    uint8_t  _ctlSeen = 0, _reqPatSeen = 0, _clkSeen = 0, _panicSeen = 0, _gctr = 0;
    bool     _playing = false;
    uint8_t  _step = 0, _patCur = 0, _songPos = 0, _pendingPat = 0xFF, _tickPhase = 0;
    bool     _firstTick = false;
    float    _cnt = 0.0f, _curDur = 0.0f;
    uint32_t _seed = 1;

    void _cmd(uint8_t c) { ctlCmd = c; __dmb(); ctlSeq = (uint8_t)(ctlSeq + 1); }
    void _pushPat(uint8_t p) { reqPat = p; __dmb(); reqPatSeq = (uint8_t)(reqPatSeq + 1); }
    void _updateTiming() { stepLen = (float)SAMPLE_RATE * 15.0f / bpm; }
    void _applyAll() {
        for (int i = 0; i < N_INST; i++) _v[i].derive(i, kit[i]);
        selectInst(sel);
        setParam(P_SWING, pv[P_SWING]); setParam(P_VOL, pv[P_VOL]); setParam(P_DRIVE, pv[P_DRIVE]);
        bpm = bpmFromKnob(pv[P_TEMPO]); _updateTiming();
    }

    // ---- core 1 ---------------------------------------------------------------
    HOT void _control() {
        // pattern request first, so a START in the same tick plays the pattern that was selected
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
                case CMD_START:
                    _playing = true; playing = 1; _step = 0; _songPos = 0; _pendingPat = 0xFF;
                    if (songMode && song[0] < N_PATTERNS) _patCur = song[0];
                    songPosNow = 0; patNow = _patCur; _cnt = 0.0f; _tickPhase = 0; _firstTick = true;
                    break;
                case CMD_STOP:  _playing = false; playing = 0; break;          // position is kept: CONTINUE resumes, START restarts
                case CMD_CONT:  _playing = true;  playing = 1; _cnt = 0.0f; _tickPhase = 0; _firstTick = true; break;
                default: break;
            }
        }
        const uint8_t ps = panicSeq;
        if (ps != _panicSeen) { _panicSeen = ps; for (int i = 0; i < N_INST; i++) _v[i].choked = true; }
        uint8_t n = (uint8_t)(clkSeq - _clkSeen);
        _clkSeen = clkSeq;
        while (n--) _clockTick();

        for (int i = 0; i < N_INST; i++) {                     // pad hits
            DrumVoice& v = _v[i];
            const uint8_t ts = v.trigSeq;
            if (ts != v.trigSeen) {
                __dmb(); v.trigSeen = ts;
                const uint8_t vel = v.trigVel;
                _startVoice(i, vel);
                if (recording && _playing) _recordHit(i, vel);
            }
        }
        for (int i = 0; i < N_INST; i++) if (_v[i].live) _v[i].control();
    }

    HOT void _startVoice(int i, uint8_t vel) {
        _v[i].trigger(i, (float)vel * (1.0f / 127.0f), ++_seed);
        if (i == I_CH && _v[I_OH].live) _v[I_OH].choked = true;     // the closed hat chokes the open hat
#ifdef DRUM_TRACE
        if (traceN < 8192) { trace[traceN].t = _sampleCtr; trace[traceN].inst = (uint8_t)i; trace[traceN].vel = vel; trace[traceN].pat = _patCur; trace[traceN].step = stepNow; traceN++; }
#endif
    }

    HOT void _fireStep(uint8_t s) {
        for (int i = 0; i < N_INST; i++) {
            const uint8_t lv = pat[_patCur][i][s];
            if (lv) _startVoice(i, levelToVel(lv));
        }
        stepNow = s; patNow = _patCur;
    }

    HOT void _advance() {
        uint8_t len = patLen[_patCur];
        if (len < 1 || len > N_STEPS) len = N_STEPS;
        _step++;
        if (_step >= len) {
            _step = 0;
            if (songMode) {                                          // follow the song chain
                uint8_t np = (uint8_t)(_songPos + 1);
                if (np >= SONG_MAX || song[np] >= N_PATTERNS) np = 0;
                if (song[np] < N_PATTERNS) { _songPos = np; _patCur = song[np]; }
                songPosNow = _songPos;
            } else if (_pendingPat != 0xFF) { _patCur = _pendingPat; _pendingPat = 0xFF; }
        }
    }

    HOT void _fireAndAdvance() {                                     // internal clock
        const uint8_t cur = _step;
        _fireStep(cur);
        const float L = stepLen, sw = swing;
        const float dur = (cur & 1) ? 2.0f * L * (1.0f - sw) : 2.0f * L * sw;
        _curDur = dur;
        _cnt += dur;
        _advance();
    }

    HOT void _clockTick() {                                          // external clock: 24 ppqn, one step per 6 ticks
        if (!_playing || !extSync) return;
        _tickPhase++;
        if (_firstTick || _tickPhase >= 6) {
            _firstTick = false; _tickPhase = 0;
            _fireStep(_step);
            _advance();
        }
    }

    HOT void _recordHit(int inst, uint8_t vel) {                     // quantise to the nearest step
        uint8_t len = patLen[_patCur];
        if (len < 1 || len > N_STEPS) len = N_STEPS;
        float frac;
        if (extSync) frac = (float)_tickPhase * (1.0f / 6.0f);
        else         frac = (_curDur > 0.0f) ? (_curDur - _cnt) / _curDur : 0.0f;
        uint8_t target = stepNow;
        if (frac > REC_QUANT_NEXT) target = (uint8_t)((stepNow + 1) % len);
        const uint8_t w = _rw, nw = (uint8_t)((w + 1) % REC_RING);
        if (nw == _rr) return;                                       // ring full: drop
        _ring[w].pat = _patCur; _ring[w].step = target; _ring[w].inst = (uint8_t)inst; _ring[w].vel = vel;
        __dmb();
        _rw = nw;
    }
};
