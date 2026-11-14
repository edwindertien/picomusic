// Renders the panel through the ST7789 emulator -> panel_*.rgb (used by tools/panel_images.py)
#include <Arduino.h>
#include <SPI.h>
#include "strings_manager.h"
volatile uint8_t g_cpu_load_pct = 0;
StringsManager machine;
#include "lcd.cpp"
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




static void settle() { for (int k = 0; k < 200; k++) { g_mock_us += 25000; ui_flush(machine); } }
static void save(const char* p) { FILE* f = fopen(p, "wb"); for (int i = 0; i < LCD_W * LCD_H; i++) { uint16_t c = scr[i]; fputc((c >> 11) << 3, f); fputc(((c >> 5) & 63) << 2, f); fputc((c & 31) << 3, f); } fclose(f); }
static void ticks(int n) { for (int i = 0; i < n; i++) { int16_t l, r; machine.tick(l, r); } }
static void lit() { ui_midi_activity(); ui_flush(machine); g_mock_us += 25000; ui_flush(machine); for (int i = 0; i < 20; i++) { g_mock_us += 1000; ui_flush(machine); } }
int main() {
    Wavetable::init(); for (int p : {2, 18, 16, 20, 3, 15, 17}) g_pin[p] = 1; g_pin_hook = pinHook; SPI1.hook = spiHook;
    machine.init(); lcd_begin();
    // state 1: the classic Strings patch, a 5-note chord, ensemble depth selected
    machine.loadPatch(0); machine.pitchBend(1500); _sel = P_ENS_DEPTH; g_cpu_load_pct = 22;
    for (int n : {48, 55, 60, 64, 67}) machine.noteOn(n, 100);
    ticks(6000); ui_midi_activity(); g_mock_us += 1000; settle(); lit(); save("panel_clean.rgb");
    // state 2: Pizz patch, big chord, pedal down
    machine.panic(); ticks(8000); machine.loadPatch(3); machine.pitchBend(0); machine.setPedal(true); _sel = P_PIZZ; g_cpu_load_pct = 41;
    for (int n : {40, 47, 52, 55, 59, 64, 67, 71, 74, 76, 79, 83}) machine.noteOn(n, 100);
    ticks(5000); ui_midi_activity(); g_mock_us += 1000; settle(); lit(); save("panel_played.rgb");
    printf("patch=%s counted=%d\n", machine.patchName, machine.countedVoices());
}
