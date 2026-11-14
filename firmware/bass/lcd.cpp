// ============================================================
//  lcd.cpp  —  4-page bass synth panel, Waveshare Pico-LCD-1.14 (ST7789, 240x135)
//
//  Same renderer as the other projects: every widget remembers what it last drew and is only redrawn when its own value
//  changed; only that rectangle goes to the panel as one bulk SPI transfer; at most ONE rectangle per ui_flush().
//
//  PAGES (button B cycles them)
//    OSC    knobs: waveform / octave / detune of the 3 oscillators, their levels, sub, noise, glide, volume
//    FILT   knobs: cutoff, resonance, drive, key tracking, envelope amount, velocity, accent, gate length,
//           filter envelope (ADSR), amp envelope (ADSR), tempo, swing
//    SEQ    16 steps (2 rows of 8): note name, accent (bright), slide (white bar into the next step), rests
//    SETUP  patch, pattern, length, sync, keys, step-write, clear, save
//  Knob pages: LEFT/RIGHT select a knob, UP/DOWN change it (hold = fast), PRESS = audition a C2 note.
//  SEQ: LEFT/RIGHT cursor, UP/DOWN pitch (hold = octaves), PRESS = note -> accent -> slide -> slide+accent -> rest.
//  A = play / stop (tap)   A held 0.6 s = step-write on / off   B = next page
// ============================================================
#include <Arduino.h>
#include <SPI.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "config.h"
#include "lcd.h"
#include "bass_manager.h"

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
#define C_BG       0x0821
#define C_TOPBG    0x59C4
#define C_KBKG     0x2104
#define C_KRIM     0x52AA
#define C_SEP      0x8C71
#define C_GRAY     0x8410
#define C_WHITE    0xF79B
#define C_CYAN     0xE6C6
#define C_GREEN    0x5FE8
#define C_GREENDIM 0x2B02
#define C_ORANGE   0xFC40
#define C_RED      0xD0A0
#define C_AMBER    0xFD60
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
enum { PG_OSC, PG_FILT, PG_SEQ, PG_SETUP };
#define W_TOP     0
#define W_K0      1                         // knob pages: knobs 1..n, then two status lines
#define W_SEQ0    1                         // SEQ page: 16 step cells, then the info line
#define W_SEQINFO 17
#define W_P_ROW0  1                         // SETUP page: 8 rows
#define PATT_ROWS 8
enum { R_PATCH, R_PATTERN, R_LENGTH, R_SYNC, R_KEYS, R_WRITE, R_CLEAR, R_SAVE };

static int      _page = PG_OSC;
static int      _ksel[2] = { P_WAV1, P_CUT };   // selected knob per knob page
static int      _pr = 0;                    // setup page: selected row
static uint8_t  _kidx[N_PARAMS];            // widget index of each knob within its page
static uint8_t  _kcount[2];
static uint8_t  _kparam[2][24];             // page -> list of params
static uint32_t _pending = 0;
static uint32_t _midi_until = 0;
static bool     _force = true, _first = true, _page_dirty = false;
static uint32_t _lastScan = 0, _clearArm = 0, _msgUntil = 0, _audEnd = 0;
static char     _msg[20] = "";

struct TopInfo  { char a[8]; char b[20]; char c[6]; char nm[5]; uint8_t play, ext, wr, queued, midi, cpu, grp, gate; };
struct StatInfo { char patch[12]; char nm[5]; uint8_t gate, active, pat, step, flags, tr; int8_t bend10; uint8_t pad[2]; };
struct CellKey  { uint8_t note, flags, cur, play, beyond, pad[3]; };
struct PRow     { char label[12]; char value[24]; uint8_t sel; };
static TopInfo  _topK;
static StatInfo _statK;
static CellKey  _cellK[N_STEPS];
static char     _infoK[64];
static uint16_t _knobK[N_PARAMS];
static PRow     _pK[PATT_ROWS];

static inline uint16_t dimc(uint16_t c, int f256) {
    const int r = ((c >> 11) & 31) * f256 / 256, g = ((c >> 5) & 63) * f256 / 256, b = (c & 31) * f256 / 256;
    return (uint16_t)((r << 11) | (g << 5) | b);
}
static void fb_frame(int x, int y, int w, int h, uint16_t c) { fb_rect(x, y, w, 1, c); fb_rect(x, y + h - 1, w, 1, c); fb_rect(x, y, 1, h, c); fb_rect(x + w - 1, y, 1, h, c); }
static const uint16_t GROUP_COL[6] = { 0xF79B /*osc: cream*/, 0xCE79 /*mix: light grey*/, 0xFC40 /*filter: orange*/, 0xFD80 /*filter env: amber*/, 0xE6C6 /*amp env: gold*/, 0xFFFF /*play: white*/ };   // Minimoog palette
static const uint16_t STEP_ON[4] = { 0xFC40, 0xFD80, 0xE6C6, 0xF79B };   // orange / amber / gold / cream

static void pageInit() {                                     // which params live on which knob page, in which order
    _kcount[0] = _kcount[1] = 0;
    for (int i = 0; i < N_PARAMS; i++) { const int pg = PARAM_INFO[i].page; _kidx[i] = _kcount[pg]; _kparam[pg][_kcount[pg]++] = (uint8_t)i; }
}
static inline bool knobPage() { return _page == PG_OSC || _page == PG_FILT; }

// ---- Value text -----------------------------------------------------------
static void timeText(float t, char* out, size_t n) { if (t < 0.995f) snprintf(out, n, "%dms", (int)(t * 1000.0f + 0.5f)); else snprintf(out, n, "%.1fs", t); }
static void valueText(const BassManager& s, int id, char* out, size_t n) {
    const int v = s.pv[id];
    switch (PARAM_INFO[id].fmt) {
        case F_WAVE:  snprintf(out, n, "%s", waveOf(v) == 0 ? "SAW" : waveOf(v) == 1 ? "SQR" : "TRI"); break;
        case F_OCT:   snprintf(out, n, "%+d", octOf(v)); break;
        case F_CENTS: snprintf(out, n, "%+dc", (int)lroundf(centsOf(v))); break;
        case F_GLIDE: if (v < 1) snprintf(out, n, "off"); else timeText(glideSecOf(v), out, n); break;
        case F_HZ:    { const float h = cutHzOf(v); if (h < 1000.0f) snprintf(out, n, "%dHz", (int)(h + 0.5f)); else snprintf(out, n, "%.1fk", h / 1000.0f); break; }
        case F_PCT:   { float p = v * 100.0f / 127.0f;
                        if (id == P_RES) p = resRatioOf(v) * 100.0f; else if (id == P_GATE) p = gateFracOf(v) * 100.0f; else if (id == P_SWING) p = swingFromKnob(v) * 100.0f;
                        snprintf(out, n, "%d%%", (int)(p + 0.5f)); break; }
        case F_OCTS:  snprintf(out, n, "%.1foct", envOctsOf(v)); break;
        case F_TIME:  timeText((id == P_FA || id == P_AA) ? attackSecOf(v) : timeSecOf(v), out, n); break;
        case F_BPM:   snprintf(out, n, "%.0f", s.bpm); break;
        default:      snprintf(out, n, "%d", v); break;
    }
}

// ---- Geometry ---------------------------------------------------------------
#define SEQ_X0  8
#define SEQ_PITCH 28
#define SEQ_Y(r) (21 + (r) * 42)
#define PROW_Y  19
#define PROW_H  14
static int statX() { return _page == PG_OSC ? 0 : cellX(2); }                 // FILT page: the TEMPO and SWING knobs occupy the left of row 2
static void widgetRect(int w, int& x, int& y, int& ww, int& hh) {
    x = 0; ww = LCD_W;
    if (w == W_TOP) { y = 0; hh = TOP_H; return; }
    switch (_page) {
        case PG_OSC: case PG_FILT: {
            const int nk = _kcount[_page];
            if (w >= W_K0 + nk) { x = statX(); ww = LCD_W - x; y = cellY(2) + 6 + (w - W_K0 - nk) * 13; hh = 12; }
            else { const ParamInfo& pi = PARAM_INFO[_kparam[_page][w - W_K0]]; x = cellX(pi.col); y = cellY(pi.row); ww = CELL_W; hh = CELL_H; }
            break; }
        case PG_SEQ:
            if (w == W_SEQINFO) { y = 106; hh = 14; }
            else { const int c = w - W_SEQ0; x = SEQ_X0 + (c % 8) * SEQ_PITCH; ww = SEQ_PITCH; y = SEQ_Y(c / 8); hh = 40; }
            break;
        default: y = PROW_Y + (w - W_P_ROW0) * PROW_H; hh = PROW_H; break;
    }
}

// ---- The top bar ---------------------------------------------------------------
static void fillTop(const BassManager& s, TopInfo& t) {
    memset(&t, 0, sizeof t);
    snprintf(t.a, sizeof t.a, "P%02d", s.selPat + 1);
    t.queued = (s.playing && s.patNow != s.selPat);
    char val[12];
    switch (_page) {
        case PG_OSC: case PG_FILT: { const int id = _ksel[_page]; valueText(s, id, val, sizeof val); snprintf(t.b, sizeof t.b, "%s %s", PARAM_INFO[id].name, val); t.grp = PARAM_INFO[id].group; break; }
        case PG_SEQ: { char nm[8]; noteName(s.stepNote[s.selPat][s.cursor], nm); snprintf(t.b, sizeof t.b, "Step %02d %s", s.cursor + 1, (s.stepFlg[s.selPat][s.cursor] & F_GATE) ? nm : "rest"); t.grp = G_PLAY; break; }
        default: snprintf(t.b, sizeof t.b, "Setup"); t.grp = G_FILT; break;
    }
    snprintf(t.c, sizeof t.c, "%.0f", s.bpm);
    if (s.gateNow) noteName(s.noteNow, t.nm);
    t.gate = s.gateNow; t.play = s.playing; t.ext = s.extSync; t.wr = s.stepWrite;
    const uint8_t cpu = g_cpu_load_pct; t.cpu = cpu >= 80 ? 2 : cpu >= 50 ? 1 : 0;
    t.midi = millis() < _midi_until;
}
static void drawTop(const TopInfo& t) {
    fb_rect(0, 0, LCD_W, TOP_H, C_TOPBG);
    fb_rect(0, 2, LCD_W, 2, 0x4163); fb_rect(0, 14, LCD_W, 1, 0x4163); fb_rect(0, 15, LCD_W, 1, 0x6A66); fb_rect(0, 17, LCD_W, 1, 0xF79B);   // walnut grain, cream panel trim
    fb_str(4, 5, t.a, t.queued ? C_AMBER : C_WHITE, C_TOPBG);
    fb_str(36, 5, t.b, GROUP_COL[t.grp], C_TOPBG);
    fb_str(134, 5, t.c, C_WHITE, C_TOPBG);
    if (t.gate) fb_str(156, 5, t.nm, C_GREEN, C_TOPBG);
    if (t.play) { for (int k = 0; k < 9; k++) fb_rect(178 + k / 2, 4 + k, 8 - k, 1, C_GREEN); fb_rect(178, 4, 2, 9, C_GREEN); }
    else fb_rect(178, 4, 8, 9, C_GRAY);
    if (t.ext) fb_str(190, 5, "EXT", C_CYAN, C_TOPBG);
    if (t.wr)  fb_str(210, 5, "W", C_RED, C_TOPBG);
    fb_rect(218, 5, 7, 7, t.cpu == 2 ? C_RED : t.cpu == 1 ? C_ORANGE : C_GREEN);
    fb_rect(228, 5, 8, 8, t.midi ? C_GREEN : C_GREENDIM);
    fb_px(228, 5, C_TOPBG);  fb_px(235, 5, C_TOPBG);  fb_px(228, 12, C_TOPBG); fb_px(235, 12, C_TOPBG);
}

// ---- Knob pages ------------------------------------------------------------------
static void drawKnob(const BassManager& s, int id, bool sel) {
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
static void fillStat(const BassManager& s, StatInfo& t) {
    memset(&t, 0, sizeof t);
    snprintf(t.patch, sizeof t.patch, "%s", s.patchName());
    noteName(s.noteNow, t.nm); t.gate = s.gateNow; t.active = s.voiceActive();
    t.pat = s.patNow; t.step = s.playing ? s.stepNow : 0xFF; t.flags = (uint8_t)((s.extSync ? 1 : 0) | (s.stepWrite ? 2 : 0));
    t.tr = (uint8_t)s.transpose; t.bend10 = (int8_t)lroundf(s.bendSemi * 10.0f);
}
static void drawStat(const StatInfo& t, int line) {
    int x, y, w, h; widgetRect(W_K0 + _kcount[_page] + line, x, y, w, h);
    fb_rect(x, y, w, h, C_BG);
    char b[40];
    if (line == 0) {
        fb_str(x + 6, y + 2, t.patch, C_WHITE, C_BG);
        fb_str(x + 6 + 12 * 6, y + 2, "NOTE", C_GRAY, C_BG);
        fb_str(x + 6 + 17 * 6, y + 2, t.active ? t.nm : "--", t.gate ? C_GREEN : t.active ? C_AMBER : C_GRAY, C_BG);
    } else {
        if (t.step != 0xFF) snprintf(b, sizeof b, "P%02d STEP %02d", t.pat + 1, t.step + 1); else snprintf(b, sizeof b, "stopped");
        fb_str(x + 6, y + 2, b, t.step != 0xFF ? C_GREEN : C_GRAY, C_BG);
        snprintf(b, sizeof b, "TR%+d", (int)(int8_t)t.tr); fb_str(x + 6 + 12 * 6, y + 2, b, t.tr ? C_AMBER : C_GRAY, C_BG);
        snprintf(b, sizeof b, "BEND%+.1f", t.bend10 / 10.0f); fb_str(x + 6 + 17 * 6, y + 2, b, t.bend10 ? C_CYAN : C_GRAY, C_BG);
    }
}

// ---- SEQ page --------------------------------------------------------------------
static void fillCell(const BassManager& s, int c, CellKey& k) {
    memset(&k, 0, sizeof k);
    k.note = s.stepNote[s.selPat][c]; k.flags = s.stepFlg[s.selPat][c]; k.cur = (c == s.cursor); k.play = (s.playing && s.patNow == s.selPat && c == s.stepNow); k.beyond = c >= s.patLen[s.selPat];
}
static void drawCell(const CellKey& k, int c) {
    int x, y, w, h; widgetRect(W_SEQ0 + c, x, y, w, h);
    fb_rect(x, y, w, h, C_BG);
    const int g = (c & 15) >> 2, cw = w - 1;
    const bool on = k.flags & F_GATE, acc = k.flags & F_ACCENT;
    uint16_t bg = k.beyond ? C_BG : !on ? C_KBKG : acc ? STEP_ON[g] : dimc(STEP_ON[g], 90);
    fb_rect(x, y + 4, cw, 32, bg);
    char b[12]; snprintf(b, sizeof b, "%d", c + 1);
    fb_str(x + 2, y + 6, b, k.beyond ? C_KRIM : on ? (acc ? 0x0000 : C_WHITE) : C_GRAY, bg);
    if (on && !k.beyond) { noteName(k.note, b); fb_str(x + (cw - fb_strw(b)) / 2, y + 19, b, acc ? 0x0000 : C_WHITE, bg); }
    else if (!k.beyond) fb_rect(x + cw / 2 - 2, y + 22, 4, 2, C_KRIM);
    if ((k.flags & F_SLIDE) && on && !k.beyond) fb_rect(x, y + 33, w, 3, C_WHITE);                // the slide bar bridges into the next cell
    if (k.play) fb_rect(x, y + 1, cw, 2, C_WHITE);
    if (k.cur)  fb_frame(x, y + 3, w, 34, C_WHITE);                                               // inside the cell: a frame outside the widget rect would never be sent
}
static void fillInfo(const BassManager& s, char* b, size_t n) {
    snprintf(b, n, "LEN %02d  KEYS %s %+d%s", s.patLen[s.selPat], s.keyMode ? "TRANSPOSE" : "PLAY", (int)s.transpose, s.stepWrite ? "  WRITE" : "");
}
static void drawInfo(const char* b, bool wr) { fb_rect(0, 106, LCD_W, 14, C_BG); fb_str(8, 110, b, wr ? C_AMBER : C_GRAY, C_BG); }

// ---- SETUP page -------------------------------------------------------------------
static void fillPRow(const BassManager& s, int r, PRow& p) {
    memset(&p, 0, sizeof p);
    static const char* L[PATT_ROWS] = { "PATCH", "PATTERN", "LENGTH", "SYNC", "KEYS", "STEP-WRITE", "CLEAR", "SAVE" };
    snprintf(p.label, sizeof p.label, "%s", L[r]);
    switch (r) {
        case R_PATCH:   snprintf(p.value, sizeof p.value, "%s", s.patchName()); break;
        case R_PATTERN: snprintf(p.value, sizeof p.value, "%02d", s.selPat + 1); break;
        case R_LENGTH:  snprintf(p.value, sizeof p.value, "%d STEPS", s.patLen[s.selPat]); break;
        case R_SYNC:    snprintf(p.value, sizeof p.value, "%s", s.extSync ? "MIDI CLOCK" : "INTERNAL"); break;
        case R_KEYS:    snprintf(p.value, sizeof p.value, "%s", s.keyMode ? "TRANSPOSE" : "PLAY OVER"); break;
        case R_WRITE:   snprintf(p.value, sizeof p.value, "%s", s.stepWrite ? "ON: keys enter notes" : "off"); break;
        case R_CLEAR:   snprintf(p.value, sizeof p.value, "%s", millis() < _clearArm ? "PRESS AGAIN!" : "[PRESS]"); break;
        default:        snprintf(p.value, sizeof p.value, "%s", millis() < _msgUntil ? _msg : "[PRESS]"); break;
    }
    p.sel = (r == _pr);
}
static void drawPRow(const PRow& p, int r) {
    const int y = PROW_Y + r * PROW_H; const uint16_t bg = p.sel ? C_KBKG : C_BG;
    fb_rect(0, y, LCD_W, PROW_H, bg);
    fb_str(10, y + 4, p.label, p.sel ? C_WHITE : C_GRAY, bg);
    fb_str(110, y + 4, p.value, p.sel ? C_CYAN : C_WHITE, bg);
    if (p.sel) fb_rect(2, y + 3, 3, 8, C_WHITE);
}

static void drawBackground() {
    fb_rect(0, 0, LCD_W, LCD_H, C_BG);
    if (knobPage()) {
        for (int r = 0; r < 2; r++) fb_rect(0, cellY(r) + CELL_H, LCD_W, 1, C_SEP);
    } else if (_page == PG_SEQ) {
        fb_str(4, 124, "UP/DN pitch  PRESS note>acc>slide>rest", C_GRAY, C_BG);
    }
}

// ---- Change detection -----------------------------------------------------------
static void scan(const BassManager& s) {
    TopInfo t; fillTop(s, t);
    if (_force || memcmp(&t, &_topK, sizeof t)) { _topK = t; drawTop(t); _pending |= 1u << W_TOP; }
    switch (_page) {
        case PG_OSC: case PG_FILT: {
            const int nk = _kcount[_page];
            for (int i = 0; i < nk; i++) {
                const int id = _kparam[_page][i]; const uint16_t k = (uint16_t)(s.pv[id] | (id == _ksel[_page] ? 0x100 : 0));
                if (_force || k != _knobK[id]) { _knobK[id] = k; drawKnob(s, id, id == _ksel[_page]); _pending |= 1u << (W_K0 + i); }
            }
            StatInfo st; fillStat(s, st);
            if (_force || memcmp(&st, &_statK, sizeof st)) { _statK = st; drawStat(st, 0); drawStat(st, 1); _pending |= 3u << (W_K0 + nk); }
            break; }
        case PG_SEQ:
            for (int c = 0; c < N_STEPS; c++) { CellKey k; fillCell(s, c, k); if (_force || memcmp(&k, &_cellK[c], sizeof k)) { _cellK[c] = k; drawCell(k, c); _pending |= 1u << (W_SEQ0 + c); } }
            { char b[64]; memset(b, 0, sizeof b); fillInfo(s, b, sizeof b); if (_force || memcmp(b, _infoK, sizeof b)) { memcpy(_infoK, b, sizeof b); drawInfo(b, s.stepWrite); _pending |= 1u << W_SEQINFO; } }
            break;
        default:
            for (int r = 0; r < PATT_ROWS; r++) { PRow p; fillPRow(s, r, p); if (_force || memcmp(&p, &_pK[r], sizeof p)) { _pK[r] = p; drawPRow(p, r); _pending |= 1u << (W_P_ROW0 + r); } }
            break;
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

void ui_flush(const BassManager& s) {
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
static bool     _aArmed = false;

static bool fire(int i, bool repeat, uint32_t now) {
    if (_bc[i] && !_bp[i]) { _bdown[i] = _brep[i] = now; return true; }
    if (repeat && _bc[i] && _bp[i] && now - _bdown[i] > 350 && now - _brep[i] >= 70) { _brep[i] = now; return true; }
    return false;
}
static void setMsg(const char* m) { snprintf(_msg, sizeof _msg, "%s", m); _msgUntil = millis() + 2500; }
static int  clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

static void setupRow(BassManager& s, int dir, bool press) {
    switch (_pr) {
        case R_PATCH:   s.loadPatch((uint8_t)((s.patch + dir + N_PATCHES) % N_PATCHES)); break;
        case R_PATTERN: s.setPattern((uint8_t)((s.selPat + dir + N_PATTERNS) % N_PATTERNS)); break;
        case R_LENGTH:  s.setLength((uint8_t)clampi(s.patLen[s.selPat] + dir, 1, N_STEPS)); break;
        case R_SYNC:    s.extSync = !s.extSync; break;
        case R_KEYS:    s.keyMode = s.keyMode ? 0 : 1; break;
        case R_WRITE:   s.stepWrite = !s.stepWrite; break;
        case R_CLEAR:
            if (!press) break;
            if (millis() < _clearArm) { s.clearPattern(s.selPat); s.cursor = 0; _clearArm = 0; } else _clearArm = millis() + 2000;
            break;
        case R_SAVE:
            if (!press) break;
            if (s.playing) setMsg("STOP FIRST"); else setMsg(storage_save(s) ? "SAVED" : "SAVE FAILED");
            break;
    }
}

void ui_handle_buttons(BassManager& s) {
    const uint32_t now = millis();
    if (_audEnd && now >= _audEnd) { _audEnd = 0; s.noteOff(KEY_ROOT); }       // the audition note ends by itself
    if (now - _bms < 5) return;                              // 5 ms sampling = cheap debounce
    _bms = now;
    for (int i = 0; i < 7; i++) { _bp[i] = _bc[i]; _bc[i] = !digitalRead(BPINS[i]); }

    // A: tap = play / stop, held 0.6 s = step-write on / off
    if (_bc[B_A] && !_bp[B_A]) { _aArmed = true; _bdown[B_A] = now; }
    if (_aArmed && _bc[B_A] && now - _bdown[B_A] >= 600) { _aArmed = false; s.stepWrite = !s.stepWrite; }
    if (_aArmed && !_bc[B_A] && _bp[B_A]) { _aArmed = false; s.togglePlay(); }
    if (fire(B_B, false, now)) { _page = (_page + 1) % PAGE_COUNT; _page_dirty = true; return; }

    const bool l = fire(B_LEFT, true, now), r = fire(B_RIGHT, true, now), u = fire(B_UP, true, now), d = fire(B_DOWN, true, now), p = fire(B_PRESS, false, now);
    const int heldUD = (int)(now - _bdown[u ? B_UP : B_DOWN]);
    switch (_page) {
        case PG_OSC: case PG_FILT: {
            int& sel = _ksel[_page]; const int nk = _kcount[_page]; int pos = _kidx[sel];
            if (l) { pos = (pos + nk - 1) % nk; sel = _kparam[_page][pos]; }
            if (r) { pos = (pos + 1) % nk; sel = _kparam[_page][pos]; }
            if (u || d) {
                const bool fast = heldUD > 700;
                const int step = (sel == P_TEMPO) ? (fast ? 4 : 1) : (fast ? 8 : 2);
                const int cur = (sel == P_TEMPO) ? (int)(s.bpm + 0.5f) - TEMPO_KNOB_BASE : s.pv[sel];
                s.setParam((uint8_t)sel, (uint8_t)clampi(cur + (u ? +step : -step), 0, 127));
            }
            if (p) { s.noteOn(KEY_ROOT, VEL_NORMAL); _audEnd = now + 350; }            // audition: C2 for 350 ms
            break; }
        case PG_SEQ: {
            const int len = s.patLen[s.selPat];
            if (l) s.cursor = (uint8_t)((s.cursor + len - 1) % len);
            if (r) s.cursor = (uint8_t)((s.cursor + 1) % len);
            if (u || d) s.shiftNote(s.cursor, (heldUD > 700 ? 12 : 1) * (u ? 1 : -1));
            if (p) s.cycleFlags(s.cursor);
            break; }
        default:
            if (u) _pr = (_pr + PATT_ROWS - 1) % PATT_ROWS;
            if (d) _pr = (_pr + 1) % PATT_ROWS;
            if (l) setupRow(s, -1, false);
            if (r) setupRow(s, +1, false);
            if (p) setupRow(s, +1, true);
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

    pageInit();
    _first = true; _force = true; _pending = 0;
}
