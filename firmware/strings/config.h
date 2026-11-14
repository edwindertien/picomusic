#pragma once
// ============================================================
//  config.h  —  Pico String Machine, Pico 2 (RP2350)
//  PURE #defines only. No #includes, no code, no classes.
// ============================================================

// ---- Feature flags -----------------------------------------
#define ENABLE_LCD        1
#define ENABLE_SERIAL     1
#ifndef ENABLE_USB_HOST          // the *_nohost / pico_tone envs pass -DENABLE_USB_HOST=0
#define ENABLE_USB_HOST   1
#endif
#define MIDI_HOST_DP_PIN  6      // D+ = GP6, D- = GP7

// ---- Polyphony ---------------------------------------------
// A string machine plays every key. A voice is only a phase-locked saw stack + envelope (the
// registration filters and the ensemble are shared), so voices are cheap.
// MAX_ACTIVE_VOICES: notes that may sound at once (held keys + sustained + release tails).
//                    16 is a CONSERVATIVE default until the CPU square has been checked on the Pico: on a PC
//                    24 voices with every registration + unison cost ~3.5x the analog synth's 12 voices.
//                    Raise it (max 24 for the panel's pip rows) once the square stays green.
// STRINGS_POOL     : physical slots; the spare ones let a stolen voice fade out while the new
//                    note already starts in a free slot (same scheme as the analog synth).
#define MAX_ACTIVE_VOICES  16
#define STRINGS_POOL       24
#define STEAL_RELEASE_MS    6

// ---- MIDI --------------------------------------------------
#define MIDI_CHANNEL        0    // 0 = OMNI
#define AKAI_REMAP          1    // 1: CC1..CC8 = the 8 knobs of an AKAI MPK mini (see strings_params.h)
#define MIDI_PROGCHANGE_PATCHES 1

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
// Envelopes, pitch (vibrato / bend) and the pizzicato decay update once per CONTROL_DIV samples and
// are linearly interpolated in between.
#define CONTROL_DIV    16

// ---- Sine table (LFOs, ensemble) — 4096 points --------------
#define WAVETABLE_BITS 12
#define WAVETABLE_SIZE (1 << WAVETABLE_BITS)
#define PHASE_SHIFT    (32 - WAVETABLE_BITS)

// ---- Voicing ------------------------------------------------
// Registration filters (fixed, like the divider-bus filters of a real string machine).
// Each is a 2-pole state-variable filter fed by one footage bus:
//   BASS  16' -> low-pass      CELLO 8' -> low-pass      VIOLA 8' -> band-pass
//   VIOLIN 4' -> band-pass     PIZZ (8'+4', own decay) -> band-pass
#define FILT_BASS_HZ     260.0f
#define FILT_BASS_Q      0.71f
#define FILT_CELLO_HZ    720.0f
#define FILT_CELLO_Q     1.0f
#define FILT_VIOLA_HZ   1100.0f
#define FILT_VIOLA_Q     0.9f
#define FILT_VIOLIN_HZ  2400.0f
#define FILT_VIOLIN_Q    0.7f
#define FILT_PIZZ_HZ    1600.0f
#define FILT_PIZZ_Q      0.7f
// Per-registration loudness trims (calibrated by host_test/test_dsp.py so that every registration
// is equally loud at middle C).
#define TRIM_BASS   1.205f
#define TRIM_CELLO  1.000f
#define TRIM_VIOLA  2.047f
#define TRIM_VIOLIN 1.932f
#define TRIM_PIZZ   2.308f
#define PZ_ATTACK_MS     2.0f     // pizzicato attack
#define OUT_GAIN         0.15f    // overall level (full-scale = 1.0) at volume 127
#define VIB_MAX_SEMITONES 0.35f   // vibrato depth at depth = 127

// ---- Ensemble (3-tap chorus; one slow + one fast LFO, taps 120 degrees apart) --------------
#define ENS_BUF               2048
#define ENS_D0_MS             5.0f    // centre delay
#define ENS_SLOW_DEPTH_MS     2.4f    // slow LFO sweep (+-) at depth 127
#define ENS_FAST_DEPTH_MS     0.45f   // fast LFO sweep (+-) at depth 127, shimmer 127
#define ENS_FAST_HZ           6.2f

// ---- Misc --------------------------------------------------
#define LED_PIN  25

// Hot-path functions are force-inlined so they end up INSIDE loop1(), which lives in SRAM.
#define HOT      __attribute__((always_inline)) inline

// ---- MIDI CC map -------------------------------------------
#define CC_MASTER_VOL       7
#define CC_EXPRESSION      11    // alias of the master volume
#define CC_BASS            20
#define CC_CELLO           21
#define CC_VIOLA           22
#define CC_VIOLIN          23
#define CC_PIZZ            24
#define CC_DETUNE          26
#define CC_ENS_RATE        27
#define CC_ENS_SHIMMER     28
#define CC_SUSTAIN_PEDAL   64    // damper pedal (>= 64 = down)
#define CC_RELEASE         72
#define CC_ATTACK          73
#define CC_TONE            74
#define CC_PIZ_DECAY       75
#define CC_VIB_RATE        76
#define CC_VIB_DEPTH       77
#define CC_ENS_MIX         93    // GM "chorus send"
#define CC_ENS_DEPTH       94    // GM "celeste / detune depth"
#define CC_ALL_SOUND_OFF  120
#define CC_ALL_NOTES_OFF  123

// ---- AKAI MPK mini knobs K1..K8 (factory program: CC1..CC8) -
#define AKAI_K1   1   // -> tone
#define AKAI_K2   2   // -> attack
#define AKAI_K3   3   // -> release
#define AKAI_K4   4   // -> ensemble depth
#define AKAI_K5   5   // -> ensemble rate
#define AKAI_K6   6   // -> ensemble mix
#define AKAI_K7   7   // -> volume
#define AKAI_K8   8   // -> vibrato depth
