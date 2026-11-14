#pragma once
// ============================================================
//  config.h  —  Pico Sampled Piano (Salamander Grand, 4-bit ADPCM in flash), Pico 2 (RP2350)
//  PURE #defines only. No #includes, no code, no classes.
// ============================================================

// ---- Feature flags -----------------------------------------
#define ENABLE_LCD        1
#define ENABLE_SERIAL     1
#ifndef ENABLE_USB_HOST          // the *_nohost envs pass -DENABLE_USB_HOST=0
#define ENABLE_USB_HOST   1
#endif
#define MIDI_HOST_DP_PIN  6      // D+ = GP6, D- = GP7

// ---- Voices --------------------------------------------------
#define N_POLY          24       // the polyphony limit: voices that are held, pedalled or releasing normally
#define N_VOICES        32       // the pool: N_POLY + 8 spare slots in which stolen voices fade out over ~13 ms instead of being cut (a click)
#define KEY_LO          21       // A0
#define KEY_HI         108       // C8
#define N_KEYS          88
#define EVT_RING        64       // core 0 -> core 1 event queue (power of two)

// ---- MIDI --------------------------------------------------
#define MIDI_CHANNEL        0    // 0 = OMNI
#define AKAI_REMAP          1    // 1: CC1..CC8 = the 8 knobs of an AKAI MPK mini (see piano_params.h)
#define MIDI_REALTIME       1    // real-time bytes are parsed out of the stream (so a clock byte can never corrupt a note) and ignored: main.cpp midi_realtime() does nothing
#define BEND_RANGE_SEMI     2.0f

// ---- Audio shield: Waveshare Pico-Audio, two different boards -------------------
// The two boards carry different DACs and different pinouts, and software cannot tell
// them apart. The wrong setting is not "a bit off", it is SILENT. Check the PCB.
//
//   1 = "Rev2.1" (printed on the board)             CS4344 DAC   DIN GP22, BCK GP26, LRCK GP27
//       The board all development so far used. Default.
//   0 = "Initial Version" (no revision marking)     PCM5101A DAC DIN GP26, BCK GP27, LRCK GP28
//
// Switch without editing this file:  build_flags = ... -DAUDIO_BOARD_REV2_1=0
//
// The setting selects the pins AND the PIO program (see audio_driver.h):
//   1  audio_pio.pio      BCK = 64 x fs, each data bit held for two BCK periods. The Rev2.1 accepts
//                         this; it is the organ's proven program.
//   0  audio_pio_i2s.pio  standard I2S, BCK = 32 x fs, one data bit per BCK. The PCM5101A clocks DIN
//                         in on EVERY rising BCK edge (TI SLAS859C) and needs this. The Rev2.1
//                         program sounds badly distorted on it. It generates its own system clock
//                         from BCK (no MCLK needed).
#ifndef AUDIO_BOARD_REV2_1
#define AUDIO_BOARD_REV2_1  1
#endif
#if AUDIO_BOARD_REV2_1
  #define AUDIO_BOARD_NAME        "Rev2.1 (CS4344)"
  #define AUDIO_DATA_PIN          22   // DIN
  #define AUDIO_CLOCK_PIN_BASE    26   // BCK = GP26, LRCK = GP27 (automatic)
#else
  #define AUDIO_BOARD_NAME        "Initial Version (PCM5101A)"
  #define AUDIO_DATA_PIN          26   // DIN
  #define AUDIO_CLOCK_PIN_BASE    27   // BCK = GP27, LRCK = GP28 (automatic)
#endif

// ---- Audio -------------------------------------------------
#define SAMPLE_RATE    44100
#define BUFFER_FRAMES  256
#define CONTROL_DIV    16        // envelopes, release and decay advance once per 16 samples (2756 Hz) and are interpolated

// ---- Voicing -------------------------------------------------
#define OUT_GAIN        0.5f     // full scale = 1.0 at volume 127. A mezzo-forte triad at the default volume peaks near 0.45, leaving room for ten fingers
#define VEL_REF         80       // the recorded velocity layer: velocity 80 plays the sample as recorded (no gain change, no filtering)
#define FAST_RELEASE_S  0.015f   // a re-struck key's old voice and a stolen voice fade out in this time

// ---- Misc --------------------------------------------------
#define LED_PIN  25
// Hot-path functions are force-inlined so they end up INSIDE loop1(), which lives in SRAM.
#define HOT      __attribute__((always_inline)) inline

// ---- AKAI MPK mini knobs K1..K8 (factory program: CC1..CC8) -
#define AKAI_K1   1   // -> tone
#define AKAI_K2   2   // -> hardness (velocity curve)
#define AKAI_K3   3   // -> release (damper)
#define AKAI_K4   4   // -> decay (string)
#define AKAI_K5   5   // -> stereo width
#define AKAI_K6   6   // -> tune
#define AKAI_K7   7   // -> volume
#define AKAI_K8   8   // -> una corda (soft)
