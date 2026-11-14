// ============================================================
//  main.cpp  —  Drum Machine  (same architecture as the analog synth, the tonewheel organ and the string machine)
//
//  Core 0: MIDI (USB device + USB host + UART), LCD, buttons, serial. loop() spins flat out (no delay()).
//  Core 1: audio AND the sequencer. loop1() lives in SRAM (__not_in_flash_func); every hot function it calls is
//          force-inlined (HOT). The sequencer counts samples, so step times do not depend on core 0.
//
//  Serial (115200): type  help
// ============================================================
#include <Arduino.h>
#include "config.h"
#include "wavetable.h"
#include "drum_manager.h"
#include "drum_storage.h"
#include "drum_hex.h"
#include "audio_driver.h"
#if ENABLE_LCD
#  include "lcd.h"
#endif
#include "midi_handler.h"
#if ENABLE_USB_HOST
#  include "usb_host.h"
#endif

static_assert(AUDIO_BOARD_REV2_1 == 0 || AUDIO_BOARD_REV2_1 == 1, "AUDIO_BOARD_REV2_1 must be 0 or 1 (see config.h)");

DrumManager machine;
volatile uint8_t g_cpu_load_pct = 0;     // written by core 1, read by core 0 (uint8 write is atomic)

#if ENABLE_SERIAL
static void handleSerial();
#endif

// ============================================================
//  MIDI event log: callbacks only enqueue a tiny record, loop() prints it (a callback must never wait on USB-CDC)
// ============================================================
enum : uint8_t { L_PAD, L_CC, L_PC };
struct LogRec { uint8_t kind, a, b; int16_t v; };
#define LOG_N 64                                  // power of two
static LogRec           _log[LOG_N];
static volatile uint8_t _lw = 0, _lr = 0;
static uint8_t          g_logMask = 1;            // bit0 notes/pads, bit1 CC, bit2 program change  (serial: log ...)

static void log_push(uint8_t kind, uint8_t a, uint8_t b, int16_t v = 0) {
#if ENABLE_SERIAL
    const uint8_t bit = (kind == L_PAD) ? 1 : (kind == L_CC) ? 2 : 4;
    if (!(g_logMask & bit)) return;
    const uint8_t n = (uint8_t)((_lw + 1) & (LOG_N - 1));
    if (n == _lr) return;                         // full: drop
    _log[_lw] = { kind, a, b, v };
    _lw = n;
#else
    (void)kind; (void)a; (void)b; (void)v;
#endif
}

static void log_flush() {
#if ENABLE_SERIAL
    for (int i = 0; i < 6 && _lr != _lw; i++) {   // at most 6 lines per loop() pass
        const LogRec& r = _log[_lr];
        switch (r.kind) {
            case L_PAD: if (r.v >= 0) Serial.printf("NOTE %3u v%3u -> %s (%s)\n", r.a, r.b, INST_LABEL[r.v], INST_NAME[r.v]); else Serial.printf("NOTE %3u v%3u   (not a pad: see 'pads' / 'padbase')\n", r.a, r.b); break;
            case L_CC:  Serial.printf("CC  %3u = %3u%s\n", r.a, r.b, r.v ? "" : "  (unused)"); break;
            case L_PC:  Serial.printf("PC  %u -> pattern %u\n", r.a, r.a % N_PATTERNS + 1); break;
        }
        _lr = (uint8_t)((_lr + 1) & (LOG_N - 1));
    }
#endif
}

// ============================================================
//  MIDI callbacks (called from loop() via midi_handler_poll / usb_host_poll)
// ============================================================
void midi_note_on(uint8_t note, uint8_t velocity) {
    const int inst = machine.padHit(note, velocity);
#if ENABLE_LCD
    ui_midi_activity();
#endif
    log_push(L_PAD, note, velocity, (int16_t)inst);
}
void midi_note_off(uint8_t) {}                     // drum hits are one-shot
void midi_pitch_bend(int16_t) {}
void midi_cc(uint8_t cc, uint8_t value) {
    const bool used = machine.handleCC(cc, value);
#if ENABLE_LCD
    ui_midi_activity();
#endif
    log_push(L_CC, cc, value, used ? 1 : 0);
}
void midi_program_change(uint8_t program) {        // program change = pattern select (queued to the bar end while playing)
    machine.setPattern(program % N_PATTERNS);
    log_push(L_PC, program, 0);
}
void midi_realtime(uint8_t status) { machine.midiRealtime(status); }     // clock / start / continue / stop

// ============================================================
//  Core 0
// ============================================================
void setup() {
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, LOW);

    midi_handler_init();

#if ENABLE_SERIAL
    Serial.begin(115200);                          // no while(!Serial): runs without a terminal
    delay(100);
    Serial.println("=== Drum Machine ===");
    Serial.printf("%d instruments, %d patterns x %d steps, control: %d samples\n", N_INST, N_PATTERNS, N_STEPS, CONTROL_DIV);
    Serial.printf("audio shield: %s  (DIN GP%d, BCK GP%d, LRCK GP%d)  <- silent if this is not your board\n",
                  AUDIO_BOARD_NAME, AUDIO_DATA_PIN, AUDIO_CLOCK_PIN_BASE, AUDIO_CLOCK_PIN_BASE + 1);
#endif

    Wavetable::init();
    machine.init();
    storage_init();
    const bool loaded = storage_load(machine);     // a missing / invalid file leaves the factory demo
    audio_driver_init(SAMPLE_RATE);                // claims PIO2 first

#if ENABLE_USB_HOST
    usb_host_init();                               // after audio: USB takes PIO0 (TX) / PIO1 (RX)
#endif
#if ENABLE_LCD
    lcd_begin();
#endif
#if ENABLE_SERIAL
    Serial.printf("patterns: %s\n", loaded ? "loaded from flash" : "factory demo (nothing saved, or the file is not valid)");
    Serial.println("Ready. Type 'help'.");
#endif
    digitalWrite(LED_PIN, HIGH);
}

void loop() {
    midi_handler_poll();
#if ENABLE_USB_HOST
    usb_host_poll();
#endif
    machine.service();                               // apply what the audio core recorded
#if ENABLE_LCD
    ui_handle_buttons(machine);
    ui_flush(machine);                               // cheap when idle; sends at most one rectangle per call
#endif
#if ENABLE_SERIAL
    log_flush();
    handleSerial();
#endif
}

// ============================================================
//  Core 1  —  audio, runs from SRAM
// ============================================================
void setup1() { delay(500); }                      // let core 0 finish init (tables, USB host, LCD)

void __not_in_flash_func(loop1)() {
    // CPU load = cycles spent in tick() / cycles available per buffer.
    // DWT cycle counter via raw registers (CMSIS structs are not exposed in arduino-pico).
    static constexpr uint32_t BUDGET_CY =
        (uint32_t)((uint64_t)F_CPU * BUFFER_FRAMES / SAMPLE_RATE);
    #define _DCB_DEMCR  (*((volatile uint32_t*)0xE000EDFC))
    #define _DWT_CYCCNT (*((volatile uint32_t*)0xE0001004))
    #define _DWT_CTRL   (*((volatile uint32_t*)0xE0001000))
    _DCB_DEMCR |= 0x01000000u;                     // TRCENA
    _DWT_CYCCNT = 0;
    _DWT_CTRL  |= 0x00000001u;                     // CYCCNTENA

    uint32_t total_cycles = 0;
    for (int i = 0; i < BUFFER_FRAMES; i++) {
        const uint32_t c0 = _DWT_CYCCNT;
        int16_t l, r;
        machine.tick(l, r);
        total_cycles += _DWT_CYCCNT - c0;
        audio_driver_put(l, r);                    // blocks until the FIFO has room: paces the loop
    }

    const uint32_t load_now = (total_cycles * 100) / BUDGET_CY;
    static uint32_t filt16 = 0;                    // x16 fixed point, fast attack / fast decay
    const uint32_t target = load_now * 16;
    if (target > filt16) filt16 = (filt16 * 1 + target * 15) / 16;
    else                 filt16 = (filt16 * 13 + target * 3) / 16;
    g_cpu_load_pct = (uint8_t)((filt16 / 16) > 99 ? 99 : filt16 / 16);
}

// ============================================================
//  Serial commands
// ============================================================
#if ENABLE_SERIAL
static void printGrid() {
    Serial.printf("pattern %u, length %u   (. off  1 ghost  2 normal  3 accent)\n", machine.selPat + 1, machine.patLen[machine.selPat]);
    Serial.print("   "); for (int s = 0; s < N_STEPS; s++) { Serial.print(s % 4 == 0 ? "|" : ""); Serial.print(s % 10); } Serial.println();
    for (int i = 0; i < N_INST; i++) {
        Serial.printf("%s ", INST_LABEL[i]);
        for (int s = 0; s < N_STEPS; s++) { if (s % 4 == 0) Serial.print("|"); const uint8_t v = machine.pat[machine.selPat][i][s]; Serial.print(v ? (char)('0' + v) : '.'); }
        Serial.println();
    }
}
static void printKit() {
    Serial.println("  inst  level tune decay tone pan");
    for (int i = 0; i < N_INST; i++)
        Serial.printf("%c %s %-10s %3u  %3u  %3u  %3u  %3u\n", i == machine.sel ? '>' : ' ', INST_LABEL[i], INST_NAME[i], machine.kit[i][0], machine.kit[i][1], machine.kit[i][2], machine.kit[i][3], machine.kit[i][4]);
}
static void printParams() {
    Serial.printf("selected: %s   bpm %.1f   swing %u   %s   sync %s   pads %s (base %u)\n", INST_NAME[machine.sel], machine.bpm, machine.pv[P_SWING],
                  machine.songMode ? "SONG" : "PATTERN", machine.extSync ? "MIDI clock" : "internal", machine.padGm ? "GM" : "linear", machine.padBase);
    for (int i = 0; i < N_PARAMS; i++) Serial.printf("  %-6s %-12s = %3u   (CC %u)\n", PARAM_INFO[i].cmd, PARAM_INFO[i].name, machine.pv[i], PARAM_INFO[i].cc);
}

static void runCommand(String line) {
    line.trim();
    if (!line.length()) return;
    int a = 0, b = 0, c = 0;
    if (line == "help") {
        Serial.println("  stat                 CPU load, transport, live voices, audio PIO");
        Serial.println("  grid                 the selected pattern as text");
        Serial.println("  kit | params         instrument settings | machine settings");
        Serial.println("  play | stop | rec on|off | resume");
        Serial.println("  hit <inst 0-15> [vel]   trigger an instrument (BD SD LT MT HT RS CP CB CH OH CY CL MA LC MC HC)");
        Serial.println("  pad <note> [vel]        simulate a pad note");
        Serial.println("  set <name> <0-127>   names: inst level tune decay tone pan tempo swing vol drive");
        Serial.println("  bpm <30-300>         exact tempo");
        Serial.println("  cc <n> <0-127>");
        Serial.println("  pat <1-16>           select pattern (queued to the bar end while playing)");
        Serial.println("  len <1-16>           pattern length");
        Serial.println("  step <inst> <step 1-16> <0-3>   edit a step");
        Serial.println("  clear | copy <to 1-16>          clear / copy the selected pattern");
        Serial.println("  song <p1> <p2> ...   set the chain (e.g. song 1 1 2 3), 'song' alone prints it");
        Serial.println("  songmode on|off      sync int|ext      pads linear|gm      padbase <note>");
        Serial.println("  save | load | demo   flash save (stopped only) | load | restore the factory demo");
        Serial.println("  dump | image <hex>   the whole state (kit, patterns, song, settings) as one hex line | load it (stopped only); used by tools/drums");
        Serial.println("  panic                silence everything now");
        Serial.println("  log notes|cc|all|off what MIDI events to print (notes: shows which instrument each pad is)");
        Serial.println("  pio                  audio PIO state");
    } else if (line == "stat") {
        Serial.printf("CPU: %u%%   %s  pattern %u step %u   bpm %.1f   live voices %u/%d\n", (unsigned)g_cpu_load_pct, machine.playing ? "PLAYING" : "stopped",
                      machine.patNow + 1, machine.stepNow + 1, machine.bpm, machine.liveCount(), N_INST);
        audio_driver_debug();
    } else if (line == "grid") { printGrid();
    } else if (line == "kit") { printKit();
    } else if (line == "params") { printParams();
    } else if (line == "play") { machine.play(); Serial.println("play");
    } else if (line == "stop") { machine.stop(); Serial.println("stop");
    } else if (line == "resume") { machine.resume(); Serial.println("continue");
    } else if (line == "rec on" || line == "rec off") { machine.setRecord(line == "rec on"); Serial.printf("record %s\n", machine.recording ? "ON" : "off");
    } else if (line == "panic") { machine.panic(); Serial.println("panic");
    } else if (line == "pio") { audio_driver_debug();
    } else if (sscanf(line.c_str(), "hit %d %d", &a, &b) >= 1) { machine.hit((uint8_t)constrain(a, 0, 15), b > 0 ? (uint8_t)constrain(b, 1, 127) : 100);
    } else if (sscanf(line.c_str(), "pad %d %d", &a, &b) >= 1) { Serial.printf("pad %d -> %d\n", a, machine.padHit((uint8_t)a, b > 0 ? (uint8_t)b : 100));
    } else if (sscanf(line.c_str(), "bpm %d", &a) == 1) { machine.setBpm((float)a); Serial.printf("bpm %.1f\n", machine.bpm);
    } else if (line.startsWith("bpm ")) { machine.setBpm(line.substring(4).toFloat()); Serial.printf("bpm %.1f\n", machine.bpm);
    } else if (line.startsWith("cc ")) {
        const int sp = line.indexOf(' ', 3);
        if (sp > 0) { const int cc = line.substring(3, sp).toInt(), v = line.substring(sp + 1).toInt(); Serial.printf("CC %d = %d  %s\n", cc, v, machine.handleCC((uint8_t)cc, (uint8_t)constrain(v, 0, 127)) ? "" : "(unused)"); }
        else Serial.println("usage: cc <n> <0-127>");
    } else if (line.startsWith("set ")) {
        const int sp = line.indexOf(' ', 4);
        if (sp > 0) {
            const String name = line.substring(4, sp);
            const int v = constrain((int)line.substring(sp + 1).toInt(), 0, 127);
            int id = -1;
            for (int i = 0; i < N_PARAMS; i++) if (name == PARAM_INFO[i].cmd) id = i;
            if (id >= 0) { machine.setParam((uint8_t)id, (uint8_t)v); Serial.printf("%s = %d\n", PARAM_INFO[id].name, v); } else Serial.println("unknown parameter (type 'params')");
        } else Serial.println("usage: set <name> <0-127>");
    } else if (sscanf(line.c_str(), "pat %d", &a) == 1) { machine.setPattern((uint8_t)constrain(a - 1, 0, N_PATTERNS - 1)); Serial.printf("pattern %u%s\n", machine.selPat + 1, machine.playing ? " (at the bar end)" : "");
    } else if (sscanf(line.c_str(), "len %d", &a) == 1) { machine.setLength((uint8_t)a); Serial.printf("length %u\n", machine.patLen[machine.selPat]);
    } else if (sscanf(line.c_str(), "step %d %d %d", &a, &b, &c) == 3) { machine.setStep((uint8_t)a, (uint8_t)(b - 1), (uint8_t)c);
    } else if (line == "clear") { machine.clearPattern(machine.selPat); Serial.println("cleared");
    } else if (sscanf(line.c_str(), "copy %d", &a) == 1) { machine.copyPattern(machine.selPat, (uint8_t)constrain(a - 1, 0, N_PATTERNS - 1)); Serial.println("copied");
    } else if (line == "song") { for (int i = 0; i < SONG_MAX; i++) { if (machine.song[i] == 0xFF) break; Serial.printf("%u ", machine.song[i] + 1); } Serial.println();
    } else if (line.startsWith("song ")) {
        int slot = 0; const char* p = line.c_str() + 5;
        for (int i = 0; i < SONG_MAX; i++) machine.song[i] = 0xFF;
        while (*p && slot < SONG_MAX) { char* e; const long v = strtol(p, &e, 10); if (e == p) break; machine.setSongSlot((uint8_t)slot++, (uint8_t)constrain(v - 1, 0, N_PATTERNS - 1)); p = e; }
        Serial.printf("song: %d slots\n", slot);
    } else if (line == "songmode on" || line == "songmode off") { machine.songMode = (line == "songmode on"); Serial.println(machine.songMode ? "song mode" : "pattern mode");
    } else if (line == "sync int" || line == "sync ext") { machine.extSync = (line == "sync ext"); Serial.println(machine.extSync ? "sync: MIDI clock" : "sync: internal");
    } else if (line == "pads linear" || line == "pads gm") { machine.padGm = (line == "pads gm"); Serial.println(machine.padGm ? "pads: General MIDI" : "pads: linear");
    } else if (sscanf(line.c_str(), "padbase %d", &a) == 1) { machine.padBase = (uint8_t)constrain(a, 0, 111); Serial.printf("pad base note %u\n", machine.padBase);
    } else if (line == "save") { if (machine.playing) Serial.println("stop first: a flash write would stall the audio"); else Serial.println(storage_save(machine) ? "saved" : "SAVE FAILED");
    } else if (line == "load") { if (machine.playing) Serial.println("stop first"); else Serial.println(storage_load(machine) ? "loaded" : "load failed (nothing saved, or the file is not valid)");
    } else if (line == "dump") {
        static uint8_t img[DrumManager::SAVE_SIZE]; static char hex[2 * DrumManager::SAVE_SIZE + 1];
        const size_t n = machine.serialize(img, sizeof img);
        hex[drumHexEncode(img, n, hex)] = 0;
        Serial.print("IMAGE "); Serial.println(hex);
    } else if (line.startsWith("image ")) {
        if (machine.playing) Serial.println("image refused: stop first");
        else {
            static uint8_t img[DrumManager::SAVE_SIZE + 1]; size_t n = 0;
            if (!drumHexDecode(line.c_str() + 6, line.length() - 6, img, sizeof img, n)) Serial.println("image bad: not valid hex (or too long)");
            else if (!machine.deserialize(img, n)) Serial.println("image bad: wrong size, wrong header or CRC mismatch (nothing changed)");
            else { machine.selectInst(machine.sel); Serial.println("image ok"); }
        }
    } else if (line == "demo") { if (machine.playing) Serial.println("stop first"); else { machine.loadDemo(); Serial.println("factory demo restored"); }
    } else if (line == "log notes") { g_logMask = 1; Serial.println("log: notes / pads");
    } else if (line == "log cc")    { g_logMask = 3; Serial.println("log: notes + CC");
    } else if (line == "log all")   { g_logMask = 7; Serial.println("log: everything");
    } else if (line == "log off")   { g_logMask = 0; Serial.println("log: off");
    } else { Serial.print("Unknown: "); Serial.println(line); }
#if ENABLE_LCD
    ui_mark_all_dirty();
#endif
}

static void handleSerial() {
    static String line;
    while (Serial.available()) {
        const char c = (char)Serial.read();
        if (c == '\n' || c == '\r') { runCommand(line); line = ""; }
        else if (line.length() < (line.startsWith("image ") ? (size_t)(2 * DrumManager::SAVE_SIZE + 8) : (size_t)80)) line += c;
    }
}
#endif
