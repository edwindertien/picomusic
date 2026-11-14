// ============================================================
//  lcd.cpp  —  4-page drum machine panel, Waveshare Pico-LCD-1.14 (ST7789, 240x135)
//
//  Same renderer as the analog synth and the string machine: every widget remembers what it last drew
//  and is only redrawn when its own value changed; only that rectangle goes to the panel as one bulk SPI
//  transfer; at most ONE rectangle is sent per ui_flush() call so loop() keeps polling USB/MIDI.
//
//  PAGES (button B cycles them)
//    GRID     16 steps x 8 tracks (the cursor's bank: tracks 1-8 or 9-16), coloured in the 808's groups of four.
//             Joystick moves the cursor, PRESS cycles the step  off -> normal -> accent -> ghost -> off.
//             Moving to another track selects that instrument for the knobs; hitting a pad moves the cursor to it.
//    INST     the knob panel for the SELECTED instrument (LVL TUNE DEC TONE PAN) + machine knobs (BPM SWNG VOL DRV)
//             LEFT/RIGHT select a knob, UP/DOWN change it (hold = repeat), PRESS = audition the instrument.
//    PATTERN  pattern number, length, play mode, sync, pad map, copy, clear, save.  UP/DOWN row, LEFT/RIGHT value.
//    SONG     the 32-slot pattern chain.  LEFT/RIGHT slot, UP/DOWN pattern, PRESS = song mode on/off.
//
//  A = play / stop (tap)   A held 0.6 s = record on / off   B = next page
// ============================================================
#include <Arduino.h>
#include <SPI.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "config.h"
#include "lcd.h"
#include "drum_manager.h"

extern volatile uint8_t g_cpu_load_pct;     // measured by core 1 (main.cpp)

// ---- Pins (hard-wired on the Pico-LCD-1.14) ----------------
#define LCD_SCK   10
#define LCD_MOSI  11
#define LCD_CS     9
#define LCD_DC     8
#define LCD_RST   12
#define LCD_BL    13
#define BTN_UP     2
#define BTN_DOWN  18
#define BTN_LEFT  16
#define BTN_RIGHT 20
#define BTN_PRESS  3
#define BTN_A     15
#define BTN_B     17

#define LCD_W   240
#define LCD_H   135

// ---- Layout -------------------------------------------------
#define TOP_H     18
#define GRID_Y0   19
#define ROW_H     38
#define CELL_W    29
#define CELL_H    37
#define KNOB_R    12
static inline int cellX(int col) { return col * CELL_W + (col >= 4 ? 8 : 0); }   // col 7 ends at x=239
static inline int cellY(int row) { return GRID_Y0 + row * ROW_H; }
#define SEP_X     120
#define STAT_COL    5          // status block starts at the column-5 cell of row 2

// ---- Colours (RGB565) -----------------------------------------
#define C_BG       0x1062
#define C_TOPBG    0x2124
#define C_KBKG     0x2945
#define C_KRIM     0x5ACB
#define C_SEP      0x39C7
#define C_GRAY     0x8C71
#define C_WHITE    0xF75A
#define C_CYAN     0xFEC0
#define C_GREEN    0x9FE0
#define C_GREENDIM 0x4C00
#define C_ORANGE   0xFC60
#define C_RED      0xE0A0
#define C_AMBER    0xFD40
// (the group colours live next to the widgets below)

// ---- Framebuffer (native RGB565) + bulk-transfer staging ----------------
static uint16_t FB[LCD_W * LCD_H];
#define TXB_SIZE 4096
static uint8_t  TXB[TXB_SIZE];

static inline void fb_px(int x, int y, uint16_t c) {
    if ((unsigned)x < (unsigned)LCD_W && (unsigned)y < (unsigned)LCD_H) FB[y * LCD_W + x] = c;
}
static void fb_rect(int x, int y, int w, int h, uint16_t c) {
    for (int r = y; r < y + h && r < LCD_H; r++) {
        if (r < 0) continue;
        for (int col = x; col < x + w && col < LCD_W; col++)
            if (col >= 0) FB[r * LCD_W + col] = c;
    }
}
static void fb_disc(int cx, int cy, int r, uint16_t c) {
    for (int dy = -r; dy <= r; dy++)
        for (int dx = -r; dx <= r; dx++)
            if (dx * dx + dy * dy <= r * r) fb_px(cx + dx, cy + dy, c);
}
static void fb_circle(int cx, int cy, int r, uint16_t c) {
    int x = r, y = 0, d = 1 - r;
    while (x >= y) {
        fb_px(cx+x, cy+y, c); fb_px(cx-x, cy+y, c); fb_px(cx+x, cy-y, c); fb_px(cx-x, cy-y, c);
        fb_px(cx+y, cy+x, c); fb_px(cx-y, cy+x, c); fb_px(cx+y, cy-x, c); fb_px(cx-y, cy-x, c);
        if (d <= 0) { d += 2 * y + 3; y++; } else { d += 2 * (y - x) + 5; x--; y++; }
    }
}

// ---- 5x7 font (identical to the organ's lcd.cpp) -------------------------
static const uint8_t FONT[][5] = {
    {0x00,0x00,0x00,0x00,0x00},{0x00,0x00,0x5F,0x00,0x00},{0x00,0x07,0x00,0x07,0x00},
    {0x14,0x7F,0x14,0x7F,0x14},{0x24,0x2A,0x7F,0x2A,0x12},{0x23,0x13,0x08,0x64,0x62},
    {0x36,0x49,0x55,0x22,0x50},{0x00,0x05,0x03,0x00,0x00},{0x00,0x1C,0x22,0x41,0x00},
    {0x00,0x41,0x22,0x1C,0x00},{0x14,0x08,0x3E,0x08,0x14},{0x08,0x08,0x3E,0x08,0x08},
    {0x00,0x50,0x30,0x00,0x00},{0x08,0x08,0x08,0x08,0x08},{0x00,0x60,0x60,0x00,0x00},
    {0x20,0x10,0x08,0x04,0x02},{0x3E,0x51,0x49,0x45,0x3E},{0x00,0x42,0x7F,0x40,0x00},
    {0x42,0x61,0x51,0x49,0x46},{0x21,0x41,0x45,0x4B,0x31},{0x18,0x14,0x12,0x7F,0x10},
    {0x27,0x45,0x45,0x45,0x39},{0x3C,0x4A,0x49,0x49,0x30},{0x01,0x71,0x09,0x05,0x03},
    {0x36,0x49,0x49,0x49,0x36},{0x06,0x49,0x49,0x29,0x1E},{0x00,0x36,0x36,0x00,0x00},
    {0x00,0x56,0x36,0x00,0x00},{0x08,0x14,0x22,0x41,0x00},{0x14,0x14,0x14,0x14,0x14},
    {0x00,0x41,0x22,0x14,0x08},{0x02,0x01,0x51,0x09,0x06},{0x32,0x49,0x79,0x41,0x3E},
    {0x7E,0x11,0x11,0x11,0x7E},{0x7F,0x49,0x49,0x49,0x36},{0x3E,0x41,0x41,0x41,0x22},
    {0x7F,0x41,0x41,0x22,0x1C},{0x7F,0x49,0x49,0x49,0x41},{0x7F,0x09,0x09,0x09,0x01},
    {0x3E,0x41,0x49,0x49,0x7A},{0x7F,0x08,0x08,0x08,0x7F},{0x00,0x41,0x7F,0x41,0x00},
    {0x20,0x40,0x41,0x3F,0x01},{0x7F,0x08,0x14,0x22,0x41},{0x7F,0x40,0x40,0x40,0x40},
    {0x7F,0x02,0x0C,0x02,0x7F},{0x7F,0x04,0x08,0x10,0x7F},{0x3E,0x41,0x41,0x41,0x3E},
    {0x7F,0x09,0x09,0x09,0x06},{0x3E,0x41,0x51,0x21,0x5E},{0x7F,0x09,0x19,0x29,0x46},
    {0x46,0x49,0x49,0x49,0x31},{0x01,0x01,0x7F,0x01,0x01},{0x3F,0x40,0x40,0x40,0x3F},
    {0x1F,0x20,0x40,0x20,0x1F},{0x3F,0x40,0x38,0x40,0x3F},{0x63,0x14,0x08,0x14,0x63},
    {0x07,0x08,0x70,0x08,0x07},{0x61,0x51,0x49,0x45,0x43},{0x00,0x7F,0x41,0x41,0x00},
    {0x02,0x04,0x08,0x10,0x20},{0x00,0x41,0x41,0x7F,0x00},{0x04,0x02,0x01,0x02,0x04},
    {0x40,0x40,0x40,0x40,0x40},{0x00,0x01,0x02,0x04,0x00},{0x20,0x54,0x54,0x54,0x78},
    {0x7F,0x48,0x44,0x44,0x38},{0x38,0x44,0x44,0x44,0x20},{0x38,0x44,0x44,0x48,0x7F},
    {0x38,0x54,0x54,0x54,0x18},{0x08,0x7E,0x09,0x01,0x02},{0x0C,0x52,0x52,0x52,0x3E},
    {0x7F,0x08,0x04,0x04,0x78},{0x00,0x44,0x7D,0x40,0x00},{0x20,0x40,0x44,0x3D,0x00},
    {0x7F,0x10,0x28,0x44,0x00},{0x00,0x41,0x7F,0x40,0x00},{0x7C,0x04,0x18,0x04,0x78},
    {0x7C,0x08,0x04,0x04,0x78},{0x38,0x44,0x44,0x44,0x38},{0x7C,0x14,0x14,0x14,0x08},
    {0x08,0x14,0x14,0x18,0x7C},{0x7C,0x08,0x04,0x04,0x08},{0x48,0x54,0x54,0x54,0x20},
    {0x04,0x3F,0x44,0x40,0x20},{0x3C,0x40,0x40,0x40,0x3C},{0x1C,0x20,0x40,0x20,0x1C},
    {0x3C,0x40,0x30,0x40,0x3C},{0x44,0x28,0x10,0x28,0x44},{0x0C,0x50,0x50,0x50,0x3C},
    {0x44,0x64,0x54,0x4C,0x44},{0x00,0x08,0x36,0x41,0x00},{0x00,0x00,0x7F,0x00,0x00},
    {0x00,0x41,0x36,0x08,0x00},{0x10,0x08,0x08,0x10,0x08},
};

static void fb_char(int x, int y, char c, uint16_t fg, uint16_t bg) {
    if (c < 32 || c > 126) c = '?';
    const uint8_t* g = FONT[c - 32];
    for (int col = 0; col < 5; col++)
        for (int row = 0; row < 7; row++)
            fb_px(x + col, y + row, ((g[col] >> row) & 1) ? fg : bg);
}
static void fb_str(int x, int y, const char* s, uint16_t fg, uint16_t bg) {
    for (; *s; s++, x += 6) fb_char(x, y, *s, fg, bg);
}
static int fb_strw(const char* s) { int n = 0; while (*s++) n++; return n * 6; }

// ---- Knob arc tables (computed once; no sinf/cosf while drawing) ---------
#define ARC_N 109                          // 270 deg in 2.5 deg steps, bottom-left -> bottom-right
static int8_t ARC_DX[ARC_N], ARC_DY[ARC_N];   // radius KNOB_R-2
static int8_t DOT_DX[ARC_N], DOT_DY[ARC_N];   // radius KNOB_R-5
static void initArcTables() {
    for (int k = 0; k < ARC_N; k++) {
        const float th = (-135.0f + 2.5f * (float)k) * (float)M_PI / 180.0f;   // 0 = straight up
        ARC_DX[k] = (int8_t)lroundf((KNOB_R - 2) * sinf(th));
        ARC_DY[k] = (int8_t)(-lroundf((KNOB_R - 2) * cosf(th)));
        DOT_DX[k] = (int8_t)lroundf((KNOB_R - 5) * sinf(th));
        DOT_DY[k] = (int8_t)(-lroundf((KNOB_R - 5) * cosf(th)));
    }
}

// ---- UI state -----------------------------------------------------------
enum { PG_GRID, PG_INST, PG_PATT, PG_SONG };
// widget ids (per page; the top bar is always widget 0)
#define W_TOP     0
#define W_G_HEAD  1                         // grid: playhead strip, then 8 rows
#define W_G_ROW0  2
#define W_I_KNOB0 1                         // inst: 10 knobs, then the status block
#define W_I_STAT0 (1 + N_KNOBS)               // the status block is 3 widgets of 25 rows: no single transfer > 5.8 KB
#define W_I_STAT1 (2 + N_KNOBS)
#define W_I_STAT2 (3 + N_KNOBS)
#define W_P_ROW0  1                         // pattern setup: 8 rows
#define W_S_INFO  1                         // song: info line, two rows of 16 slots
#define W_S_ROW0  2

#define PATT_ROWS 8
enum { R_PATTERN, R_LENGTH, R_MODE, R_SYNC, R_PADS, R_COPY, R_CLEAR, R_SAVE };

static int      _page       = PG_GRID;
static int      _cs = 0, _ct = 0;           // grid cursor: step, track
static int      _sel        = P_LEVEL;      // inst page: selected knob
static int      _pr = 0;                    // pattern page: selected row
static int      _ss = 0;                    // song page: selected slot
static uint8_t  _lastSel    = 0;            // instrument selection last seen (a pad hit moves the grid cursor)
static uint32_t _pending    = 0;            // widgets drawn into FB but not yet sent to the panel
static uint32_t _midi_until = 0;
static bool     _force      = true;         // redraw everything on the next scan
static bool     _first      = true;         // nothing on the panel yet
static bool     _page_dirty = false;        // page changed: repaint everything
static uint32_t _lastScan   = 0;
static uint32_t _clearArm   = 0;            // CLEAR needs a second press within 2 s
static uint32_t _msgUntil   = 0;
static char     _msg[20]    = "";

// ---- Per-widget change keys: plain-old-data snapshots of everything a widget shows; redraw only when different ----
struct TopInfo  { char a[12]; char b[20]; char c[6]; uint8_t play, rec, ext, queued, midi, cpu, grp, pad; };
struct StatInfo { uint8_t pat, step, len, flags, sel; uint16_t live; uint8_t pad[3]; };
struct RowKey   { uint32_t steps; uint8_t cur, live, isCur, len; };
struct PRow     { char label[12]; char value[24]; uint8_t sel; };
struct SongKey  { uint8_t slot[16]; uint8_t cur, play, pad[2]; };
static TopInfo  _topK;
static StatInfo _statK;
static RowKey   _rowK[8];
static uint8_t  _headK;
static uint16_t _knobK[N_KNOBS];
static PRow     _pK[PATT_ROWS];
static char     _infoK[40];
static SongKey  _sK[2];

// ---- Value text -----------------------------------------------------------
static void valueText(const DrumManager& s, int id, char* out, size_t n) {
    const int v = s.pv[id];
    switch (PARAM_INFO[id].fmt) {
        case F_INST: snprintf(out, n, "%s", INST_LABEL[s.sel]); break;
        case F_BPM:  snprintf(out, n, "%.0f", s.bpm); break;
        case F_PCT:  snprintf(out, n, "%d%%", (int)(swingFromKnob(v) * 100.0f + 0.5f)); break;
        case F_PAN:  if (v == 64) snprintf(out, n, "C"); else if (v < 64) snprintf(out, n, "L%d", (64 - v) * 100 / 64); else snprintf(out, n, "R%d", (v - 64) * 100 / 63); break;
        default:     snprintf(out, n, "%d", v); break;
    }
}

static inline uint16_t dimc(uint16_t c, int f256) {                      // scale an RGB565 colour
    const int r = ((c >> 11) & 31) * f256 / 256, g = ((c >> 5) & 63) * f256 / 256, b = (c & 31) * f256 / 256;
    return (uint16_t)((r << 11) | (g << 5) | b);
}
static void fb_frame(int x, int y, int w, int h, uint16_t c) { fb_rect(x, y, w, 1, c); fb_rect(x, y + h - 1, w, 1, c); fb_rect(x, y, 1, h, c); fb_rect(x + w - 1, y, 1, h, c); }

// ---- Geometry ---------------------------------------------------------------
#define GRID_X0   26                         // first step cell; pitch 13 (cell 12 + 1 gap): 16 steps end at x=233
#define GRID_PITCH 13
#define HEAD_Y    19
#define ROWS_Y    24                         // 8 rows x 13 px
#define FOOT_Y    128
#define PROW_Y    19                         // pattern page rows, pitch 14
#define PROW_H    14
#define SONG_ROW_Y(r) (42 + (r) * 28)
#define SLOT_X0   16
static const uint16_t STEP_ON[4] = { 0xF800, 0xFC60, 0xFFE0, 0xFFFF };   // the 808's red / orange / yellow / white groups of four
static const uint16_t STEP_OFF[4] = { 0x3000, 0x3100, 0x3180, 0x2965 };
static const uint16_t GROUP_COL[4] = { 0xF75A /*instrument: cream*/, 0xFC60 /*voice: orange*/, 0xF9A0 /*time: red*/, 0xFFE0 /*output: yellow*/ };   // TR-808 palette

static void widgetRect(int w, int& x, int& y, int& ww, int& hh) {
    x = 0; ww = LCD_W;
    if (w == W_TOP) { y = 0; hh = TOP_H; return; }
    switch (_page) {
        case PG_GRID:
            if (w == W_G_HEAD) { y = HEAD_Y; hh = 5; }
            else               { y = ROWS_Y + (w - W_G_ROW0) * 13; hh = 13; }
            break;
        case PG_INST:
            if (w >= W_I_STAT0) { x = cellX(4); y = cellY(1) + (w - W_I_STAT0) * 25; ww = LCD_W - x; hh = 25; }
            else { const ParamInfo& pi = PARAM_INFO[w - W_I_KNOB0]; x = cellX(pi.col); y = cellY(pi.row); ww = CELL_W; hh = CELL_H; }
            break;
        case PG_PATT: y = PROW_Y + (w - W_P_ROW0) * PROW_H; hh = PROW_H; break;
        default:
            if (w == W_S_INFO) { y = 20; hh = 14; } else { x = SLOT_X0 - 2; ww = 212; y = SONG_ROW_Y(w - W_S_ROW0) - 1; hh = 19; }   // frame top .. underline, slots only
            break;
    }
}

// ---- The top bar ---------------------------------------------------------------
static void fillTop(const DrumManager& s, TopInfo& t) {
    memset(&t, 0, sizeof t);
    const int pat = s.selPat + 1;
    if (s.songMode && s.playing) snprintf(t.a, sizeof t.a, "S%02d:P%02d", s.songPosNow + 1, s.patNow + 1); else snprintf(t.a, sizeof t.a, "P%02d", pat);
    t.queued = (s.playing && !s.songMode && s.patNow != s.selPat);
    char val[12];
    switch (_page) {
        case PG_GRID: snprintf(t.b, sizeof t.b, "%s %02d", INST_NAME[_ct], _cs + 1); t.grp = G_VOICE; break;
        case PG_INST: valueText(s, _sel, val, sizeof val); snprintf(t.b, sizeof t.b, "%s %s", PARAM_INFO[_sel].name, val); t.grp = PARAM_INFO[_sel].group; break;
        case PG_PATT: snprintf(t.b, sizeof t.b, "Pattern setup"); t.grp = G_TIME; break;
        default:      snprintf(t.b, sizeof t.b, "Song chain %d", s.songLength()); t.grp = G_OUT; break;
    }
    snprintf(t.c, sizeof t.c, "%.0f", s.bpm);
    t.play = s.playing; t.rec = s.recording; t.ext = s.extSync;
    const uint8_t cpu = g_cpu_load_pct; t.cpu = cpu >= 80 ? 2 : cpu >= 50 ? 1 : 0;
    t.midi = millis() < _midi_until;
}
static void drawTop(const TopInfo& t) {
    fb_rect(0, 0, LCD_W, TOP_H, C_TOPBG);
    fb_rect(0, 15, LCD_W, 1, 0xF800); fb_rect(0, 16, LCD_W, 1, 0xFC60); fb_rect(0, 17, LCD_W, 1, 0xFFE0);      // the 808's red / orange / yellow stripes
    fb_str(4, 5, t.a, t.queued ? C_AMBER : C_WHITE, C_TOPBG);
    fb_str(50, 5, t.b, GROUP_COL[t.grp], C_TOPBG);
    fb_str(144, 5, t.c, C_WHITE, C_TOPBG);
    if (t.play) { for (int k = 0; k < 9; k++) fb_rect(168 + k / 2, 4 + k, 8 - k, 1, C_GREEN); fb_rect(168, 4, 2, 9, C_GREEN); }   // play triangle
    else fb_rect(168, 4, 8, 9, C_GRAY);                                                                                         // stop square
    if (t.rec) fb_disc(186, 8, 4, C_RED); else fb_circle(186, 8, 4, C_GRAY);
    if (t.ext) fb_str(194, 5, "EXT", C_CYAN, C_TOPBG);
    fb_rect(218, 5, 7, 7, t.cpu == 2 ? C_RED : t.cpu == 1 ? C_ORANGE : C_GREEN);          // CPU square
    fb_rect(228, 5, 8, 8, t.midi ? C_GREEN : C_GREENDIM);                                  // MIDI dot
    fb_px(228, 5, C_TOPBG);  fb_px(235, 5, C_TOPBG);  fb_px(228, 12, C_TOPBG); fb_px(235, 12, C_TOPBG);
}

// ---- GRID page -----------------------------------------------------------------
static void fillRowKey(const DrumManager& s, int r, RowKey& k) {
    const int track = (_ct >> 3) * 8 + r;
    memset(&k, 0, sizeof k);
    for (int i = 0; i < N_STEPS; i++) k.steps |= (uint32_t)(s.pat[s.selPat][track][i] & 3) << (2 * i);
    k.isCur = (track == _ct); k.cur = k.isCur ? (uint8_t)_cs : 0xFF; k.live = s.voiceLive(track); k.len = s.patLen[s.selPat];
}
static void drawRow(const DrumManager& s, int r) {
    const int track = (_ct >> 3) * 8 + r, y = ROWS_Y + r * 13;
    const bool cur = (track == _ct);
    fb_rect(0, y, LCD_W, 13, C_BG);
    fb_str(2, y + 3, INST_LABEL[track], s.voiceLive(track) ? C_AMBER : (cur ? C_WHITE : C_GRAY), C_BG);
    if (cur) fb_rect(0, y + 1, 1, 11, C_WHITE);
    const int len = s.patLen[s.selPat];
    for (int st = 0; st < N_STEPS; st++) {
        const int x = GRID_X0 + st * GRID_PITCH, g = st >> 2;
        const uint8_t lv = s.pat[s.selPat][track][st];
        if (st >= len) { fb_rect(x + 5, y + 5, 2, 2, C_KRIM); continue; }               // beyond the pattern length
        fb_rect(x, y + 1, 12, 11, lv == 0 ? STEP_OFF[g] : lv == 1 ? dimc(STEP_ON[g], 112) : STEP_ON[g]);
        if (lv == 3) fb_rect(x + 4, y + 2, 4, 2, C_WHITE);                                // accent mark
        if (cur && st == _cs) fb_frame(x - 1, y, 14, 13, C_WHITE);                        // cursor
    }
}
static void drawHead(const DrumManager& s) {
    fb_rect(0, HEAD_Y, LCD_W, 5, C_BG);
    const bool show = s.playing && s.patNow == s.selPat;
    for (int st = 0; st < N_STEPS; st++) {
        const int x = GRID_X0 + st * GRID_PITCH + 3;
        fb_rect(x, HEAD_Y + 1, 6, 3, (show && st == s.stepNow) ? C_WHITE : ((st & 3) == 0 ? C_KRIM : C_KBKG));
    }
}

// ---- INST page -----------------------------------------------------------------
static void drawKnob(const DrumManager& s, int id, bool sel) {
    const ParamInfo& pi = PARAM_INFO[id];
    const int x0 = cellX(pi.col), y0 = cellY(pi.row), cx = x0 + CELL_W / 2, cy = y0 + 22;
    const uint16_t gc = GROUP_COL[pi.group];
    fb_rect(x0, y0, CELL_W, CELL_H, C_BG);
    fb_str(cx - fb_strw(pi.label) / 2, y0 + 1, pi.label, sel ? C_WHITE : gc, C_BG);
    fb_disc(cx, cy, KNOB_R - 1, C_KBKG);
    const int n = (s.pv[id] * (ARC_N - 1)) / 127;
    for (int k = 0; k < ARC_N; k++) { const uint16_t c = (k <= n) ? gc : C_KRIM; fb_px(cx + ARC_DX[k], cy + ARC_DY[k], c); fb_px(cx + ARC_DX[k] + 1, cy + ARC_DY[k], c); }
    fb_disc(cx + DOT_DX[n], cy + DOT_DY[n], 2, C_WHITE);
    fb_circle(cx, cy, KNOB_R, sel ? C_WHITE : C_KRIM);
    if (sel) fb_circle(cx, cy, KNOB_R - 1, C_WHITE);
}
static void fillStat(const DrumManager& s, StatInfo& t) {
    memset(&t, 0, sizeof t);
    t.pat = s.selPat; t.step = s.playing ? s.stepNow : 0xFF; t.len = s.patLen[s.selPat]; t.sel = s.sel;
    t.flags = (uint8_t)((s.playing ? 1 : 0) | (s.recording ? 2 : 0) | (s.songMode ? 4 : 0) | (s.extSync ? 8 : 0));
    for (int i = 0; i < N_INST; i++) if (s.voiceLive(i)) t.live |= (uint16_t)(1u << i);
}
static void drawStat(const StatInfo& t, int part) {                      // part 0: pattern + step, 1: lamps + live pips, 2: instrument name
    const int x0 = cellX(4), y0 = cellY(1) + part * 25, w = LCD_W - x0;
    char b[32];
    fb_rect(x0, y0, w, 25, C_BG);
    if (part == 0) {
        snprintf(b, sizeof b, "PATTERN %02d  LEN %02d", t.pat + 1, t.len);  fb_str(x0 + 3, y0 + 2, b, C_GRAY, C_BG);
        if (t.step != 0xFF) snprintf(b, sizeof b, "STEP %02d", t.step + 1); else snprintf(b, sizeof b, "STOPPED");
        fb_str(x0 + 3, y0 + 13, b, C_WHITE, C_BG);
    } else if (part == 1) {
        fb_str(x0 + 3,  y0 + 2, "PLAY", (t.flags & 1) ? C_GREEN : C_GRAY, C_BG);
        fb_str(x0 + 31, y0 + 2, "REC",  (t.flags & 2) ? C_RED   : C_GRAY, C_BG);
        fb_str(x0 + 55, y0 + 2, "SONG", (t.flags & 4) ? C_AMBER : C_GRAY, C_BG);
        fb_str(x0 + 85, y0 + 2, "EXT",  (t.flags & 8) ? C_CYAN  : C_GRAY, C_BG);
        for (int i = 0; i < N_INST; i++) fb_rect(x0 + 3 + i * 7, y0 + 13, 5, 5, (t.live >> i) & 1 ? C_GREEN : C_KBKG);   // which instruments sound
        fb_rect(x0 + 3 + t.sel * 7 - 1, y0 + 20, 7, 1, C_WHITE);                                                          // ... and which is selected
    } else {
        fb_str(x0 + 3, y0 + 4, INST_NAME[t.sel], C_WHITE, C_BG);
    }
}

// ---- PATTERN page --------------------------------------------------------------
static void fillPRow(const DrumManager& s, int r, PRow& p) {
    memset(&p, 0, sizeof p);
    static const char* L[PATT_ROWS] = { "PATTERN", "LENGTH", "PLAY MODE", "SYNC", "PAD MAP", "COPY", "CLEAR", "SAVE" };
    snprintf(p.label, sizeof p.label, "%s", L[r]);
    switch (r) {
        case R_PATTERN: snprintf(p.value, sizeof p.value, "%02d", s.selPat + 1); break;
        case R_LENGTH:  snprintf(p.value, sizeof p.value, "%d STEPS", s.patLen[s.selPat]); break;
        case R_MODE:    snprintf(p.value, sizeof p.value, "%s", s.songMode ? "SONG" : "PATTERN"); break;
        case R_SYNC:    snprintf(p.value, sizeof p.value, "%s", s.extSync ? "MIDI CLOCK" : "INTERNAL"); break;
        case R_PADS:    if (s.padGm) snprintf(p.value, sizeof p.value, "GENERAL MIDI"); else snprintf(p.value, sizeof p.value, "LINEAR %d-%d", s.padBase, s.padBase + N_INST - 1); break;
        case R_COPY:    snprintf(p.value, sizeof p.value, "TO %02d  [PRESS]", (s.selPat + 1) % N_PATTERNS + 1); break;
        case R_CLEAR:   snprintf(p.value, sizeof p.value, "%s", millis() < _clearArm ? "PRESS AGAIN!" : "[PRESS]"); break;
        default:        snprintf(p.value, sizeof p.value, "%s", millis() < _msgUntil ? _msg : "[PRESS]"); break;
    }
    p.sel = (r == _pr);
}
static void drawPRow(const PRow& p, int r) {
    const int y = PROW_Y + r * PROW_H;
    const uint16_t bg = p.sel ? C_KBKG : C_BG;
    fb_rect(0, y, LCD_W, PROW_H, bg);
    fb_str(10, y + 4, p.label, p.sel ? C_WHITE : C_GRAY, bg);
    fb_str(110, y + 4, p.value, p.sel ? C_CYAN : C_WHITE, bg);
    if (p.sel) fb_rect(2, y + 3, 3, 8, C_WHITE);
}

// ---- SONG page -----------------------------------------------------------------
static void fillInfo(const DrumManager& s, char* b, size_t n) {
    snprintf(b, n, "%d SLOTS  %s%s", s.songLength(), s.songMode ? "SONG ON" : "song off", (s.songMode && s.playing) ? " >" : "");
}
static void drawInfo(const char* b, bool on) { fb_rect(0, 20, LCD_W, 14, C_BG); fb_str(8, 23, b, on ? C_AMBER : C_GRAY, C_BG); }
static void fillSong(const DrumManager& s, int r, SongKey& k) {
    memset(&k, 0, sizeof k);
    for (int i = 0; i < 16; i++) k.slot[i] = s.song[r * 16 + i];
    k.cur = (_ss >> 4) == r ? (uint8_t)(_ss & 15) : 0xFF;
    k.play = (s.songMode && s.playing && (s.songPosNow >> 4) == r) ? (uint8_t)(s.songPosNow & 15) : 0xFF;
}
static void drawSong(const SongKey& k, int r) {
    const int y = SONG_ROW_Y(r);
    fb_rect(SLOT_X0 - 2, y - 1, 212, 19, C_BG);
    for (int i = 0; i < 16; i++) {
        const int x = SLOT_X0 + i * GRID_PITCH, g = i >> 2;
        const bool has = k.slot[i] != 0xFF;
        fb_rect(x, y, 12, 14, has ? STEP_ON[g] : STEP_OFF[g]);
        char b[4]; if (has) snprintf(b, sizeof b, "%02d", k.slot[i] + 1); else snprintf(b, sizeof b, "--");
        fb_str(x, y + 4, b, has ? 0x0000 : C_GRAY, has ? STEP_ON[g] : STEP_OFF[g]);
        if (k.cur == i) fb_frame(x - 1, y - 1, 14, 16, C_WHITE);
        if (k.play == i) fb_rect(x, y + 16, 12, 2, C_WHITE);
    }
}

static void drawBackground() {
    fb_rect(0, 0, LCD_W, LCD_H, C_BG);
    switch (_page) {
        case PG_GRID:
            for (int g = 0; g < 4; g++) { char b[4]; snprintf(b, sizeof b, "%d", g * 4 + 1); fb_str(GRID_X0 + g * 4 * GRID_PITCH, FOOT_Y, b, STEP_ON[g], C_BG); }
            break;
        case PG_INST:
            for (int y = GRID_Y0; y < LCD_H; y++) fb_px(SEP_X, y, C_SEP);
            for (int r = 0; r < 2; r++) fb_rect(0, cellY(r) + CELL_H, 4 * CELL_W, 1, C_SEP);
            break;
        case PG_SONG:
            fb_str(8, 98, "UP/DOWN  pattern in slot", C_GRAY, C_BG);
            fb_str(8, 108, "LEFT/RIGHT  move slot", C_GRAY, C_BG);
            fb_str(8, 118, "PRESS  song on / off", C_GRAY, C_BG);
            fb_str(8, 128, "slot 1 plays first, loops", C_GRAY, C_BG);
            break;
        default: break;
    }
}

// ---- Change detection: redraw a widget only when what it shows changed --------
static void scan(const DrumManager& s) {
    TopInfo t; fillTop(s, t);
    if (_force || memcmp(&t, &_topK, sizeof t)) { _topK = t; drawTop(t); _pending |= 1u << W_TOP; }
    switch (_page) {
        case PG_GRID: {
            const uint8_t hk = (uint8_t)((s.playing && s.patNow == s.selPat) ? s.stepNow : 0xFF);
            if (_force || hk != _headK) { _headK = hk; drawHead(s); _pending |= 1u << W_G_HEAD; }
            for (int r = 0; r < 8; r++) { RowKey k; fillRowKey(s, r, k); if (_force || memcmp(&k, &_rowK[r], sizeof k)) { _rowK[r] = k; drawRow(s, r); _pending |= 1u << (W_G_ROW0 + r); } }
            break; }
        case PG_INST:
            for (int i = 0; i < N_KNOBS; i++) {
                const uint16_t k = (uint16_t)(s.pv[i] | (i == _sel ? 0x100 : 0));
                if (_force || k != _knobK[i]) { _knobK[i] = k; drawKnob(s, i, i == _sel); _pending |= 1u << (W_I_KNOB0 + i); }
            }
            { StatInfo k; fillStat(s, k);
              if (_force || k.pat != _statK.pat || k.len != _statK.len || k.step != _statK.step) { drawStat(k, 0); _pending |= 1u << W_I_STAT0; }
              if (_force || k.flags != _statK.flags || k.live != _statK.live || k.sel != _statK.sel) { drawStat(k, 1); _pending |= 1u << W_I_STAT1; }
              if (_force || k.sel != _statK.sel) { drawStat(k, 2); _pending |= 1u << W_I_STAT2; }
              _statK = k; }
            break;
        case PG_PATT:
            for (int r = 0; r < PATT_ROWS; r++) { PRow p; fillPRow(s, r, p); if (_force || memcmp(&p, &_pK[r], sizeof p)) { _pK[r] = p; drawPRow(p, r); _pending |= 1u << (W_P_ROW0 + r); } }
            break;
        default: {
            char b[40]; memset(b, 0, sizeof b); fillInfo(s, b, sizeof b);
            if (_force || memcmp(b, _infoK, sizeof b)) { memcpy(_infoK, b, sizeof b); drawInfo(b, s.songMode); _pending |= 1u << W_S_INFO; }
            for (int r = 0; r < 2; r++) { SongKey k; fillSong(s, r, k); if (_force || memcmp(&k, &_sK[r], sizeof k)) { _sK[r] = k; drawSong(k, r); _pending |= 1u << (W_S_ROW0 + r); } }
            break; }
    }
    _force = false;
}

// ---- Panel access (ST7789) -------------------------------------------------
static SPISettings _spi(62500000, MSBFIRST, SPI_MODE0);

static void _cmd(uint8_t c) {
    digitalWrite(LCD_DC, LOW); digitalWrite(LCD_CS, LOW);
    SPI1.transfer(c);
    digitalWrite(LCD_CS, HIGH);
}
static void _dat(uint8_t d) {
    digitalWrite(LCD_DC, HIGH); digitalWrite(LCD_CS, LOW);
    SPI1.transfer(d);
    digitalWrite(LCD_CS, HIGH);
}
static void _win(int x0, int y0, int x1, int y1) {
    x0 += 40; x1 += 40; y0 += 53; y1 += 53;                  // RAM offset of this 240x135 panel
    const uint8_t cx[4] = { (uint8_t)(x0 >> 8), (uint8_t)x0, (uint8_t)(x1 >> 8), (uint8_t)x1 };
    const uint8_t cy[4] = { (uint8_t)(y0 >> 8), (uint8_t)y0, (uint8_t)(y1 >> 8), (uint8_t)y1 };
    _cmd(0x2A);
    digitalWrite(LCD_DC, HIGH); digitalWrite(LCD_CS, LOW); SPI1.transfer(cx, nullptr, 4); digitalWrite(LCD_CS, HIGH);
    _cmd(0x2B);
    digitalWrite(LCD_DC, HIGH); digitalWrite(LCD_CS, LOW); SPI1.transfer(cy, nullptr, 4); digitalWrite(LCD_CS, HIGH);
    _cmd(0x2C);
}

// Send a rectangle of the framebuffer to the panel as bulk transfers.
// Caller holds the SPI transaction.
static void pushRect(int x, int y, int w, int h) {
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > LCD_W) w = LCD_W - x;
    if (y + h > LCD_H) h = LCD_H - y;
    if (w <= 0 || h <= 0) return;

    _win(x, y, x + w - 1, y + h - 1);
    digitalWrite(LCD_DC, HIGH); digitalWrite(LCD_CS, LOW);
    const int rowsPerChunk = TXB_SIZE / (w * 2);
    for (int row = 0; row < h; ) {
        const int n = (h - row < rowsPerChunk) ? (h - row) : rowsPerChunk;
        uint8_t* d = TXB;
        for (int r = 0; r < n; r++) {
            const uint16_t* src = &FB[(y + row + r) * LCD_W + x];
            for (int c = 0; c < w; c++) { const uint16_t px = src[c]; *d++ = (uint8_t)(px >> 8); *d++ = (uint8_t)px; }
        }
        SPI1.transfer(TXB, nullptr, (size_t)(d - TXB));
        row += n;
    }
    digitalWrite(LCD_CS, HIGH);
}

// ---- Public API ---------------------------------------------------------------
void ui_mark_all_dirty() { _force = true; }
void ui_midi_activity()  { _midi_until = millis() + 120; }

static void pageChanged() {                                  // repaint the whole screen for the new page
    _page_dirty = true;
}

void ui_flush(const DrumManager& s) {
    if (_first || _page_dirty) {                             // first call / page change: paint everything once
        _first = false; _page_dirty = false;
        drawBackground();
        _force = true; scan(s);
        SPI1.beginTransaction(_spi);
        pushRect(0, 0, LCD_W, LCD_H);
        SPI1.endTransaction();
        _pending = 0;
        _lastScan = millis();
        return;
    }
    const uint32_t now = millis();
    if (now - _lastScan >= 20) { _lastScan = now; scan(s); }
    if (_pending) {                                          // send ONE rectangle, then return to loop()
        const int w = __builtin_ctz(_pending);
        _pending &= ~(1u << w);
        int x, y, ww, hh;
        widgetRect(w, x, y, ww, hh);
        SPI1.beginTransaction(_spi);
        pushRect(x, y, ww, hh);
        SPI1.endTransaction();
    }
}

// ---- Joystick / buttons ---------------------------------------------------------
static const int BPINS[7] = { BTN_UP, BTN_DOWN, BTN_LEFT, BTN_RIGHT, BTN_PRESS, BTN_A, BTN_B };
enum { B_UP, B_DOWN, B_LEFT, B_RIGHT, B_PRESS, B_A, B_B };
static bool     _bc[7], _bp[7];
static uint32_t _bdown[7], _brep[7], _bms = 0;
static bool     _aArmed = false;                             // A is down and has not yet been used as a long press

// true on the press edge, then (for repeat buttons) every 70 ms after 350 ms
static bool fire(int i, bool repeat, uint32_t now) {
    if (_bc[i] && !_bp[i]) { _bdown[i] = _brep[i] = now; return true; }
    if (repeat && _bc[i] && _bp[i] && now - _bdown[i] > 350 && now - _brep[i] >= 70) { _brep[i] = now; return true; }
    return false;
}
static void setMsg(const char* m) { snprintf(_msg, sizeof _msg, "%s", m); _msgUntil = millis() + 2500; }
static int  clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

static void gridMove(DrumManager& s, int dstep, int dtrack) {
    _cs = (_cs + dstep + N_STEPS) % N_STEPS;
    if (dtrack) { _ct = (_ct + dtrack + N_INST) % N_INST; s.selectInst((uint8_t)_ct); _lastSel = (uint8_t)_ct; }
}

static void pattRow(DrumManager& s, int dir, bool press) {          // LEFT/RIGHT (dir = -1/+1) or PRESS (press) on the selected row
    switch (_pr) {
        case R_PATTERN: s.setPattern((uint8_t)((s.selPat + dir + N_PATTERNS) % N_PATTERNS)); break;
        case R_LENGTH:  s.setLength((uint8_t)clampi(s.patLen[s.selPat] + dir, 1, N_STEPS)); break;
        case R_MODE:    s.songMode = !s.songMode; break;
        case R_SYNC:    s.extSync = !s.extSync; break;
        case R_PADS:    s.padGm = !s.padGm; break;
        case R_COPY:    if (press) { s.copyPattern(s.selPat, (uint8_t)((s.selPat + 1) % N_PATTERNS)); setMsg("COPIED"); } break;
        case R_CLEAR:
            if (!press) break;
            if (millis() < _clearArm) { s.clearPattern(s.selPat); _clearArm = 0; } else _clearArm = millis() + 2000;
            break;
        case R_SAVE:
            if (!press) break;
            if (s.playing) setMsg("STOP FIRST"); else setMsg(storage_save(s) ? "SAVED" : "SAVE FAILED");
            break;
    }
}

void ui_handle_buttons(DrumManager& s) {
    const uint32_t now = millis();
    if (now - _bms < 5) return;                              // 5 ms sampling = cheap debounce
    _bms = now;
    for (int i = 0; i < 7; i++) { _bp[i] = _bc[i]; _bc[i] = !digitalRead(BPINS[i]); }

    if (s.sel != _lastSel) { _lastSel = s.sel; _ct = s.sel; }       // a pad hit (or CC27) moved the selection: the grid cursor follows

    // A: tap = play / stop, held 0.6 s = record on / off
    if (_bc[B_A] && !_bp[B_A]) { _aArmed = true; _bdown[B_A] = now; }
    if (_aArmed && _bc[B_A] && now - _bdown[B_A] >= 600) { _aArmed = false; s.setRecord(!s.recording); }
    if (_aArmed && !_bc[B_A] && _bp[B_A]) { _aArmed = false; s.togglePlay(); }
    if (fire(B_B, false, now)) { _page = (_page + 1) % PAGE_COUNT; pageChanged(); return; }

    const bool l = fire(B_LEFT, true, now), r = fire(B_RIGHT, true, now), u = fire(B_UP, true, now), d = fire(B_DOWN, true, now), p = fire(B_PRESS, false, now);
    switch (_page) {
        case PG_GRID:
            if (l) gridMove(s, -1, 0);
            if (r) gridMove(s, +1, 0);
            if (u) gridMove(s, 0, -1);
            if (d) gridMove(s, 0, +1);
            if (p) s.cycleStep((uint8_t)_ct, (uint8_t)_cs);
            break;
        case PG_INST:
            if (l) _sel = (_sel + N_KNOBS - 1) % N_KNOBS;
            if (r) _sel = (_sel + 1) % N_KNOBS;
            if (u || d) {
                const int held = (int)(now - _bdown[u ? B_UP : B_DOWN]);
                const bool fast = held > 700;                                         // slow, then fast when held
                const int step = (_sel == P_INST) ? 8 : (_sel == P_TEMPO) ? (fast ? 4 : 1) : (fast ? 8 : 2);   // tempo moves in whole BPM
                const int cur  = (_sel == P_TEMPO) ? (int)(s.bpm + 0.5f) - TEMPO_KNOB_BASE : s.pv[_sel];
                s.setParam((uint8_t)_sel, (uint8_t)clampi(cur + (u ? +step : -step), 0, 127));
            }
            if (p) s.hit(s.sel, VEL_NORMAL);
            break;
        case PG_PATT:
            if (u) _pr = (_pr + PATT_ROWS - 1) % PATT_ROWS;
            if (d) _pr = (_pr + 1) % PATT_ROWS;
            if (l) pattRow(s, -1, false);
            if (r) pattRow(s, +1, false);
            if (p) pattRow(s, +1, true);
            break;
        default:
            if (l) _ss = (_ss + SONG_MAX - 1) % SONG_MAX;
            if (r) _ss = (_ss + 1) % SONG_MAX;
            if (u || d) {
                const int cur = s.song[_ss] == 0xFF ? -1 : s.song[_ss];
                const int nx = clampi(cur + (u ? 1 : -1), -1, N_PATTERNS - 1);
                s.setSongSlot((uint8_t)_ss, nx < 0 ? 0xFF : (uint8_t)nx);
            }
            if (p) s.songMode = !s.songMode;
            break;
    }
}

// ---- Init -------------------------------------------------------------------------
void lcd_begin() {
    pinMode(LCD_CS, OUTPUT);  digitalWrite(LCD_CS, HIGH);
    pinMode(LCD_DC, OUTPUT);  digitalWrite(LCD_DC, HIGH);
    pinMode(LCD_RST, OUTPUT); digitalWrite(LCD_RST, HIGH);
    pinMode(LCD_BL, OUTPUT);  digitalWrite(LCD_BL, HIGH);
    for (int p : BPINS) pinMode(p, INPUT_PULLUP);
    initArcTables();

    SPI1.setTX(LCD_MOSI); SPI1.setSCK(LCD_SCK); SPI1.begin();
    SPI1.beginTransaction(_spi);

    digitalWrite(LCD_RST, LOW); delay(10); digitalWrite(LCD_RST, HIGH); delay(120);

    _cmd(0x01); delay(150);
    _cmd(0x11); delay(500);
    _cmd(0x3A); _dat(0x05);
    _cmd(0x36); _dat(0x70);
    _cmd(0xB2); _dat(0x0C); _dat(0x0C); _dat(0x00); _dat(0x33); _dat(0x33);
    _cmd(0xB7); _dat(0x35);
    _cmd(0xBB); _dat(0x19);
    _cmd(0xC0); _dat(0x2C);
    _cmd(0xC2); _dat(0x01);
    _cmd(0xC3); _dat(0x12);
    _cmd(0xC4); _dat(0x20);
    _cmd(0xC6); _dat(0x0F);
    _cmd(0xD0); _dat(0xA4); _dat(0xA1);
    _cmd(0x21); delay(10);
    _cmd(0x13); delay(10);
    _cmd(0x29); delay(20);

    memset(FB, 0, sizeof(FB));                               // clear the panel (one bulk push)
    pushRect(0, 0, LCD_W, LCD_H);
    SPI1.endTransaction();

    _first = true; _force = true; _pending = 0;
}
