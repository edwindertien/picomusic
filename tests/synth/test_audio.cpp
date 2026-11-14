// Runs the REAL audio_driver.h against the mocked SDK and dumps what it configured and pushed.
#include <Arduino.h>
#include <cmath>
#include "audio_driver.h"
int main() {
    audio_driver_init(44100);
    const int n = 3000;
    for (int i = 0; i < n; i++) {
        int16_t l = (int16_t)(14000 * sin(2 * M_PI * 1000.0 * i / 44100.0));
        int16_t r = (int16_t)(9000  * sin(2 * M_PI * 1700.0 * i / 44100.0 + 1.0));
        audio_driver_put(l, r);
    }
    const PioCapture& c = g_pio_cap;
    printf("BOARD %d\nPROGRAM", AUDIO_BOARD_REV2_1); for (auto w : c.program) printf(" %u", w);
    printf("\nWRAP %u %u\nSIDE %u %u\nPC %u\nEXEC %d\nCLKDIV %.6f\nOUT %u %u\nSET %u %u\nSHIFT %d %d %u\nDIR %u %u\nPINS %u\nENABLED %d\nGPIO",
           c.cfg.wrap_target, c.cfg.wrap, c.cfg.side_bits, c.cfg.side_base, c.initial_pc, c.exec_jmp, c.cfg.clkdiv, c.cfg.out_base, c.cfg.out_cnt,
           c.cfg.set_base, c.cfg.set_cnt, c.cfg.shift_right, c.cfg.autopull, c.cfg.thresh, c.dir_val, c.dir_mask, c.pins_val, c.enabled);
    for (auto g : c.gpio_init) printf(" %u", g);
    printf("\nN %zu\n", c.words.size()); for (auto w : c.words) printf("W %u\n", w);
    fprintf(stderr, "%s", Serial.out.c_str());   // (Serial is not used by init/put; debug printed below)
    Serial.out.clear(); audio_driver_debug(); fprintf(stderr, "%s", Serial.out.c_str());
}
