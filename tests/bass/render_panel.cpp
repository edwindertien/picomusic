// Render the real UI code to raw RGB565 files (docs/panel_*.png are made from these).
#include <Arduino.h>
#include <SPI.h>
#include "bass_manager.h"
volatile uint8_t g_cpu_load_pct = 0;
BassManager machine;
bool storage_save(BassManager&) { return true; } bool storage_load(BassManager&) { return true; }
#include "lcd.cpp"
static void settle() { for (int k = 0; k < 400; k++) { g_mock_us += 25000; ui_flush(machine); } }
static void run(long n) { for (long i = 0; i < n; i++) { int16_t l, r; machine.tick(l, r); } }
static void dump(const char* p) { FILE* f = fopen(p, "wb"); fwrite(FB, 2, LCD_W * LCD_H, f); fclose(f); }
static void tap(int pin) { g_pin[pin] = 0; g_mock_us += 10000; ui_handle_buttons(machine); g_pin[pin] = 1; g_mock_us += 10000; ui_handle_buttons(machine); }
int main() {
    bass_tables_init(); for (int p : {BTN_UP, BTN_DOWN, BTN_LEFT, BTN_RIGHT, BTN_PRESS, BTN_A, BTN_B}) g_pin[p] = 1;
    machine.init(); lcd_begin(); settle();
    machine.setBpm(104); machine.setPattern(1); machine.loadPatch(2); machine.play(); run(6 * 6360 + 3000);          // Acid, playing, a little way into the bar
    g_cpu_load_pct = 12; ui_midi_activity(); machine.pitchBend(1200); _ksel[0] = P_MIX2; settle(); dump("/tmp/bpanel_osc.rgb");
    tap(BTN_B); _ksel[1] = P_RES; settle(); dump("/tmp/bpanel_filt.rgb");
    tap(BTN_B); machine.cursor = 5; settle(); dump("/tmp/bpanel_seq.rgb");
    tap(BTN_B); machine.stop(); run(100); machine.keyMode = 1; machine.stepWrite = false; _pr = R_SYNC; settle(); dump("/tmp/bpanel_setup.rgb");
    return 0;
}
