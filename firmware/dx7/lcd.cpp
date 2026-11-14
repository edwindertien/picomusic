// ============================================================
//  lcd.cpp  —  DX7 panel, Waveshare Pico-LCD-1.14 (ST7789, 240x135)
//
//  Same renderer as the other instruments (framebuffer, widgets with change keys, one rectangle per ui_flush, nothing larger than
//  the top bar). Top bar: program number, voice name, algorithm, voices sounding, CPU load. Below it the six operators (carriers
//  amber, modulators cyan: frequency ratio and output level), then the eight knobs of the selected page (two pages: the voice,
//  then the effects).
//
//  LEFT/RIGHT select a knob, UP/DOWN change it (hold = fast), PRESS = audition a middle C (400 ms), A = sustain pedal (latching),
//  B = next voice (also: PRESS held 0.6 s, or a MIDI program change).
// ============================================================
#include <Arduino.h>
#include <SPI.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "config.h"
#include "lcd.h"
#include "dx7_manager.h"

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

// ---- Colours (RGB565) -----------------------------------------
#define C_BG       0x0000
#define C_TOPBG    0x1082
#define C_GRAY     0x7BCF
#define C_DIM      0x39E7
#define C_WHITE    0xFF9B
#define C_CYAN     0x4E9F
#define C_AMBER    0xFD20
#define C_GREEN    0x5FE8
#define C_RED      0xD0A0
#define C_SEL      0x2965

// ---- Layout -------------------------------------------------
#define TOP_H     18
#define OPS_Y     20
#define OPS_H     44
#define OP_W      40
#define KNOB_Y    68
#define KNOB_H    44
#define CELL_W    29
#define BOT_Y    116
#define BOT_H     19
static inline int cellX(int col) { return col * CELL_W + (col >= 4 ? 8 : 0); }   // col 7 ends at x=239
enum { W_TOP, W_OPS, W_K0, W_BOT = W_K0 + PARAMS_PER_PAGE, W_N };

static int      _sel = P_MOD, _page = 0;
static uint32_t _pending = 0, _midi_until = 0, _lastScan = 0, _audEnd = 0;
static bool     _force = true, _first = true;
static uint32_t _key[W_N];                  // what each widget showed the last time (change keys)

static uint32_t fnv(uint32_t h, uint32_t v) { h ^= v; h *= 16777619u; return h; }
static void fmtRatio(const Dx7Op& o, char* out, size_t n) {
    if (o.fixed) { int hz = 1; for (int i = 0; i < (o.coarse & 3); i++) hz *= 10; snprintf(out, n, "%dHz", hz); }
    else         { const int c = (o.coarse ? o.coarse * 100 : 50) * (100 + o.fine) / 100; snprintf(out, n, "%d.%02d", c / 100, c % 100); }
}

static void drawTop(const Dx7Manager& s, bool midi) {
    fb_rect(0, 0, LCD_W, TOP_H, C_TOPBG);
    char b[48];
    snprintf(b, sizeof b, "%02d %-10s", s.patchNo + 1, s.patchName());
    fb_str(4, 5, b, C_WHITE, C_TOPBG);
    snprintf(b, sizeof b, "ALG %02d", s.patch.alg + 1);
    fb_str(96, 5, b, C_AMBER, C_TOPBG);
    snprintf(b, sizeof b, "V%u/%d", (unsigned)s.voicesNow, N_POLY);
    fb_str(142, 5, b, C_GRAY, C_TOPBG);
    snprintf(b, sizeof b, "%u%%", (unsigned)g_cpu_load_pct);
    fb_str(186, 5, b, g_cpu_load_pct > 85 ? C_RED : C_GRAY, C_TOPBG);
    fb_disc(232, 9, 3, midi ? C_GREEN : C_DIM);
}
static void drawOps(const Dx7Manager& s) {
    fb_rect(0, OPS_Y, LCD_W, OPS_H, C_BG);
    for (int k = 0; k < 6; k++) {                          // left to right: operator 1 .. 6
        const int idx = 5 - k; const Dx7Op& o = s.patch.op[idx]; const bool car = dx7IsCarrier(s.patch.alg, idx);
        const int x = k * OP_W; const uint16_t c = car ? C_AMBER : C_CYAN;
        char b[12]; snprintf(b, sizeof b, "OP%d%s", k + 1, (DX7_ALG[s.patch.alg][idx] & (DX_FB_IN | DX_FB_OUT)) ? "f" : "");
        fb_str(x + 4, OPS_Y + 1, b, c, C_BG);
        fmtRatio(o, b, sizeof b); fb_str(x + 4, OPS_Y + 12, b, C_WHITE, C_BG);
        const int w = (o.ol * (OP_W - 10)) / 99;           // output level bar
        fb_rect(x + 4, OPS_Y + 25, OP_W - 10, 5, C_DIM); fb_rect(x + 4, OPS_Y + 25, w, 5, c);
        snprintf(b, sizeof b, "%d", o.ol); fb_str(x + 4, OPS_Y + 33, b, C_GRAY, C_BG);
    }
}
static void drawKnob(const Dx7Manager& s, int w) {
    const int i = w - W_K0, id = _page * PARAMS_PER_PAGE + i, x = cellX(i);
    const bool sel = id == _sel;
    fb_rect(x, KNOB_Y, CELL_W, KNOB_H, sel ? C_SEL : C_BG);
    fb_str(x + 2, KNOB_Y + 3, PARAM_INFO[id].label, sel ? C_WHITE : C_GRAY, sel ? C_SEL : C_BG);
    char b[8]; snprintf(b, sizeof b, "%d", s.pv[id]);
    fb_str(x + 2, KNOB_Y + 16, b, id >= P_WAH ? C_CYAN : C_AMBER, sel ? C_SEL : C_BG);
    fb_rect(x + 2, KNOB_Y + 31, CELL_W - 5, 5, C_DIM); fb_rect(x + 2, KNOB_Y + 31, (s.pv[id] * (CELL_W - 5)) / 127, 5, sel ? C_WHITE : C_GRAY);
}
static void drawBot(const Dx7Manager& s) {
    fb_rect(0, BOT_Y, LCD_W, BOT_H, C_BG);
    char b[48];
    snprintf(b, sizeof b, "%s   pedal %s   page %d/2", PARAM_INFO[_sel].name, s.pedalDown ? "DOWN" : "up", _page + 1);
    fb_str(4, BOT_Y + 3, b, C_WHITE, C_BG);
    fb_str(4, BOT_Y + 12, "A pedal  B next voice  PRESS tone", C_DIM, C_BG);
}
static void widgetRect(int w, int& x, int& y, int& ww, int& hh) {
    x = 0; ww = LCD_W;
    if (w == W_TOP) { y = 0; hh = TOP_H; }
    else if (w == W_OPS) { y = OPS_Y; hh = OPS_H; }
    else if (w == W_BOT) { y = BOT_Y; hh = BOT_H; }
    else { x = cellX(w - W_K0); ww = CELL_W; y = KNOB_Y; hh = KNOB_H; }
}
static uint32_t keyOf(const Dx7Manager& s, int w, bool midi) {
    uint32_t h = 2166136261u;
    if (w == W_TOP) { h = fnv(h, s.patchNo); for (const char* p = s.patchName(); *p; p++) h = fnv(h, (uint8_t)*p); h = fnv(h, s.patch.alg); h = fnv(h, s.voicesNow); h = fnv(h, g_cpu_load_pct); h = fnv(h, midi); }
    else if (w == W_OPS) { h = fnv(h, s.patch.alg); h = fnv(h, s.patchNo); for (int i = 0; i < 6; i++) { const Dx7Op& o = s.patch.op[i]; h = fnv(h, o.ol); h = fnv(h, o.coarse); h = fnv(h, o.fine); h = fnv(h, o.fixed); } }
    else if (w == W_BOT) { h = fnv(h, _sel); h = fnv(h, s.pedalDown); }
    else { const int id = _page * PARAMS_PER_PAGE + (w - W_K0); h = fnv(h, s.pv[id]); h = fnv(h, id == _sel); h = fnv(h, _page); }
    return h;
}
static void scan(const Dx7Manager& s) {
    _page = _sel / PARAMS_PER_PAGE;
    const bool midi = millis() < _midi_until;
    for (int w = 0; w < W_N; w++) {
        const uint32_t k = keyOf(s, w, midi);
        if (!_force && k == _key[w]) continue;
        _key[w] = k; _pending |= 1u << w;
        if (w == W_TOP) drawTop(s, midi); else if (w == W_OPS) drawOps(s); else if (w == W_BOT) drawBot(s); else drawKnob(s, w);
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

void ui_flush(const Dx7Manager& s) {
    if (_first) {                                            // first call: paint everything once
        _first = false;
        memset(FB, 0, sizeof FB);
        _force = true; scan(s);
        SPI1.beginTransaction(_spi);
        pushRect(0, 0, LCD_W, LCD_H);
        SPI1.endTransaction();
        _pending = 0; _lastScan = millis();
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

void ui_handle_buttons(Dx7Manager& s) {
    const uint32_t now = millis();
    if (_audEnd && now >= _audEnd) { _audEnd = 0; s.noteOff(60); }             // the audition note ends by itself
    if (now - _bms < 5) return;                                                // 5 ms sampling = cheap debounce
    _bms = now;
    for (int i = 0; i < 7; i++) { _bp[i] = _bc[i]; _bc[i] = !digitalRead(BPINS[i]); }

    if (fire(B_A, false, now)) s.setPedal(!s.pedalDown);                       // A: sustain pedal, latching
    if (fire(B_B, false, now)) s.setPatch((uint8_t)((s.patchNo + 1) % s.nBank));
    const bool l = fire(B_LEFT, true, now), r = fire(B_RIGHT, true, now), u = fire(B_UP, true, now), d = fire(B_DOWN, true, now);
    bool p = false;                                                            // PRESS: a tap auditions middle C; held 0.6 s it selects the next voice
    if (_bc[B_PRESS] && !_bp[B_PRESS]) { _pressArmed = true; _bdown[B_PRESS] = now; }
    if (_pressArmed && _bc[B_PRESS] && now - _bdown[B_PRESS] >= 600) { _pressArmed = false; s.setPatch((uint8_t)((s.patchNo + 1) % s.nBank)); }
    if (_pressArmed && !_bc[B_PRESS] && _bp[B_PRESS]) { _pressArmed = false; p = true; }
    if (l) _sel = (_sel + N_PARAMS - 1) % N_PARAMS;                           // 16 knobs in a ring; crossing the 8th turns the page
    if (r) _sel = (_sel + 1) % N_PARAMS;
    if (u || d) {
        const bool fast = (int)(now - _bdown[u ? B_UP : B_DOWN]) > 700;
        const int step = fast ? 8 : 2;
        s.setParam((uint8_t)_sel, (uint8_t)clampi((int)s.pv[_sel] + (u ? step : -step), 0, 127));
    }
    if (p) { s.noteOn(60, 90); _audEnd = now + 400; }
}

// ---- Init -------------------------------------------------------------------------
void lcd_begin() {
    pinMode(LCD_CS, OUTPUT);  digitalWrite(LCD_CS, HIGH);
    pinMode(LCD_DC, OUTPUT);  digitalWrite(LCD_DC, HIGH);
    pinMode(LCD_RST, OUTPUT); digitalWrite(LCD_RST, HIGH);
    pinMode(LCD_BL, OUTPUT);  digitalWrite(LCD_BL, HIGH);
    for (int p : BPINS) pinMode(p, INPUT_PULLUP);

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
