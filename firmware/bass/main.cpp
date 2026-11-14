// ============================================================
//  main.cpp  —  Bass Synth  (same architecture as the analog synth, the tonewheel organ, the string machine and the drum machine)
//
//  Core 0: MIDI (USB device + USB host + UART), LCD, buttons, serial. loop() spins flat out (no delay()).
//  Core 1: audio AND the sequencer. loop1() lives in SRAM (__not_in_flash_func); every hot function it calls is
//          force-inlined (HOT). The sequencer counts samples, so step times do not depend on core 0.
//
//  Serial (115200): type  help
// ============================================================
#include <Arduino.h>
#include "config.h"
#include "bass_tables.h"
#include "bass_manager.h"
#include "bass_storage.h"
#include "audio_driver.h"
#if ENABLE_LCD
#  include "lcd.h"
#endif
#include "midi_handler.h"
#if ENABLE_USB_HOST
#  include "usb_host.h"
#endif

static_assert(AUDIO_BOARD_REV2_1 == 0 || AUDIO_BOARD_REV2_1 == 1, "AUDIO_BOARD_REV2_1 must be 0 or 1 (see config.h)");

BassManager machine;
volatile uint8_t g_cpu_load_pct = 0;     // written by core 1, read by core 0 (uint8 write is atomic)

#if ENABLE_SERIAL
static void handleSerial();
#endif

// ============================================================
//  MIDI event log: callbacks only enqueue a tiny record, loop() prints it (a callback must never wait on USB-CDC)
// ============================================================
enum : uint8_t { L_ON, L_OFF, L_CC, L_PB, L_PC };
struct LogRec { uint8_t kind, a, b; int16_t v; };
#define LOG_N 64                                  // power of two
static LogRec           _log[LOG_N];
static volatile uint8_t _lw = 0, _lr = 0;
static uint8_t          g_logMask = 1;            // bit0 notes, bit1 CC, bit2 bend / program change  (serial: log ...)

static void log_push(uint8_t kind, uint8_t a, uint8_t b, int16_t v = 0) {
#if ENABLE_SERIAL
    const uint8_t bit = (kind == L_ON || kind == L_OFF) ? 1 : (kind == L_CC) ? 2 : 4;
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
        const LogRec& r = _log[_lr]; char nm[8]; noteName(r.a, nm);
        switch (r.kind) {
            case L_ON:  Serial.printf("ON  %3u %-3s v%3u%s\n", r.a, nm, r.b, r.b >= ACCENT_VEL ? "  ACCENT" : ""); break;
            case L_OFF: Serial.printf("OFF %3u %-3s\n", r.a, nm); break;
            case L_CC:  Serial.printf("CC  %3u = %3u%s\n", r.a, r.b, r.v ? "" : "  (unused)"); break;
            case L_PB:  Serial.printf("PB  %d\n", (int)r.v); break;
            case L_PC:  Serial.printf("PC  %u -> patch %u\n", r.a, r.a % N_PATCHES); break;
        }
        _lr = (uint8_t)((_lr + 1) & (LOG_N - 1));
    }
#endif
}

// ============================================================
//  MIDI callbacks (called from loop() via midi_handler_poll / usb_host_poll)
// ============================================================
void midi_note_on(uint8_t note, uint8_t velocity) {
    machine.noteOn(note, velocity);
#if ENABLE_LCD
    ui_midi_activity();
#endif
    log_push(L_ON, note, velocity);
}
void midi_note_off(uint8_t note) {
    machine.noteOff(note);
#if ENABLE_LCD
    ui_midi_activity();
#endif
    log_push(L_OFF, note, 0);
}
void midi_pitch_bend(int16_t bend) { machine.pitchBend(bend); log_push(L_PB, 0, 0, bend); }
void midi_cc(uint8_t cc, uint8_t value) {
    const bool used = machine.handleCC(cc, value);
#if ENABLE_LCD
    ui_midi_activity();
#endif
    log_push(L_CC, cc, value, used ? 1 : 0);
}
void midi_program_change(uint8_t program) { machine.loadPatch(program % N_PATCHES); log_push(L_PC, program, 0); }    // program change = patch
void midi_realtime(uint8_t status) { machine.midiRealtime(status); }                                                  // clock / start / continue / stop

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
    Serial.println("=== Bass Synth ===");
    Serial.printf("%d patterns x %d steps, control: %d samples, ladder cutoff %.0f Hz .. %.0f Hz\n", N_PATTERNS, N_STEPS, CONTROL_DIV, FILTER_MIN_HZ, FILTER_MIN_HZ * powf(2.0f, FILTER_OCTAVES));
    Serial.printf("audio shield: %s  (DIN GP%d, BCK GP%d, LRCK GP%d)  <- silent if this is not your board\n",
                  AUDIO_BOARD_NAME, AUDIO_DATA_PIN, AUDIO_CLOCK_PIN_BASE, AUDIO_CLOCK_PIN_BASE + 1);
#endif

    bass_tables_init();                            // BEFORE the audio core starts: it reads these tables
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
    Serial.printf("sound + patterns: %s\n", loaded ? "loaded from flash" : "factory (nothing saved, or the file is not valid)");
    Serial.println("Ready. Type 'help'.");
#endif
    digitalWrite(LED_PIN, HIGH);
}

void loop() {
    midi_handler_poll();
#if ENABLE_USB_HOST
    usb_host_poll();
#endif
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
    Serial.printf("pattern %u, length %u   (s = slide into the next step, a = accent, . = rest)\n", machine.selPat + 1, machine.patLen[machine.selPat]);
    for (int s = 0; s < N_STEPS; s++) {
        char nm[8]; noteName(machine.stepNote[machine.selPat][s], nm); const uint8_t f = machine.stepFlg[machine.selPat][s];
        Serial.printf("%2d %s%s%s%s\n", s + 1, (f & F_GATE) ? nm : ".", (f & F_SLIDE) ? "s" : "", (f & F_ACCENT) ? "a" : "", s >= machine.patLen[machine.selPat] ? "   (beyond the length)" : "");
    }
}
static void printParams() {
    Serial.printf("patch: %s   bpm %.1f   sync %s   keys %s   step-write %s   transpose %+d\n", machine.patchName(), machine.bpm, machine.extSync ? "MIDI clock" : "internal",
                  machine.keyMode ? "transpose" : "play", machine.stepWrite ? "ON" : "off", (int)machine.transpose);
    for (int i = 0; i < N_PARAMS; i++) Serial.printf("  %-6s %-13s = %3u   (CC %u)\n", PARAM_INFO[i].cmd, PARAM_INFO[i].name, machine.pv[i], PARAM_INFO[i].cc);
}

static void runCommand(String line) {
    line.trim();
    if (!line.length()) return;
    int a = 0, b = 0;
    if (line == "help") {
        Serial.println("  stat                 CPU load, transport, audio PIO");
        Serial.println("  params               all parameter values  (set <name> <0-127>, e.g. set cut 70)");
        Serial.println("  grid                 the selected pattern as text");
        Serial.println("  cc <n> <0-127>       send a MIDI CC to the machine");
        Serial.println("  on <note> [vel] | off <note>    play a note (MIDI number)");
        Serial.println("  patch <0-3>          0 Moog Bass  1 Sub  2 Acid  3 Pluck");
        Serial.println("  play | stop | resume | bpm <30-300>");
        Serial.println("  pat <1-16> | len <1-16> | clear | copy <to 1-16>");
        Serial.println("  step <1-16> <note>   e.g.  step 3 D#2sa   (s slide, a accent)   step 4 .   (rest)");
        Serial.println("  sync int|ext         keys play|transpose      write on|off  (step-write from the keyboard)");
        Serial.println("  save | load | demo   flash save (stopped only) | load | restore the factory state");
        Serial.println("  panic                silence everything now");
        Serial.println("  log notes|cc|all|off what MIDI events to print");
        Serial.println("  pio                  audio PIO state");
    } else if (line == "stat") {
        Serial.printf("CPU: %u%%   %s  pattern %u step %u   bpm %.1f   voice %s note %u\n", (unsigned)g_cpu_load_pct, machine.playing ? "PLAYING" : "stopped", machine.patNow + 1, machine.stepNow + 1,
                      machine.bpm, machine.gateNow ? "GATE" : (machine.voiceActive() ? "release" : "idle"), machine.noteNow);
        audio_driver_debug();
    } else if (line == "params") { printParams();
    } else if (line == "grid") { printGrid();
    } else if (line == "play") { machine.play(); Serial.println("play");
    } else if (line == "stop") { machine.stop(); Serial.println("stop");
    } else if (line == "resume") { machine.resume(); Serial.println("continue");
    } else if (line == "panic") { machine.panic(); Serial.println("panic");
    } else if (line == "pio") { audio_driver_debug();
    } else if (sscanf(line.c_str(), "on %d %d", &a, &b) >= 1) { machine.noteOn((uint8_t)constrain(a, 0, 127), b > 0 ? (uint8_t)constrain(b, 1, 127) : 100);
    } else if (sscanf(line.c_str(), "off %d", &a) == 1) { machine.noteOff((uint8_t)constrain(a, 0, 127));
    } else if (sscanf(line.c_str(), "patch %d", &a) == 1) { machine.loadPatch((uint8_t)a); Serial.printf("patch: %s\n", machine.patchName());
    } else if (line.startsWith("bpm ")) { machine.setBpm(line.substring(4).toFloat()); Serial.printf("bpm %.1f\n", machine.bpm);
    } else if (line.startsWith("cc ")) {
        const int sp = line.indexOf(' ', 3);
        if (sp > 0) { const int cc = line.substring(3, sp).toInt(), v = line.substring(sp + 1).toInt(); Serial.printf("CC %d = %d  %s\n", cc, v, machine.handleCC((uint8_t)cc, (uint8_t)constrain(v, 0, 127)) ? "" : "(unused)"); }
        else Serial.println("usage: cc <n> <0-127>");
    } else if (line.startsWith("set ")) {
        const int sp = line.indexOf(' ', 4);
        if (sp > 0) {
            const String name = line.substring(4, sp); const int v = constrain((int)line.substring(sp + 1).toInt(), 0, 127); int id = -1;
            for (int i = 0; i < N_PARAMS; i++) if (name == PARAM_INFO[i].cmd) id = i;
            if (id >= 0) { machine.setParam((uint8_t)id, (uint8_t)v); Serial.printf("%s = %d\n", PARAM_INFO[id].name, v); } else Serial.println("unknown parameter (type 'params')");
        } else Serial.println("usage: set <name> <0-127>");
    } else if (sscanf(line.c_str(), "pat %d", &a) == 1) { machine.setPattern((uint8_t)constrain(a - 1, 0, N_PATTERNS - 1)); Serial.printf("pattern %u%s\n", machine.selPat + 1, machine.playing ? " (at the bar end)" : "");
    } else if (sscanf(line.c_str(), "len %d", &a) == 1) { machine.setLength((uint8_t)a); Serial.printf("length %u\n", machine.patLen[machine.selPat]);
    } else if (line == "clear") { machine.clearPattern(machine.selPat); Serial.println("cleared");
    } else if (sscanf(line.c_str(), "copy %d", &a) == 1) { machine.copyPattern(machine.selPat, (uint8_t)constrain(a - 1, 0, N_PATTERNS - 1)); Serial.println("copied");
    } else if (line.startsWith("step ")) {
        const int sp = line.indexOf(' ', 5); uint8_t note, flags;
        if (sp > 0 && parseStep(line.substring(sp + 1).c_str(), note, flags)) { machine.setStep((uint8_t)constrain(line.substring(5, sp).toInt() - 1, 0, N_STEPS - 1), note, flags); Serial.println("ok"); }
        else Serial.println("usage: step <1-16> <note>   e.g. step 3 D#2sa   or  step 4 .");
    } else if (line == "sync int" || line == "sync ext") { machine.extSync = (line == "sync ext"); Serial.println(machine.extSync ? "sync: MIDI clock" : "sync: internal");
    } else if (line == "keys play" || line == "keys transpose") { machine.keyMode = (line == "keys transpose"); Serial.println(machine.keyMode ? "keys transpose the running sequence" : "keys play over the sequence");
    } else if (line == "write on" || line == "write off") { machine.stepWrite = (line == "write on"); Serial.println(machine.stepWrite ? "step-write ON: keys enter notes at the cursor" : "step-write off");
    } else if (line == "save") { if (machine.playing) Serial.println("stop first: a flash write would stall the audio"); else Serial.println(storage_save(machine) ? "saved" : "SAVE FAILED");
    } else if (line == "load") { if (machine.playing) Serial.println("stop first"); else Serial.println(storage_load(machine) ? "loaded" : "load failed (nothing saved, or the file is not valid)");
    } else if (line == "demo") { if (machine.playing) Serial.println("stop first"); else { machine.loadDemo(); Serial.println("factory state restored"); }
    } else if (line == "log notes") { g_logMask = 1; Serial.println("log: notes");
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
        else if (line.length() < 80) line += c;
    }
}
#endif
