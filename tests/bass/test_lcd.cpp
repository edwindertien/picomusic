// LCD tests for the bass synth: emulate the ST7789 behind SPI1 and check what the panel would actually show.
#include <Arduino.h>
#include <SPI.h>
#include <vector>
#include <cstdint>
#include "bass_manager.h"
volatile uint8_t g_cpu_load_pct = 0;
BassManager machine;
static bool g_storage_ok = true; static int g_saves = 0;
bool storage_save(BassManager&) { g_saves++; return g_storage_ok; }
bool storage_load(BassManager&) { return g_storage_ok; }
#include "lcd.cpp"            // include the real implementation to reach FB / _page / _pending

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
    for (int k = 0; k < 600; k++) { idle(25); ui_flush(machine); calls++; if (!_pending && k > 0) { idle(25); ui_flush(machine); if (!_pending) break; } }
    return calls;
}
static void press(int pin, bool down) { g_pin[pin] = down ? 0 : 1; }
static void releaseAll() { for (int p : {BTN_UP, BTN_DOWN, BTN_LEFT, BTN_RIGHT, BTN_PRESS, BTN_A, BTN_B}) g_pin[p] = 1; }
static void tap(int pin) { press(pin, true); idle(10); ui_handle_buttons(machine); press(pin, false); idle(10); ui_handle_buttons(machine); }
static void run(long n) { for (long i = 0; i < n; i++) { int16_t l, r; machine.tick(l, r); } }
static void gotoPage(int p) { while (_page != p) { tap(BTN_B); settle(); } }
static uint16_t cellPx(int c, int dx, int dy) { int x, y, w, h; widgetRect(W_SEQ0 + c, x, y, w, h); return scr[(y + dy) * LCD_W + x + dx]; }

int main() {
    bass_tables_init(); releaseAll(); emuInit();
    machine.init();

    printf("== first paint ==\n");
    lcd_begin();
    SPI1.calls = SPI1.bytes = 0; pixelsWritten = 0;
    ui_flush(machine);
    printf("  first paint: %llu SPI calls, %llu bytes, %ld pixels\n", (unsigned long long)SPI1.calls, (unsigned long long)SPI1.bytes, pixelsWritten);
    CHECK(diffPixels() == 0, "panel differs from framebuffer after first paint (%d px)", diffPixels());
    CHECK(outOfWindow == 0, "pixels sent outside the address window: %ld", outOfWindow);
    CHECK(_page == PG_OSC, "starts on the OSC page");
    CHECK(_kcount[0] == 15 && _kcount[1] == 18, "knob pages hold 15 and 18 knobs (%d, %d)", _kcount[0], _kcount[1]);
    { bool clash = false; for (int a = 0; a < N_PARAMS; a++) for (int b = a + 1; b < N_PARAMS; b++) if (PARAM_INFO[a].page == PARAM_INFO[b].page && PARAM_INFO[a].col == PARAM_INFO[b].col && PARAM_INFO[a].row == PARAM_INFO[b].row) clash = true;
      CHECK(!clash, "no two knobs share a cell on a page"); }

    printf("\n== value text ==\n");
    { char b[16];
      machine.setParam(P_WAV1, 0); valueText(machine, P_WAV1, b, sizeof b); CHECK(!strcmp(b, "SAW"), "wave %s", b); machine.setParam(P_WAV1, 60); valueText(machine, P_WAV1, b, sizeof b); CHECK(!strcmp(b, "SQR"), "wave %s", b); machine.setParam(P_WAV1, 120); valueText(machine, P_WAV1, b, sizeof b); CHECK(!strcmp(b, "TRI"), "wave %s", b);
      machine.setParam(P_OCT1, 8); valueText(machine, P_OCT1, b, sizeof b); CHECK(!strcmp(b, "-1"), "octave %s", b); machine.setParam(P_OCT1, 104); valueText(machine, P_OCT1, b, sizeof b); CHECK(!strcmp(b, "+2"), "octave %s", b);
      machine.setParam(P_DET2, 127); valueText(machine, P_DET2, b, sizeof b); CHECK(!strcmp(b, "+49c"), "detune %s", b);
      machine.setParam(P_GLIDE, 0); valueText(machine, P_GLIDE, b, sizeof b); CHECK(!strcmp(b, "off"), "glide %s", b); machine.setParam(P_GLIDE, 127); valueText(machine, P_GLIDE, b, sizeof b); CHECK(!strcmp(b, "3.0s"), "glide %s", b);
      machine.setParam(P_CUT, 0); valueText(machine, P_CUT, b, sizeof b); CHECK(!strcmp(b, "20Hz"), "cutoff %s", b); machine.setParam(P_CUT, 127); valueText(machine, P_CUT, b, sizeof b); CHECK(!strcmp(b, "5.5k"), "cutoff %s", b);
      machine.setParam(P_RES, 127); valueText(machine, P_RES, b, sizeof b); CHECK(!strcmp(b, "108%"), "resonance 127 = 108 %% of critical: self-oscillation (%s)", b);
      machine.setParam(P_ENV, 127); valueText(machine, P_ENV, b, sizeof b); CHECK(!strcmp(b, "6.0oct"), "env %s", b);
      machine.setParam(P_FA, 0); valueText(machine, P_FA, b, sizeof b); CHECK(!strcmp(b, "1ms"), "attack %s", b); machine.setParam(P_FD, 127); valueText(machine, P_FD, b, sizeof b); CHECK(!strcmp(b, "3.0s"), "decay %s", b);
      machine.setParam(P_GATE, 127); valueText(machine, P_GATE, b, sizeof b); CHECK(!strcmp(b, "95%"), "gate %s", b); machine.setParam(P_SWING, 127); valueText(machine, P_SWING, b, sizeof b); CHECK(!strcmp(b, "75%"), "swing %s", b);
      machine.setBpm(123.4f); valueText(machine, P_TEMPO, b, sizeof b); CHECK(!strcmp(b, "123"), "tempo %s", b); machine.loadPatch(0); machine.setBpm(120); settle(); }

    printf("\n== knob pages ==\n");
    machine.loadPatch(0); settle();
    { int x, y, w, h; widgetRect(W_K0 + _kidx[P_MIX1], x, y, w, h); CHECK(x == cellX(0) && y == cellY(1), "MIX1 knob at column 0, row 1"); }
    tap(BTN_RIGHT); CHECK(_ksel[0] == P_WAV2, "RIGHT selects the next knob"); _ksel[0] = P_VOL; tap(BTN_RIGHT); CHECK(_ksel[0] == P_WAV1, "RIGHT wraps within the page (VOL is the last OSC knob)"); tap(BTN_LEFT); CHECK(_ksel[0] == P_VOL, "LEFT wraps back");
    _ksel[0] = P_MIX2; const int m0 = machine.pv[P_MIX2]; tap(BTN_UP); CHECK(machine.pv[P_MIX2] == m0 + 2, "UP = +2"); tap(BTN_DOWN); tap(BTN_DOWN); CHECK(machine.pv[P_MIX2] == m0 - 2, "DOWN = -2 twice");
    machine.setParam(P_MIX2, 0); press(BTN_UP, true); int v0 = 0, st = 0;
    for (int ms = 0; ms < 2500; ms += 5) { idle(5); ui_handle_buttons(machine); if (machine.pv[P_MIX2] != v0) { v0 = machine.pv[P_MIX2]; st++; } if (v0 == 127) break; }
    press(BTN_UP, false); idle(10); ui_handle_buttons(machine); printf("  holding UP: mix2 0 -> %d in %d steps\n", machine.pv[P_MIX2], st); CHECK(machine.pv[P_MIX2] == 127, "hold-to-repeat reaches max");
    settle(); CHECK(diffPixels() == 0, "panel == framebuffer");
    gotoPage(PG_FILT); machine.setBpm(120); _ksel[1] = P_TEMPO; tap(BTN_UP); CHECK(machine.bpm == 121.0f, "TEMPO moves in whole BPM (%.1f)", machine.bpm);
    machine.setBpm(100.5f); tap(BTN_DOWN); CHECK(machine.bpm == 100.0f || machine.bpm == 99.0f, "tempo from a fractional BPM lands on a whole one (%.1f)", machine.bpm);
    { int x, y, w, h; widgetRect(W_K0 + _kidx[P_TEMPO], x, y, w, h); CHECK(x == cellX(0) && y == cellY(2), "TEMPO sits at column 0, row 2 of the FILT page");
      widgetRect(W_K0 + _kcount[1], x, y, w, h); CHECK(x == cellX(2) && x >= cellX(1) + CELL_W, "status lines start right of the TEMPO and SWING knobs (x=%d)", x); }
    _ksel[1] = P_CUT; tap(BTN_PRESS); run(100); CHECK(machine.gateNow && machine.noteNow == KEY_ROOT, "PRESS auditions C2"); idle(400); ui_handle_buttons(machine); run(100); CHECK(!machine.gateNow, "...and the audition note ends by itself");
    run(30000); settle(); CHECK(diffPixels() == 0, "panel == framebuffer");

    printf("\n== SEQ page ==\n");
    gotoPage(PG_SEQ); machine.stop(); run(50); machine.setPattern(1); machine.cursor = 0; settle();     // 'Acid': C2 C2 C3a . C2 D#2s F2 . G1a ...
    { const uint16_t rest = cellPx(3, 10, 12), note = cellPx(0, 10, 12), acc = cellPx(2, 10, 12);
      CHECK(rest == C_KBKG, "rest cell is dark (0x%04X)", rest); CHECK(note == dimc(STEP_ON[0], 90), "note cell = dimmed group colour (0x%04X)", note); CHECK(acc == STEP_ON[0], "accent cell = full group colour (0x%04X)", acc);
      CHECK(cellPx(5, 10, 34) == C_WHITE, "slide step has the white bridge bar"); CHECK(cellPx(0, 10, 34) != C_WHITE, "plain step has none");
      CHECK(cellPx(0, -1 + 1 - 0, 20) == C_WHITE || cellPx(0, 0, 20) == C_WHITE || cellPx(0, 0, 3) == C_WHITE || scr[(SEQ_Y(0) + 20) * LCD_W + SEQ_X0 - 1] == C_WHITE, "white cursor frame on step 1"); }
    machine.setLength(12); settle(); { const uint16_t b = cellPx(14, 10, 28); CHECK(b == C_BG, "steps beyond the pattern length are blank (0x%04X)", b); } machine.setLength(16); settle();
    tap(BTN_RIGHT); CHECK(machine.cursor == 1, "RIGHT moves the cursor"); tap(BTN_LEFT); tap(BTN_LEFT); CHECK(machine.cursor == 15, "LEFT wraps to step 16"); machine.setLength(5); machine.cursor = 4; tap(BTN_RIGHT); CHECK(machine.cursor == 0, "cursor wraps at the pattern length"); machine.setLength(16);
    machine.cursor = 0; const int n0 = machine.stepNote[1][0]; tap(BTN_UP); CHECK(machine.stepNote[1][0] == n0 + 1, "UP = +1 semitone"); tap(BTN_DOWN); tap(BTN_DOWN); CHECK(machine.stepNote[1][0] == n0 - 1, "DOWN = -1 semitone"); tap(BTN_UP);
    press(BTN_UP, true); idle(10); ui_handle_buttons(machine); idle(800); ui_handle_buttons(machine); const int n1 = machine.stepNote[1][0]; press(BTN_UP, false); idle(10); ui_handle_buttons(machine); CHECK(n1 >= n0 + 12, "holding UP jumps by octaves (%d -> %d)", n0, n1);
    machine.setStep(0, 36, F_GATE); tap(BTN_PRESS); CHECK(machine.stepFlg[1][0] == (F_GATE | F_ACCENT), "PRESS: note -> accent"); tap(BTN_PRESS); CHECK(machine.stepFlg[1][0] == (F_GATE | F_SLIDE), "-> slide"); tap(BTN_PRESS); tap(BTN_PRESS); CHECK(machine.stepFlg[1][0] == 0, "-> rest");
    machine.cursor = 3; tap(BTN_UP); CHECK(machine.stepFlg[1][3] & F_GATE, "UP on a rest switches the step on"); machine.cursor = 0; machine.setPattern(1); settle(); CHECK(diffPixels() == 0, "panel == framebuffer");
    machine.setPattern(1); machine.play(); run(5 * 5512 + 3000); settle(); { const int c = machine.stepNow; CHECK(cellPx(c, 10, 1) == C_WHITE, "white playhead bar on the playing step (%d)", c); }
    machine.stop(); run(50); settle();

    printf("\n== SETUP page ==\n");
    gotoPage(PG_SETUP); machine.stop(); run(50); machine.setPattern(0); machine.extSync = false; machine.keyMode = 1; machine.stepWrite = false; _pr = 0; settle();
    tap(BTN_RIGHT); CHECK(machine.patch == 1, "RIGHT: next patch (Sub)"); tap(BTN_LEFT); tap(BTN_LEFT); CHECK(machine.patch == N_PATCHES - 1, "LEFT wraps to the last patch"); machine.loadPatch(0);
    tap(BTN_DOWN); tap(BTN_RIGHT); CHECK(machine.selPat == 1, "pattern 2"); tap(BTN_LEFT); tap(BTN_LEFT); CHECK(machine.selPat == 15, "wraps to 16"); tap(BTN_RIGHT);
    tap(BTN_DOWN); tap(BTN_LEFT); tap(BTN_LEFT); CHECK(machine.patLen[0] == 14, "length 14"); for (int i = 0; i < 30; i++) { tap(BTN_LEFT); } CHECK(machine.patLen[0] == 1, "stops at 1"); for (int i = 0; i < 30; i++) { tap(BTN_RIGHT); } CHECK(machine.patLen[0] == 16, "stops at 16");
    tap(BTN_DOWN); tap(BTN_RIGHT); CHECK(machine.extSync, "SYNC toggles to MIDI CLOCK"); tap(BTN_PRESS); CHECK(!machine.extSync, "PRESS toggles too");
    tap(BTN_DOWN); tap(BTN_RIGHT); CHECK(machine.keyMode == 0, "KEYS toggles to PLAY OVER"); tap(BTN_RIGHT); CHECK(machine.keyMode == 1, "...and back");
    tap(BTN_DOWN); tap(BTN_RIGHT); CHECK(machine.stepWrite, "STEP-WRITE toggles on"); tap(BTN_RIGHT); CHECK(!machine.stepWrite, "...off");
    settle(); CHECK(diffPixels() == 0, "panel == framebuffer");
    tap(BTN_DOWN); CHECK(_pr == R_CLEAR, "clear row"); machine.setStep(2, 50, F_GATE); tap(BTN_PRESS); CHECK(machine.stepFlg[0][2] == F_GATE, "first PRESS only arms"); settle(); idle(2100); tap(BTN_PRESS); CHECK(machine.stepFlg[0][2] == F_GATE, "a press after 2 s only re-arms"); tap(BTN_PRESS); CHECK(machine.stepFlg[0][2] == 0, "second PRESS within 2 s clears");
    tap(BTN_DOWN); CHECK(_pr == R_SAVE, "save row"); g_saves = 0; g_storage_ok = true; tap(BTN_PRESS); CHECK(g_saves == 1 && !strcmp(_msg, "SAVED"), "SAVE calls the storage (%s)", _msg);
    g_storage_ok = false; tap(BTN_PRESS); CHECK(!strcmp(_msg, "SAVE FAILED"), "failure is reported (%s)", _msg);
    machine.play(); run(100); g_saves = 0; tap(BTN_PRESS); CHECK(g_saves == 0 && !strcmp(_msg, "STOP FIRST"), "no save while playing (%s)", _msg); machine.stop(); run(50); g_storage_ok = true;
    tap(BTN_DOWN); CHECK(_pr == R_PATCH, "DOWN wraps to the first row"); tap(BTN_UP); CHECK(_pr == R_SAVE, "UP wraps to the last"); settle(); CHECK(diffPixels() == 0, "panel == framebuffer");

    printf("\n== buttons A and B ==\n");
    releaseAll(); idle(50); ui_handle_buttons(machine);
    tap(BTN_A); run(50); CHECK(machine.playing == 1, "A tap = play"); tap(BTN_A); run(50); CHECK(machine.playing == 0, "A tap = stop");
    press(BTN_A, true); idle(10); ui_handle_buttons(machine); idle(650); ui_handle_buttons(machine); CHECK(machine.stepWrite, "A held 0.6 s = step-write ON"); press(BTN_A, false); idle(10); ui_handle_buttons(machine); run(50);
    CHECK(machine.playing == 0, "the release after a long press does NOT toggle play");
    press(BTN_A, true); idle(10); ui_handle_buttons(machine); idle(650); ui_handle_buttons(machine); press(BTN_A, false); idle(10); ui_handle_buttons(machine); CHECK(!machine.stepWrite, "A held again = step-write OFF");
    int pg0 = _page; tap(BTN_B); CHECK(_page == (pg0 + 1) % PAGE_COUNT, "B = next page"); tap(BTN_B); tap(BTN_B); tap(BTN_B); CHECK(_page == pg0, "four pages, B wraps");

    printf("\n== page change repaints once, completely; worst single flush ==\n");
    for (int p : {PG_OSC, PG_FILT, PG_SEQ, PG_SETUP}) {
        gotoPage(p); ui_mark_all_dirty(); uint64_t worst = 0;
        for (int k = 0; k < 600; k++) { idle(25); SPI1.bytes = 0; ui_flush(machine); if (SPI1.bytes > worst) worst = SPI1.bytes; }
        printf("  page %d: largest rectangle per ui_flush() call %llu bytes = %.2f ms at 7.5 MB/s\n", p, (unsigned long long)worst, worst / 7500.0);
        CHECK(worst <= 8700, "no call may send more than the top bar"); CHECK(diffPixels() == 0, "page %d painted completely", p);
    }
    for (int p : {PG_OSC, PG_FILT, PG_SEQ, PG_SETUP}) { tap(BTN_B); idle(30); SPI1.calls = SPI1.bytes = 0; ui_flush(machine); CHECK(SPI1.bytes == 64811, "page change repaints the whole screen once (%llu bytes)", (unsigned long long)SPI1.bytes); settle(); CHECK(_page == p, "page %d", p); }

    printf("\n== status lines, top bar ==\n");
    gotoPage(PG_OSC); machine.stop(); run(50); machine.allNotesOff(); run(40000); machine.keyMode = 0; settle();
    { int x, y, w, h; widgetRect(W_K0 + _kcount[0], x, y, w, h); CHECK(x == 0 && y == cellY(2) + 6, "status line 1 below the knobs"); const int yy = y + 2;
      bool grn = false; for (int dx = 0; dx < 230; dx++) for (int dy = 0; dy < 7; dy++) if (scr[(yy + dy) * LCD_W + dx] == C_GREEN) grn = true; CHECK(!grn, "no green note text while idle");
      machine.noteOn(40, 100); run(200); settle(); grn = false; for (int dx = 0; dx < 230; dx++) for (int dy = 0; dy < 7; dy++) if (scr[(yy + dy) * LCD_W + dx] == C_GREEN) grn = true; CHECK(grn, "the note name turns green while the gate is on");
      bool top = false; for (int dx = 150; dx < 175; dx++) for (int dy = 5; dy < 12; dy++) if (scr[dy * LCD_W + dx] == C_GREEN) top = true; CHECK(top, "...and the top bar shows it too"); machine.noteOff(40); run(40000); }
    machine.stop(); run(50); settle(); CHECK(scr[8 * LCD_W + 180] == C_GRAY, "stop square when stopped"); machine.play(); run(100); settle(); CHECK(scr[8 * LCD_W + 180] == C_GREEN, "play triangle when playing"); machine.stop(); run(50);
    machine.extSync = true; settle(); { bool ext = false; for (int x = 190; x < 208; x++) for (int y = 5; y < 12; y++) if (scr[y * LCD_W + x] == C_CYAN) ext = true; CHECK(ext, "EXT shown with external sync"); } machine.extSync = false;
    machine.stepWrite = true; settle(); { bool w = false; for (int x = 210; x < 217; x++) for (int y = 5; y < 12; y++) if (scr[y * LCD_W + x] == C_RED) w = true; CHECK(w, "red W while step-write is on"); } machine.stepWrite = false;
    ui_midi_activity(); idle(30); ui_flush(machine); settle(); const uint16_t onPix = scr[5 * LCD_W + 231]; idle(300); settle(); const uint16_t offPix = scr[5 * LCD_W + 231]; CHECK(onPix == C_GREEN && offPix == C_GREENDIM, "MIDI dot dims by itself");
    for (int cpu : {10, 60, 90}) { g_cpu_load_pct = cpu; idle(30); ui_flush(machine); settle(); } CHECK(scr[7 * LCD_W + 220] == C_RED, "90%% -> red"); g_cpu_load_pct = 60; settle(); CHECK(scr[7 * LCD_W + 220] == C_ORANGE, "60%% -> orange"); g_cpu_load_pct = 5; settle(); CHECK(scr[7 * LCD_W + 220] == C_GREEN, "5%% -> green");
    machine.setPattern(0); machine.play(); run(100); machine.setPattern(3); settle(); { bool amber = false; for (int x = 4; x < 30; x++) for (int y = 5; y < 12; y++) if (scr[y * LCD_W + x] == C_AMBER) amber = true; CHECK(amber, "pattern label turns amber while another pattern is queued"); } machine.stop(); run(50); machine.setPattern(0);

    printf("\n== random: panel must always equal the framebuffer (all pages, all inputs, machine running) ==\n");
    srand(9); int bad = 0; machine.loadDemo(); machine.play(); machine.keyMode = 0;
    for (int it = 0; it < 1500; it++) {
        switch (rand() % 14) {
            case 0: tap(BTN_B); break;
            case 1: tap(BTN_LEFT); break; case 2: tap(BTN_RIGHT); break; case 3: tap(BTN_UP); break; case 4: tap(BTN_DOWN); break;
            case 5: tap(BTN_PRESS); break;
            case 6: machine.noteOn(30 + rand() % 40, 1 + rand() % 127); break;
            case 7: machine.noteOff(30 + rand() % 40); break;
            case 8: machine.setParam(rand() % N_PARAMS, rand() % 128); break;
            case 9: if (rand() % 6 == 0) tap(BTN_A); break;
            case 10: machine.setBpm(60 + rand() % 150); break;
            case 11: machine.setPattern(rand() % 16); break;
            case 12: machine.pitchBend((int16_t)(rand() % 16384 - 8192)); break;
            default: machine.cycleFlags(rand() % 16); break;
        }
        g_storage_ok = true;
        for (int k = 0; k < (rand() % 5); k++) { idle(rand() % 40); ui_flush(machine); }
        run(rand() % 3000);
        if (rand() % 5 == 0) { settle(); if (diffPixels()) { bad++; printf("   mismatch %d px at iteration %d (page %d)\n", diffPixels(), it, _page); } }
    }
    CHECK(bad == 0, "%d inconsistencies between panel and framebuffer", bad);

    printf("\n== contact bounce ==\n");
    machine.stop(); run(50); gotoPage(PG_OSC); _ksel[0] = P_MIX1; machine.setParam(P_MIX1, 50); releaseAll(); idle(20); ui_handle_buttons(machine);
    press(BTN_UP, true); idle(1); ui_handle_buttons(machine); press(BTN_UP, false); idle(1); ui_handle_buttons(machine); press(BTN_UP, true); idle(3); ui_handle_buttons(machine); press(BTN_UP, false); idle(10); ui_handle_buttons(machine);
    CHECK(machine.pv[P_MIX1] <= 52, "3 ms contact bounce moved mix 50 -> %d (one step = 52)", machine.pv[P_MIX1]);
    printf("\nLCD TESTS: %s (%d failures)\n", fails ? "FAIL" : "PASS", fails);
    return fails ? 1 : 0;
}
