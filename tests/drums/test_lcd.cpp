// LCD tests for the drum machine: emulate the ST7789 behind SPI1 and check what the panel would actually show.
#include <Arduino.h>
#include <SPI.h>
#include <vector>
#include <cstdint>
#include "drum_manager.h"
volatile uint8_t g_cpu_load_pct = 0;
DrumManager machine;
static bool g_storage_ok = true; static int g_saves = 0;
bool storage_save(DrumManager&) { g_saves++; return g_storage_ok; }
bool storage_load(DrumManager&) { return g_storage_ok; }
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
static uint16_t cellPx(int track, int step, int dx, int dy) { return scr[(ROWS_Y + (track & 7) * 13 + dy) * LCD_W + GRID_X0 + step * GRID_PITCH + dx]; }

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
    CHECK(_page == PG_GRID, "starts on the grid page");

    printf("\n== GRID page: cell colours ==\n");
    machine.setPattern(0); settle();
    // pattern 0 (House): BD 3...2...2...2...  -> accent, off, off, off, normal
    CHECK(cellPx(0, 0, 6, 6) == STEP_ON[0] && cellPx(0, 0, 5, 2) == C_WHITE, "BD step 1 = accent (group colour + white notch): 0x%04X", cellPx(0, 0, 6, 6));
    CHECK(cellPx(0, 1, 6, 6) == STEP_OFF[0], "BD step 2 off = dark group colour");
    CHECK(cellPx(0, 4, 6, 6) == STEP_ON[1] && cellPx(0, 4, 5, 2) == STEP_ON[1], "BD step 5 = normal, orange group, no notch");
    CHECK(cellPx(8 - 8 + 0, 12, 6, 6) == STEP_ON[3], "BD step 13 is in the white group");
    machine.setStep(0, 2, 1); settle(); CHECK(cellPx(0, 2, 6, 6) == dimc(STEP_ON[0], 112), "ghost = dimmed group colour (0x%04X)", cellPx(0, 2, 6, 6));
    machine.setLength(12); settle(); CHECK(cellPx(0, 13, 5, 5) == C_KRIM && cellPx(0, 13, 1, 1) == C_BG, "steps beyond the pattern length are blanked");
    machine.setLength(16); settle();
    CHECK(diffPixels() == 0, "panel == framebuffer");

    printf("\n== GRID page: cursor and editing ==\n");
    _cs = 0; _ct = 0; releaseAll(); idle(50); ui_handle_buttons(machine); settle();
    tap(BTN_RIGHT); CHECK(_cs == 1, "RIGHT moves the step cursor"); tap(BTN_LEFT); tap(BTN_LEFT); CHECK(_cs == 15, "LEFT wraps to step 16");
    tap(BTN_RIGHT); tap(BTN_DOWN); CHECK(_ct == 1 && machine.sel == 1, "DOWN moves to track 2 and SELECTS that instrument (sel=%d)", machine.sel);
    tap(BTN_UP); tap(BTN_UP); CHECK(_ct == 15, "UP wraps to track 16"); CHECK(machine.sel == 15, "...and selects it");
    settle(); CHECK(cellPx(0, 0, 6, 6) != 0 && _ct >> 3 == 1, "cursor in the second bank shows tracks 9-16");
    { const uint8_t lbl = scr[(ROWS_Y + 3) * LCD_W + 4]; (void)lbl; }
    _ct = 8; _cs = 5; machine.setStep(8, 5, 0); machine.selectInst(8); settle();
    tap(BTN_PRESS); CHECK(machine.pat[machine.selPat][8][5] == 2, "PRESS: off -> normal"); tap(BTN_PRESS); CHECK(machine.pat[machine.selPat][8][5] == 3, "-> accent");
    tap(BTN_PRESS); CHECK(machine.pat[machine.selPat][8][5] == 1, "-> ghost"); tap(BTN_PRESS); CHECK(machine.pat[machine.selPat][8][5] == 0, "-> off");
    tap(BTN_PRESS); settle(); CHECK(cellPx(8, 5, 6, 6) == STEP_ON[1], "the edit is on the panel immediately");
    printf("  cursor frame pixel: 0x%04X\n", scr[(ROWS_Y + 0 * 13) * LCD_W + GRID_X0 + 5 * GRID_PITCH - 1]);
    CHECK(scr[(ROWS_Y + 0 * 13) * LCD_W + GRID_X0 + 5 * GRID_PITCH - 1] == C_WHITE, "white cursor frame around the cell");
    CHECK(diffPixels() == 0, "panel == framebuffer");

    printf("\n== a pad hit moves the cursor and the selection ==\n");
    machine.padHit(PAD_BASE_NOTE + 3, 100); idle(10); ui_handle_buttons(machine); settle();
    CHECK(machine.sel == 3 && _ct == 3, "pad 4 -> instrument %d, grid cursor on track %d", machine.sel, _ct);
    CHECK(_ct >> 3 == 0, "...bank 1");

    printf("\n== playhead and traffic ==\n");
    machine.setPattern(0); machine.play(); run(100); settle();
    CHECK(machine.playing == 1, "playing"); settle();
    SPI1.calls = SPI1.bytes = 0; int steps_seen = 0; uint8_t last = machine.stepNow;
    for (int i = 0; i < 40; i++) { run(5512 / 4); idle(125 / 4); ui_flush(machine); settle(); if (machine.stepNow != last) { last = machine.stepNow; steps_seen++; } }
    printf("  %d step changes: %llu SPI calls, %llu bytes (~%llu bytes per step)\n", steps_seen, (unsigned long long)SPI1.calls, (unsigned long long)SPI1.bytes, (unsigned long long)(SPI1.bytes / (steps_seen ? steps_seen : 1)));
    CHECK(steps_seen >= 8, "the sequencer advanced");
    const int hx = GRID_X0 + machine.stepNow * GRID_PITCH + 5; CHECK(scr[(HEAD_Y + 2) * LCD_W + hx] == C_WHITE, "white playhead on the current step (step %d)", machine.stepNow);
    CHECK(diffPixels() == 0, "panel == framebuffer while playing");
    machine.setPattern(1); settle(); CHECK(scr[(HEAD_Y + 2) * LCD_W + hx] != C_WHITE, "no playhead while the displayed pattern is not the playing one");
    machine.setPattern(0); settle();
    machine.stop(); run(50); settle();
    SPI1.calls = SPI1.bytes = 0; for (int i = 0; i < 200; i++) { idle(1); ui_flush(machine); }
    CHECK(SPI1.bytes == 0, "idle panel must not transmit (%llu bytes)", (unsigned long long)SPI1.bytes);

    printf("\n== buttons A and B ==\n");
    releaseAll(); idle(50); ui_handle_buttons(machine);
    tap(BTN_A); run(50); CHECK(machine.playing == 1, "A tap = play"); tap(BTN_A); run(50); CHECK(machine.playing == 0, "A tap = stop");
    press(BTN_A, true); idle(10); ui_handle_buttons(machine); idle(650); ui_handle_buttons(machine);
    CHECK(machine.recording, "A held 0.6 s = record ON"); press(BTN_A, false); idle(10); ui_handle_buttons(machine); run(50);
    CHECK(machine.playing == 0, "the release after a long press does NOT toggle play");
    press(BTN_A, true); idle(10); ui_handle_buttons(machine); idle(650); ui_handle_buttons(machine); press(BTN_A, false); idle(10); ui_handle_buttons(machine);
    CHECK(!machine.recording, "A held again = record OFF");
    int pg0 = _page; tap(BTN_B); CHECK(_page == (pg0 + 1) % PAGE_COUNT, "B = next page"); tap(BTN_B); tap(BTN_B); tap(BTN_B); CHECK(_page == pg0, "four pages, B wraps");
    machine.setRecord(true); settle(); machine.setRecord(false); settle();

    printf("\n== page change repaints once, completely ==\n");
    for (int p : {PG_INST, PG_PATT, PG_SONG, PG_GRID}) {
        tap(BTN_B); idle(30); SPI1.calls = SPI1.bytes = 0; ui_flush(machine); const uint64_t b = SPI1.bytes; settle();
        printf("  page %d: first flush %llu bytes\n", _page, (unsigned long long)b);
        CHECK(_page == p && diffPixels() == 0, "page %d painted completely (%d px differ)", p, diffPixels());
    }

    printf("\n== worst single flush call (per page) ==\n");
    for (int p : {PG_GRID, PG_INST, PG_PATT, PG_SONG}) {
        gotoPage(p); uint64_t worst = 0; ui_mark_all_dirty();
        for (int k = 0; k < 600; k++) { idle(25); SPI1.bytes = 0; ui_flush(machine); if (SPI1.bytes > worst) worst = SPI1.bytes; }
        printf("  page %d: largest rectangle per ui_flush() call %llu bytes = %.2f ms at 7.5 MB/s\n", p, (unsigned long long)worst, worst / 7500.0);
        CHECK(worst <= 8700, "no call may send more than the top bar");
    }
    gotoPage(PG_GRID);

    printf("\n== INST page ==\n");
    gotoPage(PG_INST); machine.selectInst(0); _sel = P_LEVEL; settle();
    CHECK(PARAM_INFO[P_INST].col == 0 && PARAM_INFO[P_PAN].row == 1, "knob layout table");
    tap(BTN_RIGHT); CHECK(_sel == P_TUNE, "RIGHT selects the next knob"); _sel = 0; tap(BTN_LEFT); CHECK(_sel == N_KNOBS - 1, "LEFT wraps");
    _sel = P_TONE; const int t0 = machine.pv[P_TONE]; tap(BTN_UP); CHECK(machine.pv[P_TONE] == t0 + 2, "UP = +2"); tap(BTN_DOWN); tap(BTN_DOWN); CHECK(machine.pv[P_TONE] == t0 - 2, "DOWN = -2 twice");
    CHECK(machine.kit[0][3] == machine.pv[P_TONE] && machine.kit[1][3] == KIT[1][3], "the knob edits the SELECTED instrument only");
    _sel = P_INST; machine.selectInst(0); tap(BTN_UP); CHECK(machine.sel == 1, "INST knob UP selects instrument 2 (sel=%d)", machine.sel);
    _sel = P_TEMPO; machine.setBpm(120); tap(BTN_UP); CHECK(machine.bpm == 121.0f, "TEMPO moves in whole BPM (%.1f)", machine.bpm);
    machine.setBpm(100.5f); tap(BTN_DOWN); CHECK(machine.bpm == 100.0f || machine.bpm == 99.0f, "tempo from a fractional BPM lands on a whole one (%.1f)", machine.bpm);
    machine.setParam(P_TONE, 0); _sel = P_TONE; press(BTN_UP, true); int v0 = 0, st = 0;
    for (int ms = 0; ms < 2500; ms += 5) { idle(5); ui_handle_buttons(machine); if (machine.pv[P_TONE] != v0) { v0 = machine.pv[P_TONE]; st++; } if (v0 == 127) break; }
    press(BTN_UP, false); idle(10); ui_handle_buttons(machine);
    printf("  holding UP: tone 0 -> %d in %d steps\n", machine.pv[P_TONE], st); CHECK(machine.pv[P_TONE] == 127, "hold-to-repeat reaches max");
    machine.selectInst(5); run(8000); tap(BTN_PRESS); run(100); CHECK(machine.voiceLive(5), "PRESS auditions the selected instrument");
    settle();
    run(20000); settle(); CHECK(diffPixels() == 0, "panel == framebuffer");
    { char b[16]; machine.setBpm(123.4f); valueText(machine, P_TEMPO, b, sizeof b); CHECK(!strcmp(b, "123"), "tempo text %s", b);
      machine.setParam(P_SWING, 127); valueText(machine, P_SWING, b, sizeof b); CHECK(!strcmp(b, "75%"), "swing text %s", b);
      machine.setParam(P_SWING, 0); valueText(machine, P_SWING, b, sizeof b); CHECK(!strcmp(b, "50%"), "swing text %s", b);
      machine.selectInst(0); machine.setParam(P_PAN, 64); valueText(machine, P_PAN, b, sizeof b); CHECK(!strcmp(b, "C"), "pan centre %s", b);
      machine.setParam(P_PAN, 0); valueText(machine, P_PAN, b, sizeof b); CHECK(!strcmp(b, "L100"), "pan left %s", b);
      machine.setParam(P_PAN, 127); valueText(machine, P_PAN, b, sizeof b); CHECK(!strcmp(b, "R100"), "pan right %s", b);
      machine.selectInst(9); valueText(machine, P_INST, b, sizeof b); CHECK(!strcmp(b, "OH"), "instrument text %s", b); machine.selectInst(0); }
    // live pips
    machine.panic(); run(20000); settle();
    const int px0 = cellX(4) + 3 + 2, py0 = cellY(1) + 25 + 13 + 2;
    CHECK(scr[py0 * LCD_W + px0] == C_KBKG, "pip 0 dark when nothing sounds");
    machine.hit(0, 100); machine.hit(9, 100); run(200); settle();
    CHECK(scr[py0 * LCD_W + px0] == C_GREEN && scr[py0 * LCD_W + px0 + 9 * 7] == C_GREEN && scr[py0 * LCD_W + px0 + 7] == C_KBKG, "pips 0 and 9 lit, 1 dark");

    printf("\n== PATTERN page ==\n");
    gotoPage(PG_PATT); machine.stop(); run(50); machine.setPattern(0); machine.setLength(16); machine.songMode = false; machine.extSync = false; machine.padGm = false; _pr = 0; settle();
    tap(BTN_RIGHT); CHECK(machine.selPat == 1, "RIGHT: pattern 2"); tap(BTN_LEFT); tap(BTN_LEFT); CHECK(machine.selPat == 15, "LEFT wraps to pattern 16"); tap(BTN_RIGHT);
    tap(BTN_DOWN); CHECK(_pr == R_LENGTH, "DOWN: length row"); tap(BTN_LEFT); tap(BTN_LEFT); CHECK(machine.patLen[0] == 14, "LEFT x2: length 14");
    for (int i = 0; i < 30; i++) tap(BTN_LEFT); CHECK(machine.patLen[0] == 1, "length stops at 1"); for (int i = 0; i < 30; i++) tap(BTN_RIGHT); CHECK(machine.patLen[0] == 16, "length stops at 16");
    tap(BTN_DOWN); tap(BTN_RIGHT); CHECK(machine.songMode, "PLAY MODE toggles to SONG"); tap(BTN_LEFT); CHECK(!machine.songMode, "...and back");
    tap(BTN_DOWN); tap(BTN_RIGHT); CHECK(machine.extSync, "SYNC toggles to MIDI CLOCK"); tap(BTN_PRESS); CHECK(!machine.extSync, "PRESS toggles too");
    tap(BTN_DOWN); tap(BTN_RIGHT); CHECK(machine.padGm, "PAD MAP toggles to GM"); tap(BTN_RIGHT);
    settle(); CHECK(diffPixels() == 0, "panel == framebuffer");
    tap(BTN_DOWN); CHECK(_pr == R_COPY, "copy row"); machine.setStep(3, 3, 3); machine.setLength(7); tap(BTN_PRESS);
    CHECK(machine.pat[1][3][3] == 3 && machine.patLen[1] == 7, "COPY copies the pattern to the next one, with its length"); machine.setLength(16);
    tap(BTN_DOWN); CHECK(_pr == R_CLEAR, "clear row"); tap(BTN_PRESS); CHECK(machine.pat[0][3][3] == 3, "first PRESS only arms");
    settle(); idle(2100); tap(BTN_PRESS); CHECK(machine.pat[0][3][3] == 3, "a press after 2 s only re-arms"); tap(BTN_PRESS); CHECK(machine.pat[0][3][3] == 0, "second PRESS within 2 s clears the pattern");
    tap(BTN_DOWN); CHECK(_pr == R_SAVE, "save row"); g_saves = 0; g_storage_ok = true; tap(BTN_PRESS); CHECK(g_saves == 1 && !strcmp(_msg, "SAVED"), "SAVE calls the storage (%s)", _msg);
    g_storage_ok = false; tap(BTN_PRESS); CHECK(!strcmp(_msg, "SAVE FAILED"), "failure is reported (%s)", _msg);
    machine.play(); run(100); g_saves = 0; tap(BTN_PRESS); CHECK(g_saves == 0 && !strcmp(_msg, "STOP FIRST"), "no save while playing: flash writes would stall the audio (%s)", _msg);
    machine.stop(); run(50); g_storage_ok = true; settle(); CHECK(diffPixels() == 0, "panel == framebuffer");
    tap(BTN_DOWN); CHECK(_pr == R_PATTERN, "DOWN wraps to the first row"); tap(BTN_UP); CHECK(_pr == R_SAVE, "UP wraps to the last");

    printf("\n== SONG page ==\n");
    gotoPage(PG_SONG); machine.loadDemo(); _ss = 0; settle();
    CHECK(machine.song[0] == 0 && machine.song[8] == 0xFF, "demo song loaded"); tap(BTN_UP); CHECK(machine.song[0] == 1, "UP: pattern 2 in slot 1");
    tap(BTN_DOWN); tap(BTN_DOWN); CHECK(machine.song[0] == 0xFF, "DOWN twice: slot empty"); tap(BTN_DOWN); CHECK(machine.song[0] == 0xFF, "stays empty");
    tap(BTN_UP); CHECK(machine.song[0] == 0, "UP from empty = pattern 1");
    for (int i = 0; i < 30; i++) tap(BTN_UP); CHECK(machine.song[0] == 15, "stops at pattern 16"); machine.song[0] = 0;
    tap(BTN_RIGHT); CHECK(_ss == 1, "RIGHT: slot 2"); tap(BTN_LEFT); tap(BTN_LEFT); CHECK(_ss == SONG_MAX - 1, "LEFT wraps to slot 32");
    _ss = 20; tap(BTN_UP); settle(); CHECK(machine.song[20] == 0, "slot 21 on the second row is editable"); machine.song[20] = 0xFF;
    machine.songMode = false; tap(BTN_PRESS); CHECK(machine.songMode, "PRESS toggles song mode"); tap(BTN_PRESS); CHECK(!machine.songMode, "...off");
    settle(); CHECK(diffPixels() == 0, "panel == framebuffer");
    { machine.songMode = true; machine.play(); run(200); settle();
      const int sx = SLOT_X0 + 0 * GRID_PITCH + 5, sy = SONG_ROW_Y(0) + 16; CHECK(scr[sy * LCD_W + sx] == C_WHITE, "the playing slot is underlined"); machine.stop(); run(50); machine.songMode = false; }

    printf("\n== top bar: transport icons, EXT, CPU square, MIDI dot ==\n");
    gotoPage(PG_GRID); machine.stop(); run(50); machine.setRecord(false); machine.extSync = false; settle();
    CHECK(scr[8 * LCD_W + 170] == C_GRAY, "stop square when stopped"); machine.play(); run(100); settle(); CHECK(scr[8 * LCD_W + 170] == C_GREEN, "play triangle when playing");
    CHECK(scr[8 * LCD_W + 186] == C_BG || scr[8 * LCD_W + 186] == C_TOPBG, "no record dot"); machine.setRecord(true); settle(); CHECK(scr[8 * LCD_W + 186] == C_RED, "red record disc");
    machine.setRecord(false); machine.stop(); run(50);
    machine.extSync = true; settle(); { bool ext = false; for (int x = 194; x < 214; x++) for (int y = 5; y < 12; y++) if (scr[y * LCD_W + x] == C_CYAN) ext = true; CHECK(ext, "EXT shown with external sync"); } machine.extSync = false;
    ui_midi_activity(); idle(30); ui_flush(machine); settle(); const uint16_t onPix = scr[5 * LCD_W + 231]; idle(300); settle(); const uint16_t offPix = scr[5 * LCD_W + 231];
    CHECK(onPix == C_GREEN && offPix == C_GREENDIM, "MIDI dot must dim by itself (0x%04X -> 0x%04X)", onPix, offPix);
    for (int cpu : {10, 60, 90}) { g_cpu_load_pct = cpu; idle(30); ui_flush(machine); settle(); }
    CHECK(scr[7 * LCD_W + 220] == C_RED, "90%% -> red"); g_cpu_load_pct = 60; settle(); CHECK(scr[7 * LCD_W + 220] == C_ORANGE, "60%% -> orange"); g_cpu_load_pct = 5; settle(); CHECK(scr[7 * LCD_W + 220] == C_GREEN, "5%% -> green");
    machine.setPattern(2); machine.play(); run(100); machine.setPattern(3); settle();
    { bool amber = false; for (int x = 4; x < 40; x++) for (int y = 5; y < 12; y++) if (scr[y * LCD_W + x] == C_AMBER) amber = true; CHECK(amber, "pattern label turns amber while another pattern is queued"); }
    machine.stop(); run(50); machine.setPattern(0);

    printf("\n== random: panel must always equal the framebuffer (all pages, all inputs, machine running) ==\n");
    srand(9); int bad = 0; machine.loadDemo(); machine.play();
    for (int it = 0; it < 1500; it++) {
        switch (rand() % 14) {
            case 0: tap(BTN_B); break;
            case 1: tap(BTN_LEFT); break; case 2: tap(BTN_RIGHT); break; case 3: tap(BTN_UP); break; case 4: tap(BTN_DOWN); break;
            case 5: tap(BTN_PRESS); break;
            case 6: machine.padHit(PAD_BASE_NOTE + rand() % 16, 1 + rand() % 127); break;
            case 7: machine.setParam(rand() % N_PARAMS, rand() % 128); break;
            case 8: if (rand() % 6 == 0) tap(BTN_A); break;
            case 9: machine.setBpm(60 + rand() % 150); break;
            case 10: machine.setPattern(rand() % 16); break;
            case 11: machine.setRecord(rand() & 1); break;
            case 12: ui_midi_activity(); break;
            default: machine.cycleStep(rand() % 16, rand() % 16); break;
        }
        machine.service(); g_storage_ok = true;
        for (int k = 0; k < (rand() % 5); k++) { idle(rand() % 40); ui_flush(machine); }
        run(rand() % 3000);
        if (rand() % 5 == 0) { settle(); if (diffPixels()) { bad++; printf("   mismatch %d px at iteration %d (page %d)\n", diffPixels(), it, _page); } }
    }
    CHECK(bad == 0, "%d inconsistencies between panel and framebuffer", bad);

    printf("\n== contact bounce ==\n");
    machine.stop(); run(50); gotoPage(PG_INST); _sel = P_TONE; machine.selectInst(0); machine.setParam(P_TONE, 50); releaseAll(); idle(20); ui_handle_buttons(machine);
    press(BTN_UP, true); idle(1); ui_handle_buttons(machine); press(BTN_UP, false); idle(1); ui_handle_buttons(machine); press(BTN_UP, true); idle(3); ui_handle_buttons(machine); press(BTN_UP, false); idle(10); ui_handle_buttons(machine);
    CHECK(machine.pv[P_TONE] <= 52, "3 ms contact bounce moved tone 50 -> %d (one step = 52)", machine.pv[P_TONE]);

    printf("\nLCD TESTS: %s (%d failures)\n", fails ? "FAIL" : "PASS", fails);
    return fails ? 1 : 0;
}
