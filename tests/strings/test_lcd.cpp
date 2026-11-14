// LCD tests for the string machine: emulate the ST7789 behind SPI1 and check what the panel would actually show.
#include <Arduino.h>
#include <SPI.h>
#include <vector>
#include <cstdint>
#include "strings_manager.h"
volatile uint8_t g_cpu_load_pct = 0;
StringsManager machine;
#include "lcd.cpp"            // include the real implementation to reach FB / _sel / _pending

static int fails = 0;
#define CHECK(cond, ...) do { if (!(cond)) { fails++; printf("  FAIL (line %d): %s   ", __LINE__, #cond); printf(__VA_ARGS__); printf("\n"); } } while (0)

// ---------------- ST7789 emulator ------------------------------------------------
static uint16_t scr[LCD_W * LCD_H];
static int  dcv = 1, csv = 1, cmdc = -1, pn = 0;
static uint8_t pb[4];
static int  cx0, cx1, ry0, ry1, wx, wy, half = 0; static uint8_t hib = 0;
static long pixelsWritten = 0, outOfWindow = 0;
static void pinHook(int p, int v) { if (p == LCD_DC) dcv = v; if (p == LCD_CS) csv = v; }
static void spiHook(const uint8_t* d, size_t n) {
    if (csv) return;
    for (size_t i = 0; i < n; i++) {
        const uint8_t b = d[i];
        if (!dcv) { cmdc = b; pn = 0; if (b == 0x2C) { wx = cx0; wy = ry0; half = 0; } continue; }
        if (cmdc == 0x2A || cmdc == 0x2B) {
            pb[pn++] = b;
            if (pn == 4) {
                int a = ((pb[0] << 8) | pb[1]), c = ((pb[2] << 8) | pb[3]);
                if (cmdc == 0x2A) { cx0 = a - 40; cx1 = c - 40; } else { ry0 = a - 53; ry1 = c - 53; }
                pn = 0;
            }
        } else if (cmdc == 0x2C) {
            if (!half) { hib = b; half = 1; }
            else {
                half = 0;
                if (wx >= 0 && wx < LCD_W && wy >= 0 && wy < LCD_H && wx <= cx1 && wy <= ry1) { scr[wy * LCD_W + wx] = (uint16_t)((hib << 8) | b); pixelsWritten++; }
                else outOfWindow++;
                if (++wx > cx1) { wx = cx0; wy++; }
            }
        }
    }
}
static void emuInit() { g_pin_hook = pinHook; SPI1.hook = spiHook; memset(scr, 0, sizeof scr); }




static int diffPixels() { int d = 0; for (int i = 0; i < LCD_W * LCD_H; i++) if (scr[i] != FB[i]) d++; return d; }
static void idle(uint32_t ms) { g_mock_us += (uint64_t)ms * 1000; }
static int settle() {
    int calls = 0;
    for (int k = 0; k < 400; k++) { idle(25); ui_flush(machine); calls++; if (!_pending && k > 0) { idle(25); ui_flush(machine); if (!_pending) break; } }
    return calls;
}
static void press(int pin, bool down) { g_pin[pin] = down ? 0 : 1; }
static void releaseAll() { for (int p : {BTN_UP, BTN_DOWN, BTN_LEFT, BTN_RIGHT, BTN_PRESS, BTN_A, BTN_B}) g_pin[p] = 1; }
static void tap(int pin) { press(pin, true); idle(10); ui_handle_buttons(machine); press(pin, false); idle(10); ui_handle_buttons(machine); }

int main() {
    Wavetable::init(); releaseAll(); emuInit();
    machine.init();

    printf("== first paint ==\n");
    lcd_begin();
    SPI1.calls = SPI1.bytes = 0; pixelsWritten = 0;
    ui_flush(machine);
    printf("  first paint: %llu SPI calls, %llu bytes, %ld pixels\n", (unsigned long long)SPI1.calls, (unsigned long long)SPI1.bytes, pixelsWritten);
    CHECK(diffPixels() == 0, "panel differs from framebuffer after first paint (%d px)", diffPixels());
    CHECK(outOfWindow == 0, "pixels sent outside the address window: %ld", outOfWindow);

    printf("\n== one knob changes ==\n");
    machine.setParam(P_TONE, 90); settle();                  // first edit also renames the patch -> top bar redraw
    SPI1.calls = SPI1.bytes = 0; machine.setParam(P_TONE, 100); int calls = settle();
    printf("  TONE knob: %llu SPI calls, %llu bytes over %d loop passes (old design: ~64,800 SPI calls)\n", (unsigned long long)SPI1.calls, (unsigned long long)SPI1.bytes, calls);
    CHECK(SPI1.bytes < 6000 && SPI1.calls < 40, "single knob should be a small bulk transfer");
    CHECK(diffPixels() == 0, "panel == framebuffer after knob change");

    printf("\n== idle: no traffic ==\n");
    SPI1.calls = SPI1.bytes = 0; for (int i = 0; i < 200; i++) { idle(1); ui_flush(machine); }
    CHECK(SPI1.bytes == 0, "idle panel must not transmit (%llu bytes)", (unsigned long long)SPI1.bytes);

    printf("\n== worst single flush call ==\n");
    { uint64_t worst = 0; machine.loadPatch(2); ui_mark_all_dirty();
      for (int k = 0; k < 400; k++) { idle(25); SPI1.bytes = 0; ui_flush(machine); if (SPI1.bytes > worst) worst = SPI1.bytes; }
      printf("  largest rectangle per ui_flush() call: %llu bytes = %.2f ms at 7.5 MB/s\n", (unsigned long long)worst, worst / 7500.0);
      CHECK(worst <= 8700, "no call may send more than the top bar / status block"); }
    CHECK(diffPixels() == 0, "panel == framebuffer after patch change");

    printf("\n== random state changes: panel must always equal framebuffer ==\n");
    srand(5); int bad = 0;
    for (int it = 0; it < 400; it++) {
        const int r = rand() % 6;
        if (r == 0) machine.setParam(rand() % N_PARAMS, rand() % 128);
        else if (r == 1) machine.pitchBend((int16_t)(rand() % 16384 - 8192));
        else if (r == 2) machine.setPedal(!machine.sustainActive());
        else if (r == 3) machine.noteOn(30 + rand() % 60, 100);
        else if (r == 4) machine.noteOff(30 + rand() % 60);
        else ui_midi_activity();
        if (rand() % 3 == 0) _sel = rand() % N_KNOBS;
        for (int k = 0; k < (rand() % 5); k++) { idle(rand() % 40); ui_flush(machine); }
        if (rand() % 4 == 0) { settle(); if (diffPixels()) { bad++; printf("   mismatch %d px at iteration %d\n", diffPixels(), it); } }
        for (int i = 0; i < 300; i++) { int16_t l, rr; machine.tick(l, rr); }
    }
    CHECK(bad == 0, "%d inconsistencies between panel and framebuffer", bad);
    machine.setPedal(false); machine.panic(); for (int i = 0; i < 6000; i++) { int16_t l, rr; machine.tick(l, rr); }

    printf("\n== MIDI dot / CPU square ==\n");
    machine.loadPatch(0); settle(); ui_midi_activity(); idle(30); ui_flush(machine); settle();
    const uint16_t onPix = scr[5 * LCD_W + 231]; idle(300); settle(); const uint16_t offPix = scr[5 * LCD_W + 231];
    CHECK(onPix == C_GREEN && offPix == C_GREENDIM, "MIDI dot must dim by itself (0x%04X -> 0x%04X)", onPix, offPix);
    for (int cpu : {10, 60, 90}) { g_cpu_load_pct = cpu; idle(30); ui_flush(machine); settle(); }
    CHECK(scr[7 * LCD_W + 220] == C_RED, "90%% -> red"); g_cpu_load_pct = 60; settle(); CHECK(scr[7 * LCD_W + 220] == C_ORANGE, "60%% -> orange"); g_cpu_load_pct = 5; settle(); CHECK(scr[7 * LCD_W + 220] == C_GREEN, "5%% -> green");

    printf("\n== value text ==\n");
    { char b[16]; machine.setParam(P_ATTACK, 0); valueText(machine, P_ATTACK, b, sizeof b); CHECK(!strcmp(b, "4ms"), "attack 0 -> %s", b);
      machine.setParam(P_RELEASE, 127); valueText(machine, P_RELEASE, b, sizeof b); CHECK(!strcmp(b, "6.0s"), "release 127 -> %s", b);
      machine.setParam(P_DETUNE, 127); valueText(machine, P_DETUNE, b, sizeof b); CHECK(!strcmp(b, "18c"), "detune 127 -> %s", b);
      machine.setParam(P_ENS_RATE, 0); valueText(machine, P_ENS_RATE, b, sizeof b); CHECK(!strcmp(b, "0.15Hz"), "ens rate 0 -> %s", b);
      machine.setParam(P_BASS, 77); valueText(machine, P_BASS, b, sizeof b); CHECK(!strcmp(b, "77"), "plain value -> %s", b);
      printf("  ok\n"); }

    printf("\n== status block: voice pips (two rows) and sustain lamp ==\n");
    machine.loadPatch(0); machine.panic(); for (int i = 0; i < 6000; i++) { int16_t l, rr; machine.tick(l, rr); } settle();
    const int px0 = cellX(STAT_COL) + 3 + 2, py0 = cellY(2) + 2 + 2;
    CHECK(scr[py0 * LCD_W + px0] == C_KBKG, "pip 0 dark with no notes");
    for (int n = 40; n < 40 + 14; n++) { machine.noteOn(n, 100); for (int i = 0; i < 40; i++) { int16_t l, rr; machine.tick(l, rr); } }
    settle(); CHECK(scr[py0 * LCD_W + px0] == C_GREEN, "pip 0 green with notes");
    CHECK(scr[(py0 + 7) * LCD_W + px0 + 6 * 1] == C_GREEN, "second row (pip 13) lit with 14 notes");
    CHECK(scr[(py0 + 7) * LCD_W + px0 + 6 * 2] == C_KBKG, "pip 14 still dark");
    for (int n = 60; n < 60 + 12; n++) { machine.noteOn(n, 100); for (int i = 0; i < 40; i++) { int16_t l, rr; machine.tick(l, rr); } }
    settle(); CHECK(machine.countedVoices() == MAX_ACTIVE_VOICES && scr[py0 * LCD_W + px0] == C_RED, "at the cap the pips turn red");
    const int sx = cellX(STAT_COL) + 3, sy = cellY(2) + 28; bool lit = false; for (int dy = 0; dy < 7; dy++) for (int dx = 0; dx < 42; dx++) if (scr[(sy + dy) * LCD_W + sx + dx] == C_AMBER) lit = true;
    CHECK(!lit, "SUSTAIN lamp dark without pedal"); machine.setPedal(true); settle();
    lit = false; for (int dy = 0; dy < 7; dy++) for (int dx = 0; dx < 42; dx++) if (scr[(sy + dy) * LCD_W + sx + dx] == C_AMBER) lit = true; CHECK(lit, "SUSTAIN lamp lit with the pedal");
    machine.setPedal(false); machine.panic(); for (int i = 0; i < 6000; i++) { int16_t l, rr; machine.tick(l, rr); }

    printf("\n== joystick / buttons ==\n");
    machine.loadPatch(0); _sel = P_ATTACK; releaseAll(); idle(50); ui_handle_buttons(machine);
    tap(BTN_RIGHT); CHECK(_sel == P_RELEASE, "RIGHT selects the next knob (%d)", _sel);
    _sel = 0; tap(BTN_LEFT); CHECK(_sel == N_KNOBS - 1, "LEFT wraps (%d)", _sel); tap(BTN_RIGHT); CHECK(_sel == 0, "RIGHT wraps");
    _sel = P_TONE; const int c0 = machine.pv[P_TONE]; tap(BTN_UP); CHECK(machine.pv[P_TONE] == c0 + 2, "UP +2 (%d -> %d)", c0, machine.pv[P_TONE]);
    tap(BTN_DOWN); tap(BTN_DOWN); CHECK(machine.pv[P_TONE] == c0 - 2, "DOWN -2 x2");
    machine.setParam(P_TONE, 0); press(BTN_UP, true); int v0 = 0, steps = 0;
    for (int ms = 0; ms < 2500; ms += 5) { idle(5); ui_handle_buttons(machine); if (machine.pv[P_TONE] != v0) { v0 = machine.pv[P_TONE]; steps++; } if (v0 == 127) break; }
    press(BTN_UP, false); idle(10); ui_handle_buttons(machine);
    printf("  holding UP: tone 0 -> %d in %d steps\n", machine.pv[P_TONE], steps); CHECK(machine.pv[P_TONE] == 127, "hold-to-repeat reaches max");
    machine.loadPatch(0); tap(BTN_PRESS); CHECK(!strcmp(machine.patchName, "Cello"), "PRESS -> next patch (%s)", machine.patchName);
    tap(BTN_PRESS); tap(BTN_PRESS); tap(BTN_PRESS); CHECK(!strcmp(machine.patchName, "Ensemble"), "patches wrap after %d", N_PATCHES);
    machine.setPedal(false); tap(BTN_B); CHECK(machine.sustainActive(), "B latches the sustain on"); tap(BTN_B); CHECK(!machine.sustainActive(), "B again: off");
    machine.noteOn(60, 100); for (int i = 0; i < 6000; i++) { int16_t l, rr; machine.tick(l, rr); } CHECK(machine.activeCount() == 1, "note playing");
    tap(BTN_A); for (int i = 0; i < 8000; i++) { int16_t l, rr; machine.tick(l, rr); } CHECK(machine.activeCount() == 0, "A = panic silences everything");
    machine.setParam(P_TONE, 50); _sel = P_TONE; idle(20); ui_handle_buttons(machine);
    press(BTN_UP, true); idle(1); ui_handle_buttons(machine); press(BTN_UP, false); idle(1); ui_handle_buttons(machine); press(BTN_UP, true); idle(3); ui_handle_buttons(machine); press(BTN_UP, false); idle(10); ui_handle_buttons(machine);
    CHECK(machine.pv[P_TONE] <= 52, "3 ms contact bounce moved tone 50 -> %d (one step = 52)", machine.pv[P_TONE]);

    printf("\nLCD TESTS: %s (%d failures)\n", fails ? "FAIL" : "PASS", fails);
    return fails ? 1 : 0;
}
