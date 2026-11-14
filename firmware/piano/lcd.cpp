// ============================================================
//  lcd.cpp  —  piano panel, Waveshare Pico-LCD-1.14 (ST7789, 240x135)
//
//  Same renderer as the other projects (widgets with change keys, one rectangle per ui_flush, nothing larger than the top bar).
//  Eight knobs (two pages: the piano, then the effects; selecting past the 8th knob turns the page) and, below them, the 88-key
//  keyboard. The keys that sound light up amber. The top bar shows the instrument.
//  The palette is a concert grand's: black lacquer, ivory keys, gold trim.
//
//  LEFT/RIGHT select a knob, UP/DOWN change it (hold = fast), PRESS = audition a middle C (400 ms), PRESS held 0.6 s = next instrument.
//  A = sustain pedal (latching), B = una corda (latching). Both also respond to CC64 / CC67 from the keyboard.
// ============================================================
#include <Arduino.h>
#include <SPI.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "config.h"
#include "lcd.h"
#include "piano_manager.h"

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
#define C_BG       0x0000
#define C_TOPBG    0x1082
#define C_KBKG     0x18C3
#define C_KRIM     0x4208
#define C_SEP      0x6B4D
#define C_GRAY     0x7BCF
#define C_WHITE    0xFF9B
#define C_CYAN     0xD547
#define C_GREEN    0x5FE8
#define C_GREENDIM 0x2B02
#define C_ORANGE   0xFC40
#define C_RED      0xD0A0
#define C_AMBER    0xFD20
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
#define W_TOP   0
#define W_K0    1                           // knobs 1..8
#define W_KB0   9                           // keyboard strips 9..16
#define W_CREDIT 17                         // the credit line at the bottom (the selected instrument's)
#define KB_STRIPS 8
#define KB_Y      64                        // keyboard top
#define KB_H      60                        // white key height
#define KB_BLK_H  38                        // black key height
#define STRIP_W   30

static int      _sel = P_VOL;               // selected knob
static uint32_t _pending = 0, _midi_until = 0, _lastScan = 0, _audEnd = 0;
static bool     _force = true, _first = true;
static uint32_t _wmask[KB_STRIPS][3];       // which keys touch each keyboard strip

struct TopInfo { char a[10]; char b[20]; char c[6]; uint8_t ped, una, midi, cpu, pad[4]; };
static TopInfo  _topK;
static uint16_t _knobK[PARAMS_PER_PAGE];
static char     _creditK[40];
static int      _page = 0;                  // which knob page is showing (selected knob / 8)
static uint32_t _kbK[KB_STRIPS][3];

static const uint16_t GOLD = 0xD547, GOLD_DIM = 0x8B03, IVORY = 0xFF9B, IVORY_SH = 0xCE79, EBONY = 0x2104, EBONY_HI = 0x4208, LIT_W = 0xFD20, LIT_B = 0xC3A0;

static inline int whiteX(int i) { return (i * LCD_W) / N_WHITE; }
static void keyRect(int key, int& x, int& w, bool& black) {
    black = keyIsBlack(key); const int wi = whiteIndexOf(key);
    if (!black) { x = whiteX(wi); w = whiteX(wi + 1) - x - 1; }
    else        { x = whiteX(wi) - 1; w = 3; }                  // wi = whites BELOW this key, so the boundary between the white keys either side is whiteX(wi)
}
static void pageInit() {                                    // which keys touch which strip
    memset(_wmask, 0, sizeof _wmask);
    for (int k = KEY_LO; k <= KEY_HI; k++) { int x, w; bool b; keyRect(k, x, w, b); const int i = k - KEY_LO;
        for (int s = 0; s < KB_STRIPS; s++) if (x < (s + 1) * STRIP_W && x + w > s * STRIP_W) _wmask[s][i >> 5] |= 1u << (i & 31); }
}

static inline uint16_t dimc(uint16_t c, int f256) {
    const int r = ((c >> 11) & 31) * f256 / 256, g = ((c >> 5) & 63) * f256 / 256, b = (c & 31) * f256 / 256;
    return (uint16_t)((r << 11) | (g << 5) | b);
}
static void fb_clip(int x, int y, int w, int h, uint16_t c, int x0, int x1) {       // rectangle clipped to the strip [x0, x1)
    int a = x < x0 ? x0 : x, b = (x + w) > x1 ? x1 : (x + w); if (b > a) fb_rect(a, y, b - a, h, c);
}

// ---- Value text -----------------------------------------------------------
static float FxHz(uint8_t v, int which) { return which == 0 ? PianoFx::wahRateHz(v) : PianoFx::tremRateHz(v); }
static void valueText(const PianoManager& s, int id, char* out, size_t n) {
    const int v = s.pv[id];
    switch (PARAM_INFO[id].fmt) {
        case F_TONE:  snprintf(out, n, "%+d", v - 64); break;
        case F_CENTS: snprintf(out, n, "%+dc", (int)lroundf(tuneCentsOf(v))); break;
        case F_LFO:   if (v == 0) snprintf(out, n, "manual"); else snprintf(out, n, "%.1fHz", FxHz(v, 0)); break;
        case F_TREM:  if (v == 64) snprintf(out, n, "off"); else if (v < 64) snprintf(out, n, "trem %d%%", (int)((64 - v) * 100 / 64)); else snprintf(out, n, "pan %d%%", (int)((v - 64) * 100 / 63)); break;
        case F_RATE:  snprintf(out, n, "%.1fHz", FxHz(v, 1)); break;
        case F_REVT:  snprintf(out, n, "%d%%", (int)(v * 100 / 127)); break;
        case F_TIME:  if (decayOffOf(v)) snprintf(out, n, "natural"); else { const float t = decayT60Of(v); if (t < 9.95f) snprintf(out, n, "%.1fs", t); else snprintf(out, n, "%ds", (int)(t + 0.5f)); } break;
        case F_PCT:   if (id == P_HARD) snprintf(out, n, "x%.1f", hardExpOf(v)); else if (id == P_WIDTH) snprintf(out, n, "%d%%", (int)(widthOf(v) / 0.6f * 100.0f + 0.5f)); else snprintf(out, n, "%d%%", (int)(v * 100.0f / 127.0f + 0.5f)); break;
        default:      snprintf(out, n, "%d", v); break;
    }
}

// ---- top bar ------------------------------------------------------------------
static void fillTop(const PianoManager& s, TopInfo& t) {
    memset(&t, 0, sizeof t);
    char val[12]; valueText(s, _sel, val, sizeof val); snprintf(t.b, 17, "%s %s", PARAM_INFO[_sel].name, val);          // at most 16 characters: they must not run into the voice count
    snprintf(t.a, sizeof t.a, "%.8s", s.instrumentName());
    snprintf(t.c, sizeof t.c, "%dv", (int)s.voicesNow);
    t.ped = s.pedalDown; t.una = s.softPedal || s.pv[P_SOFT] > 8;
    const uint8_t cpu = g_cpu_load_pct; t.cpu = cpu >= 80 ? 2 : cpu >= 50 ? 1 : 0;
    t.midi = millis() < _midi_until;
}
static void drawTop(const TopInfo& t) {
    fb_rect(0, 0, LCD_W, TOP_H, C_TOPBG);
    fb_rect(0, 16, LCD_W, 1, GOLD); fb_rect(0, 17, LCD_W, 1, GOLD_DIM);                                   // gold trim
    fb_str(4, 5, t.a, GOLD, C_TOPBG);
    fb_str(54, 5, t.b, C_WHITE, C_TOPBG);
    fb_str(152, 5, t.c, C_GRAY, C_TOPBG);
    fb_str(172, 5, "PED", t.ped ? C_GREEN : C_KRIM, C_TOPBG);
    fb_str(194, 5, "UNA", t.una ? C_AMBER : C_KRIM, C_TOPBG);
    fb_rect(218, 5, 7, 7, t.cpu == 2 ? C_RED : t.cpu == 1 ? C_ORANGE : C_GREEN);
    fb_rect(228, 5, 8, 8, t.midi ? C_GREEN : C_GREENDIM);
    fb_px(228, 5, C_TOPBG);  fb_px(235, 5, C_TOPBG);  fb_px(228, 12, C_TOPBG); fb_px(235, 12, C_TOPBG);
}

// ---- knobs ----------------------------------------------------------------------
static void drawKnob(const PianoManager& s, int id, bool sel) {
    const ParamInfo& pi = PARAM_INFO[id];
    const int x0 = cellX(pi.col), y0 = cellY(0), cx = x0 + CELL_W / 2, cy = y0 + 22;
    fb_rect(x0, y0, CELL_W, CELL_H, C_BG);
    fb_str(cx - fb_strw(pi.label) / 2, y0 + 1, pi.label, sel ? C_WHITE : GOLD, C_BG);
    fb_disc(cx, cy, KNOB_R - 1, C_KBKG);
    const int n = (s.pv[id] * (ARC_N - 1)) / 127;
    for (int k = 0; k < ARC_N; k++) { const uint16_t c = (k <= n) ? GOLD : C_KRIM; fb_px(cx + ARC_DX[k], cy + ARC_DY[k], c); fb_px(cx + ARC_DX[k] + 1, cy + ARC_DY[k], c); }
    fb_disc(cx + DOT_DX[n], cy + DOT_DY[n], 2, C_WHITE);
    fb_circle(cx, cy, KNOB_R, sel ? C_WHITE : C_KRIM);
    if (sel) fb_circle(cx, cy, KNOB_R - 1, C_WHITE);
}

// ---- keyboard: one strip at a time ------------------------------------------------
static void drawStrip(const PianoManager& s, int st) {
    const int x0 = st * STRIP_W, x1 = x0 + STRIP_W;
    fb_rect(x0, KB_Y - 3, STRIP_W, KB_H + 7, C_BG);
    fb_rect(x0, KB_Y - 3, STRIP_W, 1, GOLD_DIM);                                                          // the fallboard rail
    for (int k = KEY_LO; k <= KEY_HI; k++) {                                                              // white keys first
        if (keyIsBlack(k)) continue;
        int x, w; bool b; keyRect(k, x, w, b);
        if (x >= x1 || x + w <= x0) continue;
        const bool on = s.keySounding(k);
        fb_clip(x, KB_Y, w, KB_H, on ? LIT_W : IVORY, x0, x1);
        if (!on) fb_clip(x, KB_Y + KB_H - 3, w, 3, IVORY_SH, x0, x1);                                     // shaded front edge
        if (k % 12 == 0) fb_clip(x + w / 2 - 1, KB_Y + KB_H - 9, 2, 2, on ? C_RED : GOLD_DIM, x0, x1);    // a gold dot on every C
    }
    for (int k = KEY_LO; k <= KEY_HI; k++) {                                                              // then the black keys
        if (!keyIsBlack(k)) continue;
        int x, w; bool b; keyRect(k, x, w, b);
        if (x >= x1 || x + w <= x0) continue;
        const bool on = s.keySounding(k);
        fb_clip(x, KB_Y, w, KB_BLK_H, on ? LIT_B : EBONY, x0, x1);
        if (!on) fb_clip(x, KB_Y, 1, KB_BLK_H, EBONY_HI, x0, x1);
    }
}

static void drawBackground() {
    fb_rect(0, 0, LCD_W, LCD_H, C_BG);
    fb_rect(0, 58, LCD_W, 1, GOLD_DIM);
}

// ---- change detection ----------------------------------------------------------------
static void scan(const PianoManager& s) {
    TopInfo t; fillTop(s, t);
    if (_force || memcmp(&t, &_topK, sizeof t)) { _topK = t; drawTop(t); _pending |= 1u << W_TOP; }
    _page = _sel / PARAMS_PER_PAGE;
    for (int slot = 0; slot < PARAMS_PER_PAGE; slot++) {                       // the eight knobs of the current page
        const int id = _page * PARAMS_PER_PAGE + slot;
        const uint16_t k = (uint16_t)(s.pv[id] | (id == _sel ? 0x80 : 0) | (id << 8));
        if (_force || k != _knobK[slot]) { _knobK[slot] = k; drawKnob(s, id, id == _sel); _pending |= 1u << (W_K0 + slot); }
    }
    for (int st = 0; st < KB_STRIPS; st++) {
        uint32_t m[3]; for (int i = 0; i < 3; i++) m[i] = s.keyMask[i] & _wmask[st][i];
        if (_force || memcmp(m, _kbK[st], sizeof m)) { memcpy(_kbK[st], m, sizeof m); drawStrip(s, st); _pending |= 1u << (W_KB0 + st); }
    }
    const char* cr = PIANO_INSTRUMENT[s.instrument].credit;                       // the credit follows the instrument
    if (_force || strncmp(cr, _creditK, sizeof _creditK - 1)) { strncpy(_creditK, cr, sizeof _creditK - 1); _creditK[sizeof _creditK - 1] = 0; fb_rect(0, 126, LCD_W, 9, C_BG); fb_str(6, 128, _creditK, C_KRIM, C_BG); _pending |= 1u << W_CREDIT; }

    _force = false;
}
static void widgetRect(int w, int& x, int& y, int& ww, int& hh) {
    x = 0; ww = LCD_W;
    if (w == W_TOP) { y = 0; hh = TOP_H; }
    else if (w < W_KB0) { x = cellX(w - W_K0); y = cellY(0); ww = CELL_W; hh = CELL_H; }
    else if (w == W_CREDIT) { x = 0; y = 126; ww = LCD_W; hh = 9; }
    else { x = (w - W_KB0) * STRIP_W; ww = STRIP_W; y = KB_Y - 3; hh = KB_H + 7; }
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

void ui_flush(const PianoManager& s) {
    if (_first) {                                            // first call: paint everything once
        _first = false;
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
static bool     _pressArmed = false;

static bool fire(int i, bool repeat, uint32_t now) {
    if (_bc[i] && !_bp[i]) { _bdown[i] = _brep[i] = now; return true; }
    if (repeat && _bc[i] && _bp[i] && now - _bdown[i] > 350 && now - _brep[i] >= 70) { _brep[i] = now; return true; }
    return false;
}
static int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

void ui_handle_buttons(PianoManager& s) {
    const uint32_t now = millis();
    if (_audEnd && now >= _audEnd) { _audEnd = 0; s.noteOff(60); }             // the audition note ends by itself
    if (now - _bms < 5) return;                                                // 5 ms sampling = cheap debounce
    _bms = now;
    for (int i = 0; i < 7; i++) { _bp[i] = _bc[i]; _bc[i] = !digitalRead(BPINS[i]); }

    if (fire(B_A, false, now)) s.setPedal(!s.pedalDown);                       // A: sustain pedal, latching
    if (fire(B_B, false, now)) s.softPedal = !s.softPedal;                     // B: una corda, latching
    const bool l = fire(B_LEFT, true, now), r = fire(B_RIGHT, true, now), u = fire(B_UP, true, now), d = fire(B_DOWN, true, now);
    // PRESS: a tap auditions middle C; held 0.6 s it selects the next instrument (and then does nothing on release)
    bool p = false;
    if (_bc[B_PRESS] && !_bp[B_PRESS]) { _pressArmed = true; _bdown[B_PRESS] = now; }
    if (_pressArmed && _bc[B_PRESS] && now - _bdown[B_PRESS] >= 600) { _pressArmed = false; s.setInstrument((uint8_t)((s.instrument + 1) % PIANO_N_INSTRUMENTS)); }
    if (_pressArmed && !_bc[B_PRESS] && _bp[B_PRESS]) { _pressArmed = false; p = true; }
    if (l) _sel = (_sel + N_PARAMS - 1) % N_PARAMS;                           // 16 knobs in a ring; crossing the 8th turns the page
    if (r) _sel = (_sel + 1) % N_PARAMS;
    if (u || d) {
        const bool fast = (int)(now - _bdown[u ? B_UP : B_DOWN]) > 700;
        const int step = fast ? 8 : 2;
        s.setParam((uint8_t)_sel, (uint8_t)clampi((int)s.pv[_sel] + (u ? step : -step), 0, 127));
    }
    if (p) { s.noteOn(60, 80); _audEnd = now + 400; }
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
