// ============================================================
//  main.cpp  —  DX7: six-operator FM synthesizer  (same architecture as the other instruments)
//
//  Core 0: MIDI (USB device + USB host + UART), LCD, buttons, serial. loop() spins flat out (no delay()).
//  Core 1: audio. loop1() lives in SRAM (__not_in_flash_func); every hot function it calls is force-inlined (HOT). The sine and
//          level tables (about 25 KB) are built on core 0 at start-up, so the audio core needs no libm.
//
//  MIDI: notes, pitch bend, CC (see dx7_params.h), program change = voice, and SysEx: a 32-voice DX7 bank dump replaces the voices.
//
//  Serial (115200): type  help
// ============================================================
#include <Arduino.h>
#include "config.h"
#include "dx7_manager.h"
#include "audio_driver.h"
#if ENABLE_LCD
#  include "lcd.h"
#endif
#include "midi_handler.h"
#if ENABLE_USB_HOST
#  include "usb_host.h"
#endif

static_assert(AUDIO_BOARD_REV2_1 == 0 || AUDIO_BOARD_REV2_1 == 1, "AUDIO_BOARD_REV2_1 must be 0 or 1 (see config.h)");

Dx7Manager machine;
volatile uint8_t g_cpu_load_pct = 0;     // written by core 1, read by core 0 (uint8 write is atomic)

#if ENABLE_SERIAL
static void handleSerial();
#endif

// ============================================================
//  MIDI event log: callbacks only enqueue a tiny record, loop() prints it (a callback must never wait on USB-CDC)
// ============================================================
enum : uint8_t { L_ON, L_OFF, L_CC, L_PB };
struct LogRec { uint8_t kind, a, b; int16_t v; };
#define LOG_N 64                                  // power of two
static LogRec           _log[LOG_N];
static volatile uint8_t _lw = 0, _lr = 0;
static uint8_t          g_logMask = 1;            // bit0 notes, bit1 CC, bit2 bend  (serial: log ...)

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
        const LogRec& r = _log[_lr];
        switch (r.kind) {
            case L_ON:  Serial.printf("ON  %3u v%3u%s\n", r.a, r.b, (r.a < KEY_LO || r.a > KEY_HI) ? "  (outside the playable range: ignored)" : ""); break;
            case L_OFF: Serial.printf("OFF %3u\n", r.a); break;
            case L_CC:  Serial.printf("CC  %3u = %3u%s\n", r.a, r.b, r.v ? "" : "  (unused)"); break;
            case L_PB:  Serial.printf("PB  %d\n", (int)r.v); break;
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
void midi_program_change(uint8_t program) { machine.setPatch((uint8_t)(program % machine.nBank)); }      // program change = voice
void midi_sysex_bank(const uint8_t* data) { machine.loadBank(data);
#if ENABLE_SERIAL
    Serial.println("SysEx: 32-voice bank received");
#endif
#if ENABLE_LCD
    ui_mark_all_dirty();
#endif
}
void midi_realtime(uint8_t) {}                    // parsed out of the stream (so it cannot corrupt a note) and ignored

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
    Serial.println("=== DX7 (six-operator FM) ===");
    Serial.printf("%d voices (%d slots), %d factory voices; load a DX7 32-voice SysEx bank to replace them\n", N_POLY, N_VOICES, DX_N_PRESETS);
    Serial.printf("audio shield: %s  (DIN GP%d, BCK GP%d, LRCK GP%d)  <- silent if this is not your board\n",
                  AUDIO_BOARD_NAME, AUDIO_DATA_PIN, AUDIO_CLOCK_PIN_BASE, AUDIO_CLOCK_PIN_BASE + 1);
#endif

    machine.init();
    audio_driver_init(SAMPLE_RATE);                // claims PIO2 first

#if ENABLE_USB_HOST
    usb_host_init();                               // after audio: USB takes PIO0 (TX) / PIO1 (RX)
#endif
#if ENABLE_LCD
    lcd_begin();
#endif
#if ENABLE_SERIAL
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
static void printParams() {
    Serial.printf("voice %d: %s   algorithm %d   pedal %s   voices %u/%d\n", machine.patchNo + 1, machine.patchName(), machine.patch.alg + 1, machine.pedalDown ? "DOWN" : "up", (unsigned)machine.voicesNow, N_POLY);
    for (int i = 0; i < N_PARAMS; i++) Serial.printf("  %-6s %-13s = %3u   (CC %u)\n", PARAM_INFO[i].cmd, PARAM_INFO[i].name, machine.pv[i], PARAM_INFO[i].cc);
}
static void printPatch() {
    const Dx7Patch& p = machine.patch;
    Serial.printf("voice %d \"%s\"  algorithm %d  feedback %d  transpose %d\n", machine.patchNo + 1, p.name, p.alg + 1, p.fb, (int)p.transpose - 24);
    Serial.printf("LFO speed %d delay %d  pitch depth %d sens %d  amp depth %d  wave %d\n", p.lfs, p.lfd, p.lpmd, p.lpms, p.lamd, p.lfw);
    for (int k = 0; k < 6; k++) { const Dx7Op& o = p.op[5 - k];
        Serial.printf("  OP%d %s coarse %2d fine %2d det %2d  OL %2d  R %2d %2d %2d %2d  L %2d %2d %2d %2d  vel %d ams %d\n", k + 1, dx7IsCarrier(p.alg, 5 - k) ? "car" : "mod",
                      o.coarse, o.fine, (int)o.det - 7, o.ol, o.r[0], o.r[1], o.r[2], o.r[3], o.l[0], o.l[1], o.l[2], o.l[3], o.kvs, o.ams); }
}
static void runCommand(String line) {
    line.trim();
    if (!line.length()) return;
    int a = 0, b = 0;
    if (line == "help") {
        Serial.println("  stat                 CPU load, voices, audio PIO");
        Serial.println("  params               all performance parameters  (set <name> <0-127>, e.g. set bright 80)");
        Serial.println("  patch                the current voice: algorithm, operators, envelopes");
        Serial.println("  voice <n>            select voice 1..N (also: program change, button B)");
        Serial.println("  on <key> [vel]       play a note (MIDI number 12..120; 60 = middle C)   off <key>");
        Serial.println("  pedal on|off         sustain pedal");
        Serial.println("  bend <-8192..8191>   cc <n> <0-127>     panic   silence now");
        Serial.println("  log notes|cc|all|off what MIDI events to print");
        Serial.println("  pio                  audio PIO state");
    } else if (line == "stat") {
        Serial.printf("CPU: %u%%   voices %u/%d   pedal %s\n", (unsigned)g_cpu_load_pct, (unsigned)machine.voicesNow, N_VOICES, machine.pedalDown ? "down" : "up");
        audio_driver_debug();
    } else if (sscanf(line.c_str(), "voice %d", &a) == 1) { machine.setPatch((uint8_t)constrain(a - 1, 0, machine.nBank - 1)); printPatch();
    } else if (line == "patch") { printPatch();
    } else if (line == "params") { printParams();
    } else if (line == "panic") { machine.panic(); Serial.println("panic");
    } else if (line == "pio") { audio_driver_debug();
    } else if (line == "pedal on" || line == "pedal off") { machine.setPedal(line == "pedal on"); Serial.println(machine.pedalDown ? "pedal down" : "pedal up");
    } else if (sscanf(line.c_str(), "on %d %d", &a, &b) >= 1) { machine.noteOn((uint8_t)constrain(a, 0, 127), b > 0 ? (uint8_t)constrain(b, 1, 127) : 90);
    } else if (sscanf(line.c_str(), "off %d", &a) == 1) { machine.noteOff((uint8_t)constrain(a, 0, 127));
    } else if (sscanf(line.c_str(), "bend %d", &a) == 1) { machine.pitchBend((int16_t)constrain(a, -8192, 8191));
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
