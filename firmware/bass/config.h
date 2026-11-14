#pragma once
// ============================================================
//  config.h  —  Pico Bass Synth (Moog-style mono bass + step sequencer), Pico 2 (RP2350)
//  PURE #defines only. No #includes, no code, no classes.
// ============================================================

// ---- Feature flags -----------------------------------------
#define ENABLE_LCD        1
#define ENABLE_SERIAL     1
#ifndef ENABLE_USB_HOST          // the *_nohost envs pass -DENABLE_USB_HOST=0
#define ENABLE_USB_HOST   1
#endif
#define MIDI_HOST_DP_PIN  6      // D+ = GP6, D- = GP7
#ifndef ENABLE_STORAGE
#define ENABLE_STORAGE    1      // save / load patterns + sound in flash (LittleFS). See bass_storage.h.
#endif
#define STORAGE_FILE      "/bass.bin"

// ---- Sequencer size -----------------------------------------
#define N_STEPS        16
#define N_PATTERNS     16

// ---- MIDI --------------------------------------------------
#define MIDI_CHANNEL        0    // 0 = OMNI
#define AKAI_REMAP          1    // 1: CC1..CC8 = the 8 knobs of an AKAI MPK mini (see bass_params.h)
#define MIDI_REALTIME       1    // pass clock / start / continue / stop to midi_realtime()
#define BEND_RANGE_SEMI     2.0f // pitch bend range, +-semitones
#define KEY_ROOT           36    // transpose mode: playing C2 = no transposition

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

// ---- Control rate ------------------------------------------
// Envelopes, glide, oscillator pitch and the filter cutoff are updated once per CONTROL_DIV samples (2756 Hz)
// and interpolated (filter coefficient, amplitude) in between. The SEQUENCER counts samples: step times are exact.
#define CONTROL_DIV    16

// ---- Filter ------------------------------------------------
#define FILTER_MIN_HZ  20.0f     // lowest self-oscillation / cutoff frequency
#define FILTER_OCTAVES 8.1f      // range above FILTER_MIN_HZ: 20 Hz .. 5.5 kHz (the closed-form coefficient g reaches 1.0 there; above it the Euler-style stage is unstable)
#define FILTER_STEPS_PER_OCTAVE 16   // table resolution: 1/16 octave, linearly interpolated

// ---- Sequencer ---------------------------------------------
#define VEL_NORMAL     100
#define VEL_ACCENT     127
#define TEMPO_KNOB_BASE 60       // tempo knob 0..127 = 60..187 BPM; serial `bpm` allows 30..300
#define SWING_MAX      0.25f

// ---- Voicing -------------------------------------------------
#define OUT_GAIN       2.5f      // full scale = 1.0: the ladder passband sits well below full scale; softSat() limits the output
#define INPUT_SCALE    0.30f     // oscillator mix -> ladder input
#define ACCENT_VEL     112       // keyboard velocity at or above this counts as an accent

// ---- Misc --------------------------------------------------
#define LED_PIN  25
// Hot-path functions are force-inlined so they end up INSIDE loop1(), which lives in SRAM.
#define HOT      __attribute__((always_inline)) inline

// ---- AKAI MPK mini knobs K1..K8 (factory program: CC1..CC8) -
#define AKAI_K1   1   // -> cutoff
#define AKAI_K2   2   // -> resonance
#define AKAI_K3   3   // -> filter env amount
#define AKAI_K4   4   // -> filter decay
#define AKAI_K5   5   // -> glide
#define AKAI_K6   6   // -> drive
#define AKAI_K7   7   // -> volume
#define AKAI_K8   8   // -> accent
