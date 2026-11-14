// Render the real UI code to a raw RGB565 file (docs/panel.png is made from it).
#include <Arduino.h>
#include <SPI.h>
#include "piano_manager.h"
volatile uint8_t g_cpu_load_pct = 0;
PianoManager machine;
#include "lcd.cpp"
static void settle() { for (int k = 0; k < 400; k++) { g_mock_us += 25000; ui_flush(machine); } }
static void run(long n) { for (long i = 0; i < n; i++) { int16_t l, r; machine.tick(l, r); } }
int main() {
    for (int p : {BTN_UP, BTN_DOWN, BTN_LEFT, BTN_RIGHT, BTN_PRESS, BTN_A, BTN_B}) g_pin[p] = 1;
    machine.init(); lcd_begin(); settle();
    machine.setPedal(true); for (int k : {36, 48, 55, 60, 64, 67, 71, 76, 79, 84}) machine.noteOn(k, 70 + (k % 7)); run(30000);
    machine.noteOff(36); machine.noteOff(55); run(3000);
    g_cpu_load_pct = 14; ui_midi_activity(); _sel = P_TONE; machine.setParam(P_TONE, 80); settle();
    FILE* f = fopen("/tmp/ppanel.rgb", "wb"); fwrite(FB, 2, LCD_W * LCD_H, f); fclose(f);
    // page 2: the effects (reverb on, tremolo/auto-pan to the pan side, wah on its pedal), a little chorus
    machine.setParam(P_REV, 70); machine.setParam(P_RSIZE, 90); machine.setParam(P_TREM, 100); machine.setParam(P_TRATE, 70); machine.setParam(P_WAH, 60); machine.setParam(P_WPOS, 90); machine.setParam(P_CHOR, 40);
    _sel = P_REV; settle();
    f = fopen("/tmp/ppanel_fx.rgb", "wb"); fwrite(FB, 2, LCD_W * LCD_H, f); fclose(f);
    return 0;
}
