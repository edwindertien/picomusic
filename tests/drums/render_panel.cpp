// Render the real UI code to raw RGB565 files (docs/panel_*.png are made from these).
#include <Arduino.h>
#include <SPI.h>
#include "drum_manager.h"
volatile uint8_t g_cpu_load_pct = 0;
DrumManager machine;
bool storage_save(DrumManager&) { return true; } bool storage_load(DrumManager&) { return true; }
#include "lcd.cpp"
static void settle() { for (int k = 0; k < 400; k++) { g_mock_us += 25000; ui_flush(machine); } }
static void run(long n) { for (long i = 0; i < n; i++) { int16_t l, r; machine.tick(l, r); } }
static void dump(const char* p) { FILE* f = fopen(p, "wb"); fwrite(FB, 2, LCD_W * LCD_H, f); fclose(f); }
static void tap(int pin) { g_pin[pin] = 0; g_mock_us += 10000; ui_handle_buttons(machine); g_pin[pin] = 1; g_mock_us += 10000; ui_handle_buttons(machine); }
int main() {
    Wavetable::init(); for (int p : {BTN_UP, BTN_DOWN, BTN_LEFT, BTN_RIGHT, BTN_PRESS, BTN_A, BTN_B}) g_pin[p] = 1;
    machine.init(); lcd_begin(); settle();
    // GRID: Boom Bap with a tom fill, playing, cursor on the snare
    machine.setPattern(1); machine.setBpm(92);
    for (int s : {13, 14, 15}) machine.setStep(2 + (s - 13), s, 2);
    machine.setStep(5, 10, 1); machine.setStep(6, 12, 3);
    machine.play(); run(7 * 5512 * 92 / 120 + 3000); g_cpu_load_pct = 22; _ct = 1; _cs = 4; machine.selectInst(1); _lastSel = 1; ui_midi_activity(); machine.hit(0, 110); run(600); settle(); dump("/tmp/panel_grid.rgb");
    // INST: snare, TONE selected
    tap(BTN_B); settle(); _sel = P_TONE; machine.setBpm(92); settle(); dump("/tmp/panel_inst.rgb");
    // PATTERN setup
    tap(BTN_B); machine.stop(); run(100); machine.setPattern(1); _pr = R_SYNC; settle(); dump("/tmp/panel_patt.rgb");
    // SONG
    tap(BTN_B); machine.songMode = true; machine.play(); run(2 * 16 * 5512 + 2000); _ss = 5; settle(); dump("/tmp/panel_song.rgb");
    return 0;
}
