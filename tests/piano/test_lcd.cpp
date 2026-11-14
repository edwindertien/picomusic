// LCD tests for the piano: emulate the ST7789 behind SPI1 and check what the panel would actually show.
#include <Arduino.h>
#include <SPI.h>
#include <vector>
#include <cstdint>
#include "piano_manager.h"
volatile uint8_t g_cpu_load_pct = 0;
PianoManager machine;
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
static int settle() { int calls = 0; for (int k = 0; k < 600; k++) { idle(25); ui_flush(machine); calls++; if (!_pending && k > 0) { idle(25); ui_flush(machine); if (!_pending) break; } } return calls; }
static void press(int pin, bool down) { g_pin[pin] = down ? 0 : 1; }
static void releaseAll() { for (int p : {BTN_UP, BTN_DOWN, BTN_LEFT, BTN_RIGHT, BTN_PRESS, BTN_A, BTN_B}) g_pin[p] = 1; }
static void tap(int pin) { press(pin, true); idle(10); ui_handle_buttons(machine); press(pin, false); idle(10); ui_handle_buttons(machine); }
static void run(long n) { for (long i = 0; i < n; i++) { int16_t l, r; machine.tick(l, r); } }
static uint16_t keyPx(int key, int dy = 8) { int x, w; bool b; keyRect(key, x, w, b); return scr[(KB_Y + dy) * LCD_W + (b ? x + 1 : x + w / 2)]; }
static bool regionHas(int x0, int x1, int y0, int y1, uint16_t c) { for (int y = y0; y < y1; y++) for (int x = x0; x < x1; x++) if (scr[y * LCD_W + x] == c) return true; return false; }

int main() {
    releaseAll(); emuInit(); machine.init();

    printf("== first paint ==\n");
    lcd_begin(); SPI1.calls = SPI1.bytes = 0; pixelsWritten = 0; ui_flush(machine);
    printf("  first paint: %llu SPI calls, %llu bytes, %ld pixels\n", (unsigned long long)SPI1.calls, (unsigned long long)SPI1.bytes, pixelsWritten);
    CHECK(diffPixels() == 0, "panel differs from framebuffer after first paint (%d px)", diffPixels());
    CHECK(outOfWindow == 0, "pixels sent outside the address window: %ld", outOfWindow);
    CHECK(SPI1.bytes == 64811, "first paint = the whole screen once (%llu bytes)", (unsigned long long)SPI1.bytes);

    printf("\n== keyboard geometry ==\n");
    { bool overlap = false, inside = true; int lastEnd = -1; int nw = 0, nb = 0;
      for (int k = KEY_LO; k <= KEY_HI; k++) { int x, w; bool b; keyRect(k, x, w, b); if (x < 0 || x + w > LCD_W || w < 2) inside = false; if (b) nb++; else { nw++; if (x < lastEnd) overlap = true; lastEnd = x + w; } }
      CHECK(nw == 52 && nb == 36, "52 white + 36 black keys (%d + %d)", nw, nb); CHECK(inside, "every key lies on the screen and is at least 2 px wide"); CHECK(!overlap, "white keys do not overlap");
      int x1, w1, x2, w2; bool b; keyRect(KEY_LO, x1, w1, b); keyRect(KEY_HI, x2, w2, b); CHECK(x1 == 0 && x2 + w2 >= LCD_W - 6, "the keyboard spans the screen (A0 at x=%d, C8 ends at %d)", x1, x2 + w2);
      for (int k = KEY_LO; k <= KEY_HI; k++) { bool cover = false; for (int s = 0; s < KB_STRIPS; s++) if ((_wmask[s][(k - KEY_LO) >> 5] >> ((k - KEY_LO) & 31)) & 1u) cover = true; if (!cover) { CHECK(false, "key %d is in no strip", k); break; } }
      CHECK(true, "every key belongs to at least one strip");
      int bx, bw, wx, ww; bool bb; keyRect(22, bx, bw, bb); keyRect(21, wx, ww, bb); int nx, nw2; keyRect(23, nx, nw2, bb); CHECK(bx > wx && bx < nx + 1, "A#0 sits between A0 and B0"); }

    printf("\n== knobs ==\n");
    { int x, y, w, h; widgetRect(W_K0 + 7, x, y, w, h); CHECK(x == cellX(7) && y == cellY(0) && y + h < KB_Y - 3, "the 8th knob slot is in the row above the keyboard"); }
    CHECK(_sel == P_VOL && _page == 0, "starts on VOL, page 1 (the piano)"); tap(BTN_RIGHT); CHECK(_sel == P_TONE, "RIGHT selects the next knob"); tap(BTN_LEFT); tap(BTN_LEFT); CHECK(_sel == P_RSIZE, "LEFT wraps to the last of the 16 knobs (reverb time)"); settle(); CHECK(_page == 1, "...which is on page 2 (the effects)"); tap(BTN_RIGHT); CHECK(_sel == P_VOL, "...and RIGHT wraps back");
    _sel = P_SOFT; settle(); CHECK(_page == 0, "SOFT is the 8th knob of page 1"); tap(BTN_RIGHT); settle(); CHECK(_sel == P_WAH && _page == 1, "RIGHT past the 8th knob turns the page: WAH is the first effect"); { char b[8]; (void)b; }
    CHECK(diffPixels() == 0, "page 2 is on the panel"); { int x0 = cellX(0) + CELL_W / 2 - 9; bool wahLabel = false; for (int y = cellY(0) + 1; y < cellY(0) + 9; y++) for (int x = x0; x < x0 + 18; x++) if (scr[y * LCD_W + x] == C_WHITE) wahLabel = true; CHECK(wahLabel, "the first knob of page 2 is labelled in the selected colour (WAH)"); }
    tap(BTN_LEFT); settle(); CHECK(_sel == P_SOFT && _page == 0 && diffPixels() == 0, "LEFT turns back to page 1"); _sel = P_VOL; settle();
    const int v0 = machine.pv[P_VOL]; tap(BTN_DOWN); CHECK(machine.pv[P_VOL] == v0 - 2, "DOWN = -2"); tap(BTN_UP); tap(BTN_UP); CHECK(machine.pv[P_VOL] == v0 + 2, "UP = +2");
    _sel = P_WIDTH; machine.setParam(P_WIDTH, 0); press(BTN_UP, true); int v = 0, st = 0;
    for (int ms = 0; ms < 3000; ms += 5) { idle(5); ui_handle_buttons(machine); if (machine.pv[P_WIDTH] != v) { v = machine.pv[P_WIDTH]; st++; } if (v == 127) break; }
    press(BTN_UP, false); idle(10); ui_handle_buttons(machine); printf("  holding UP: width 0 -> %d in %d steps\n", machine.pv[P_WIDTH], st); CHECK(machine.pv[P_WIDTH] == 127, "hold-to-repeat reaches max");
    { char b[16];
      machine.setParam(P_WLFO, 0); valueText(machine, P_WLFO, b, sizeof b); CHECK(!strcmp(b, "manual"), "wah auto %s", b); machine.setParam(P_WLFO, 127); valueText(machine, P_WLFO, b, sizeof b); CHECK(!strcmp(b, "6.4Hz"), "wah auto %s", b);
      machine.setParam(P_TREM, 64); valueText(machine, P_TREM, b, sizeof b); CHECK(!strcmp(b, "off"), "trem %s", b); machine.setParam(P_TREM, 0); valueText(machine, P_TREM, b, sizeof b); CHECK(!strcmp(b, "trem 100%"), "trem %s", b); machine.setParam(P_TREM, 127); valueText(machine, P_TREM, b, sizeof b); CHECK(!strcmp(b, "pan 100%"), "trem %s", b);
      machine.setParam(P_TRATE, 0); valueText(machine, P_TRATE, b, sizeof b); CHECK(!strcmp(b, "0.5Hz"), "trem rate %s", b); machine.setParam(P_RSIZE, 127); valueText(machine, P_RSIZE, b, sizeof b); CHECK(!strcmp(b, "100%"), "reverb time %s", b);
      machine.setParam(P_WLFO, 0); machine.setParam(P_TREM, 64); machine.setParam(P_TRATE, 64); machine.setParam(P_RSIZE, 64);
      machine.setParam(P_TONE, 64); valueText(machine, P_TONE, b, sizeof b); CHECK(!strcmp(b, "+0"), "tone %s", b); machine.setParam(P_TONE, 0); valueText(machine, P_TONE, b, sizeof b); CHECK(!strcmp(b, "-64"), "tone %s", b);
      machine.setParam(P_HARD, 64); valueText(machine, P_HARD, b, sizeof b); CHECK(!strcmp(b, "x1.4"), "hardness %s", b);
      machine.setParam(P_DECAY, 127); valueText(machine, P_DECAY, b, sizeof b); CHECK(!strcmp(b, "natural"), "decay %s", b); machine.setParam(P_DECAY, 60); valueText(machine, P_DECAY, b, sizeof b); CHECK(!strcmp(b, "6.0s"), "decay %s", b); machine.setParam(P_DECAY, 0); valueText(machine, P_DECAY, b, sizeof b); CHECK(!strcmp(b, "1.0s"), "decay %s", b);
      machine.setParam(P_TUNE, 127); valueText(machine, P_TUNE, b, sizeof b); CHECK(!strcmp(b, "+49c"), "tune %s", b); machine.setParam(P_WIDTH, 127); valueText(machine, P_WIDTH, b, sizeof b); CHECK(!strcmp(b, "100%"), "width %s", b);
      machine.setParam(P_VOL, 100); machine.setParam(P_TONE, 64); machine.setParam(P_TUNE, 64); machine.setParam(P_DECAY, 127); machine.setParam(P_WIDTH, 64); }
    settle(); CHECK(diffPixels() == 0, "panel == framebuffer");

    printf("\n== the keys light up ==\n");
    machine.allNotesOff(); machine.panic(); run(60000); settle();
    CHECK(keyPx(60) == IVORY && keyPx(61) == EBONY && keyPx(21) == IVORY, "at rest: white keys ivory, black keys ebony");
    machine.noteOn(60, 80); machine.noteOn(61, 80); machine.noteOn(21, 80); run(100); settle();
    CHECK(keyPx(60) == LIT_W && keyPx(21) == LIT_W, "sounding white keys are amber (C4, A0)"); CHECK(keyPx(61) == LIT_B, "a sounding black key is amber"); CHECK(keyPx(62) == IVORY && keyPx(59) == IVORY, "their neighbours stay ivory");
    machine.noteOn(108, 127); run(100); settle(); CHECK(keyPx(108) == LIT_W, "C8, the last key, lights too");
    CHECK(regionHas(150, 170, 5, 12, C_GRAY), "the top bar counts voices"); CHECK(diffPixels() == 0, "panel == framebuffer");
    machine.allNotesOff(); machine.panic(); run(60000); settle(); CHECK(keyPx(60) == IVORY && keyPx(61) == EBONY && keyPx(108) == IVORY, "...and they go dark again when the notes have died away");
    SPI1.calls = SPI1.bytes = 0; machine.noteOn(70, 80); run(100); settle();
    printf("  one key lights: %llu SPI calls, %llu bytes\n", (unsigned long long)SPI1.calls, (unsigned long long)SPI1.bytes); CHECK(SPI1.bytes <= 3 * 4400 + 8700, "a single key repaints at most its strips + the top bar (%llu bytes)", (unsigned long long)SPI1.bytes);
    machine.panic(); run(60000); settle(); SPI1.calls = SPI1.bytes = 0; for (int i = 0; i < 200; i++) { idle(1); ui_flush(machine); } CHECK(SPI1.bytes == 0, "an idle panel transmits nothing (%llu bytes)", (unsigned long long)SPI1.bytes);

    printf("\n== worst single flush ==\n");
    { uint64_t worst = 0; ui_mark_all_dirty(); for (int k = 0; k < 600; k++) { idle(25); SPI1.bytes = 0; ui_flush(machine); if (SPI1.bytes > worst) worst = SPI1.bytes; }
      printf("  largest rectangle per ui_flush() call %llu bytes = %.2f ms at 7.5 MB/s\n", (unsigned long long)worst, worst / 7500.0); CHECK(worst <= 8700, "no call may send more than the top bar"); CHECK(diffPixels() == 0, "repaint complete"); }

    printf("\n== pedals ==\n");
    releaseAll(); idle(50); ui_handle_buttons(machine); settle();
    CHECK(!machine.pedalDown && !regionHas(172, 190, 5, 12, C_GREEN), "pedal up: PED is dim");
    tap(BTN_A); settle(); CHECK(machine.pedalDown && regionHas(172, 190, 5, 12, C_GREEN), "A latches the sustain pedal on, PED lights"); tap(BTN_A); settle(); CHECK(!machine.pedalDown, "A again releases it");
    machine.softPedal = false; settle(); CHECK(!regionHas(194, 212, 5, 12, C_AMBER), "UNA is dim"); tap(BTN_B); settle(); CHECK(machine.softPedal && regionHas(194, 212, 5, 12, C_AMBER), "B latches una corda, UNA lights"); tap(BTN_B); settle();
    machine.softPedal = false; machine.setParam(P_SOFT, 100); settle(); CHECK(regionHas(194, 212, 5, 12, C_AMBER), "UNA also lights from the SOFT knob"); machine.setParam(P_SOFT, 0); settle();
    machine.handleCC(64, 127); settle(); CHECK(machine.pedalDown && regionHas(172, 190, 5, 12, C_GREEN), "CC64 from the keyboard lights PED too"); machine.handleCC(64, 0);
    machine.setPedal(false); machine.panic(); run(60000);

    printf("\n== audition, top bar ==\n");
    CHECK(!strcmp(_creditK, PIANO_INSTRUMENT[0].credit) && strlen(PIANO_INSTRUMENT[0].credit) <= 38, "the credit line shows the instrument's own credit ('%s'), at most 38 characters", _creditK);
    { bool ink = false; for (int y = 128; y < 135; y++) for (int x = 6; x < 234; x++) if (scr[y * LCD_W + x] == C_KRIM) ink = true; CHECK(ink, "...and it is on the panel in the credit row"); }
    CHECK(regionHas(4, 50, 5, 12, GOLD), "the instrument name is in the top bar, in gold"); { TopInfo t; fillTop(machine, t); CHECK(!strcmp(t.a, PIANO_INSTRUMENT[0].name), "top bar shows '%s'", t.a); CHECK(strlen(t.b) <= 16, "the parameter text stays within 16 characters (%zu)", strlen(t.b)); }
    for (int i = 0; i < N_PARAMS; i++) { _sel = i; machine.setParam(i, 127); TopInfo t; fillTop(machine, t); if (strlen(t.b) > 16) { CHECK(false, "top-bar text of knob %d is too long: '%s'", i, t.b); break; } }
    CHECK(true, "no knob's top-bar text overruns into the voice count"); _sel = P_VOL; for (int i = 0; i < N_PARAMS; i++) machine.setParam(i, PIANO_INSTRUMENT[0].def[i]); settle();
    press(BTN_PRESS, true); idle(10); ui_handle_buttons(machine); idle(700); ui_handle_buttons(machine); press(BTN_PRESS, false); idle(10); ui_handle_buttons(machine); run(200);
    CHECK(machine.voicesNow == 0, "a LONG press on PRESS does not audition"); CHECK(machine.instrument == (1 % PIANO_N_INSTRUMENTS), "...it selects the next instrument (instrument %d of %d)", machine.instrument, PIANO_N_INSTRUMENTS);
    machine.setInstrument(0); settle();
    tap(BTN_PRESS); run(200); CHECK(machine.keySounding(60) && machine.voicesNow == 1, "PRESS plays middle C"); idle(450); ui_handle_buttons(machine); run(60000); CHECK(!machine.keySounding(60), "...and its note-off follows by itself (the note dies away)");
    ui_midi_activity(); idle(30); ui_flush(machine); settle(); const uint16_t onPix = scr[5 * LCD_W + 231]; idle(300); settle(); const uint16_t offPix = scr[5 * LCD_W + 231]; CHECK(onPix == C_GREEN && offPix == C_GREENDIM, "MIDI dot dims by itself");
    for (int cpu : {10, 60, 90}) { g_cpu_load_pct = cpu; idle(30); ui_flush(machine); settle(); } CHECK(scr[7 * LCD_W + 220] == C_RED, "90%% -> red"); g_cpu_load_pct = 60; settle(); CHECK(scr[7 * LCD_W + 220] == C_ORANGE, "60%% -> orange"); g_cpu_load_pct = 5; settle(); CHECK(scr[7 * LCD_W + 220] == C_GREEN, "5%% -> green");
    CHECK(scr[16 * LCD_W + 100] == GOLD && scr[17 * LCD_W + 100] == GOLD_DIM, "gold trim under the top bar");

    printf("\n== random: panel must always equal the framebuffer ==\n");
    srand(9); int bad = 0;
    for (int it = 0; it < 1500; it++) {
        switch (rand() % 13) {
            case 0: tap(BTN_LEFT); break; case 1: tap(BTN_RIGHT); break; case 2: tap(BTN_UP); break; case 3: tap(BTN_DOWN); break; case 4: tap(BTN_PRESS); break;
            case 5: tap(BTN_A); break; case 6: tap(BTN_B); break;
            case 7: case 8: machine.noteOn(21 + rand() % 88, 1 + rand() % 127); break;
            case 9: machine.noteOff(21 + rand() % 88); break;
            case 10: machine.setParam(rand() % N_PARAMS, rand() % 128); break;
            case 11: machine.pitchBend((int16_t)(rand() % 16384 - 8192)); break;
            default: if (rand() % 10 == 0) machine.panic(); break;
        }
        for (int k = 0; k < (rand() % 5); k++) { idle(rand() % 40); ui_flush(machine); }
        run(rand() % 4000);
        if (rand() % 5 == 0) { settle(); if (diffPixels()) { bad++; printf("   mismatch %d px at iteration %d\n", diffPixels(), it); } }
    }
    CHECK(bad == 0, "%d inconsistencies between panel and framebuffer", bad);

    printf("\n== contact bounce ==\n");
    machine.panic(); run(60000); _sel = P_TONE; machine.setParam(P_TONE, 50); releaseAll(); idle(20); ui_handle_buttons(machine);
    press(BTN_UP, true); idle(1); ui_handle_buttons(machine); press(BTN_UP, false); idle(1); ui_handle_buttons(machine); press(BTN_UP, true); idle(3); ui_handle_buttons(machine); press(BTN_UP, false); idle(10); ui_handle_buttons(machine);
    CHECK(machine.pv[P_TONE] <= 52, "3 ms contact bounce moved tone 50 -> %d (one step = 52)", machine.pv[P_TONE]);
    printf("\nLCD TESTS: %s (%d failures)\n", fails ? "FAIL" : "PASS", fails);
    return fails ? 1 : 0;
}
