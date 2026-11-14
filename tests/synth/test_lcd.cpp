// LCD tests: emulate the ST7789 behind SPI1 and check what the panel would actually show.
#include <Arduino.h>
#include <SPI.h>
#include <vector>
#include <cstdint>
#include "synth_manager.h"
volatile uint8_t g_cpu_load_pct = 0;
SynthManager synth;
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
// run ui_flush as loop() would until nothing is pending; returns number of calls
static int settle() {
    int calls = 0;
    for (int k = 0; k < 400; k++) { idle(25); ui_flush(synth); calls++; if (!_pending && k > 0) { idle(25); ui_flush(synth); if (!_pending) break; } }
    return calls;
}
static void savePng(const char* path) {
    FILE* f = fopen(path, "wb"); if (!f) return;
    // write raw RGB for a python converter
    for (int i = 0; i < LCD_W * LCD_H; i++) { uint16_t c = scr[i]; uint8_t r = (c >> 11) << 3, g = ((c >> 5) & 63) << 2, b = (c & 31) << 3; fputc(r, f); fputc(g, f); fputc(b, f); }
    fclose(f);
}

static void press(int pin, bool down) { g_pin[pin] = down ? 0 : 1; }
static void releaseAll() { for (int p : {BTN_UP, BTN_DOWN, BTN_LEFT, BTN_RIGHT, BTN_PRESS, BTN_A, BTN_B}) g_pin[p] = 1; }
static void tap(int pin) { press(pin, true); idle(10); ui_handle_buttons(synth); press(pin, false); idle(10); ui_handle_buttons(synth); }

int main() {
    Wavetable::init(); releaseAll(); emuInit();
    synth.init();

    printf("== first paint ==\n");
    lcd_begin();
    SPI1.calls = SPI1.bytes = 0; pixelsWritten = 0;
    ui_flush(synth);                                           // paints everything once
    printf("  first paint: %llu SPI calls, %llu bytes, %ld pixels\n", (unsigned long long)SPI1.calls, (unsigned long long)SPI1.bytes, pixelsWritten);
    CHECK(diffPixels() == 0, "panel differs from framebuffer after first paint (%d px)", diffPixels());
    savePng("panel_init.rgb");
    CHECK(outOfWindow == 0, "pixels sent outside the address window: %ld", outOfWindow);

    printf("\n== one knob changes: how much is sent? ==\n");
    SPI1.calls = SPI1.bytes = 0;
    synth.setParam(P_RES, 90); settle();                       // first edit also flips the patch name -> top bar redraw
    SPI1.calls = SPI1.bytes = 0;
    synth.setParam(P_RES, 100);
    int calls = settle();
    printf("  RES knob: %llu SPI calls, %llu bytes over %d loop passes (old design: ~64,800 SPI calls, 64.8 KB)\n", (unsigned long long)SPI1.calls, (unsigned long long)SPI1.bytes, calls);
    CHECK(SPI1.bytes < 6000 && SPI1.calls < 40, "single knob should be a small bulk transfer");
    CHECK(diffPixels() == 0, "panel == framebuffer after knob change (%d px)", diffPixels());

    printf("\n== idle: no traffic ==\n");
    SPI1.calls = SPI1.bytes = 0; for (int i = 0; i < 200; i++) { idle(1); ui_flush(synth); }
    CHECK(SPI1.bytes == 0, "idle panel must not transmit (%llu bytes)", (unsigned long long)SPI1.bytes);

    printf("\n== worst single flush call (time-slice) ==\n");
    { uint64_t worst = 0; synth.loadPatch(2); ui_mark_all_dirty();
      for (int k = 0; k < 400; k++) { idle(25); SPI1.bytes = 0; ui_flush(synth); if (SPI1.bytes > worst) worst = SPI1.bytes; }
      printf("  largest rectangle per ui_flush() call: %llu bytes = %.2f ms at 7.5 MB/s\n", (unsigned long long)worst, worst / 7500.0);
      CHECK(worst <= 8700, "no call may send more than the top bar / status block"); }
    CHECK(diffPixels() == 0, "panel == framebuffer after patch change (%d px)", diffPixels());

    printf("\n== random state changes: panel must always equal framebuffer ==\n");
    srand(5); int bad = 0;
    for (int it = 0; it < 400; it++) {
        const int r = rand() % 6;
        if (r == 0) synth.setParam(rand() % N_PARAMS, rand() % 128);
        else if (r == 1) synth.pitchBend((int16_t)(rand() % 16384 - 8192));
        else if (r == 2) synth.setGlide(!synth.glide());
        else if (r == 3) synth.noteOn(30 + rand() % 60, 100);
        else if (r == 4) synth.noteOff(30 + rand() % 60);
        else ui_midi_activity();
        if (rand() % 3 == 0) _sel = rand() % N_KNOBS;
        for (int k = 0; k < (rand() % 5); k++) { idle(rand() % 40); ui_flush(synth); }
        if (rand() % 4 == 0) { settle(); if (diffPixels()) { bad++; printf("   mismatch %d px at iteration %d\n", diffPixels(), it); } }
        for (int i = 0; i < 300; i++) synth.tick();
    }
    CHECK(bad == 0, "%d inconsistencies between panel and framebuffer", bad);
    synth.panic(); for (int i = 0; i < 4000; i++) synth.tick();

    printf("\n== MIDI dot turns itself off ==\n");
    synth.loadPatch(0); settle();
    ui_midi_activity(); idle(30); ui_flush(synth); settle();
    const uint16_t onPix = scr[5 * LCD_W + 231];
    idle(300); settle();                                       // no other event at all
    const uint16_t offPix = scr[5 * LCD_W + 231];
    printf("  dot pixel while active 0x%04X, 300 ms later 0x%04X\n", onPix, offPix);
    CHECK(onPix == C_GREEN && offPix == C_GREENDIM, "MIDI dot must dim by itself");

    printf("\n== CPU square colours ==\n");
    for (int cpu : {10, 60, 90}) { g_cpu_load_pct = cpu; idle(30); ui_flush(synth); settle(); }
    CHECK(scr[7 * LCD_W + 220] == C_RED, "90%% -> red"); g_cpu_load_pct = 60; settle(); CHECK(scr[7 * LCD_W + 220] == C_ORANGE, "60%% -> orange"); g_cpu_load_pct = 5; settle(); CHECK(scr[7 * LCD_W + 220] == C_GREEN, "5%% -> green");

    printf("\n== joystick / buttons ==\n");
    synth.loadPatch(0); _sel = P_CUTOFF; releaseAll(); idle(50); ui_handle_buttons(synth);
    tap(BTN_RIGHT); CHECK(_sel == P_RES, "RIGHT selects next knob (%d)", _sel);
    tap(BTN_LEFT);  tap(BTN_LEFT); CHECK(_sel == P_FENV - 2 || _sel == P_CUTOFF - 1, "LEFT x2 (sel=%d)", _sel);
    _sel = 0; tap(BTN_LEFT); CHECK(_sel == N_KNOBS - 1, "LEFT wraps (%d)", _sel); tap(BTN_RIGHT); CHECK(_sel == 0, "RIGHT wraps");
    _sel = P_CUTOFF; const int c0 = synth.pv[P_CUTOFF]; tap(BTN_UP); CHECK(synth.pv[P_CUTOFF] == c0 + 2, "UP +2 (%d -> %d)", c0, synth.pv[P_CUTOFF]);
    tap(BTN_DOWN); tap(BTN_DOWN); CHECK(synth.pv[P_CUTOFF] == c0 - 2, "DOWN -2x2");
    // hold UP: repeat after 350 ms, accelerate after 700 ms
    synth.setParam(P_CUTOFF, 0); press(BTN_UP, true); int v0 = 0; int steps = 0;
    for (int ms = 0; ms < 2500; ms += 5) { idle(5); ui_handle_buttons(synth); if (synth.pv[P_CUTOFF] != v0) { v0 = synth.pv[P_CUTOFF]; steps++; } if (v0 == 127) break; }
    press(BTN_UP, false); idle(10); ui_handle_buttons(synth);
    printf("  holding UP: cutoff 0 -> %d in %d steps\n", synth.pv[P_CUTOFF], steps); CHECK(synth.pv[P_CUTOFF] == 127, "hold-to-repeat reaches max");
    _sel = P_WAVE; synth.setParam(P_WAVE, 80); tap(BTN_UP); CHECK(synth.ep.wave == WAVE_SQUARE, "WAVE UP -> square"); tap(BTN_UP); CHECK(synth.ep.wave == WAVE_SQUARE, "WAVE clamps");
    tap(BTN_DOWN); CHECK(synth.ep.wave == WAVE_SAW, "WAVE DOWN -> saw");
    synth.loadPatch(0); tap(BTN_PRESS); CHECK(!strcmp(synth.patchName, "Pad"), "PRESS -> next patch (%s)", synth.patchName); tap(BTN_PRESS); tap(BTN_PRESS); CHECK(!strcmp(synth.patchName, "Init"), "patches wrap");
    synth.setGlide(false); tap(BTN_B); CHECK(synth.glide(), "B toggles glide on"); tap(BTN_B); CHECK(!synth.glide(), "B toggles glide off");
    synth.noteOn(60, 100); for (int i = 0; i < 4000; i++) synth.tick(); CHECK(synth.activeCount() == 1, "note playing");
    tap(BTN_A); for (int i = 0; i < 4000; i++) synth.tick(); CHECK(synth.activeCount() == 0, "A = panic silences everything");
    // a bouncing contact (3 ms) must not double-fire
    synth.setParam(P_CUTOFF, 50); _sel = P_CUTOFF; idle(20); ui_handle_buttons(synth);
    press(BTN_UP, true); idle(1); ui_handle_buttons(synth); press(BTN_UP, false); idle(1); ui_handle_buttons(synth); press(BTN_UP, true); idle(3); ui_handle_buttons(synth);
    press(BTN_UP, false); idle(10); ui_handle_buttons(synth);
    printf("  3 ms contact bounce moved cutoff 50 -> %d (one step = 52)\n", synth.pv[P_CUTOFF]); CHECK(synth.pv[P_CUTOFF] <= 52, "debounce");

    // final images
    synth.loadPatch(0); synth.setParam(P_RES, 60); synth.pitchBend(4096); synth.setGlide(true); _sel = P_AREL; g_cpu_load_pct = 37;
    for (int n : {48, 55, 60, 64, 67}) synth.noteOn(n, 100);
    ui_midi_activity(); for (int i = 0; i < 2000; i++) synth.tick(); settle();
    CHECK(diffPixels() == 0, "final panel == framebuffer");
    savePng("panel_played.rgb");
    synth.panic();
    printf("\nLCD TESTS: %s (%d failures)\n", fails ? "FAIL" : "PASS", fails);
    return fails ? 1 : 0;
}
