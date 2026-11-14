#pragma once
// ============================================================
//  config.h  —  Pico Drum Machine, Pico 2 (RP2350)
//  PURE #defines only. No #includes, no code, no classes.
// ============================================================

// ---- Feature flags -----------------------------------------
#define ENABLE_LCD        1
#define ENABLE_SERIAL     1
#ifndef ENABLE_USB_HOST          // the *_nohost / pico_tone envs pass -DENABLE_USB_HOST=0
#define ENABLE_USB_HOST   1
#endif
#define MIDI_HOST_DP_PIN  6      // D+ = GP6, D- = GP7
#ifndef ENABLE_STORAGE
#define ENABLE_STORAGE    1      // save / load patterns + kit in flash (LittleFS). See drum_storage.h.
#endif
#define STORAGE_FILE      "/drums.bin"

// ---- Machine size ------------------------------------------
#define N_INST         16        // instruments = tracks = pad triggers (2 banks of the MPK's 8 pads)
#define N_STEPS        16        // steps per pattern (a pattern may be shortened, see "length")
#define N_PATTERNS     16
#define SONG_MAX       32        // slots in the song chain

// ---- MIDI --------------------------------------------------
#define MIDI_CHANNEL        0    // 0 = OMNI (drum machines usually listen on 10; omni also catches that)
#define AKAI_REMAP          1    // 1: CC1..CC8 = the 8 knobs of an AKAI MPK mini (see drum_params.h)
#define MIDI_REALTIME       1    // pass clock / start / continue / stop to midi_realtime() (see midi_handler.h)
#define PAD_BASE_NOTE      36    // linear pad map: note 36..51 = instrument 0..15 (the MPK mini's pads). Serial: padbase <n>

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
// Pad triggers are picked up, and every voice's envelopes are advanced, once per CONTROL_DIV samples
// (2756 Hz) and interpolated in between. The SEQUENCER itself counts samples: step times are sample-exact.
#define CONTROL_DIV    16

// ---- Sine table (4096 points, linearly interpolated) --------
#define WAVETABLE_BITS 12
#define WAVETABLE_SIZE (1 << WAVETABLE_BITS)
#define PHASE_SHIFT    (32 - WAVETABLE_BITS)

// ---- Sequencer ---------------------------------------------
#define VEL_GHOST      55        // step level 1
#define VEL_NORMAL     100       // step level 2
#define VEL_ACCENT     127       // step level 3
#define REC_QUANT_NEXT 0.5f      // recording: a hit later than this fraction of a step goes to the NEXT step
#define TEMPO_KNOB_BASE 60       // tempo knob 0..127 = 60..187 BPM (1 BPM per step); serial `bpm` allows 30..300
#define SWING_MAX      0.25f     // swing knob 127 = 75 % (odd steps delayed by up to 25 % of a beat pair)

// ---- Voicing ------------------------------------------------
#define VOICE_GAIN     2.0f      // instrument level at LEVEL 127 (+6 dB so that the default kit sits around LEVEL 100 with headroom above)
#define OUT_GAIN       0.75f     // overall level (full scale = 1.0) at volume 127, drive 0
#define CHOKE_MS       4.0f      // closed hat chokes the open hat in this time
#define KICK_SWEEP_MS  12.0f     // kick pitch-envelope time constant
#define TOM_SWEEP_MS   30.0f

// ---- Misc --------------------------------------------------
#define LED_PIN  25

// Hot-path functions are force-inlined so they end up INSIDE loop1(), which lives in SRAM.
#define HOT      __attribute__((always_inline)) inline

// ---- MIDI CC map -------------------------------------------
#define CC_PAN             10    // GM pan  (selected instrument)
#define CC_MASTER_VOL       7
#define CC_LEVEL           20    // the five instrument CCs act on the SELECTED instrument (the last pad hit)
#define CC_TUNE            21
#define CC_DECAY           22
#define CC_TONE            23
#define CC_TEMPO           24
#define CC_SWING           25
#define CC_DRIVE           26
#define CC_INST            27    // select the instrument for editing (0..15 = value / 8)
#define CC_ALL_SOUND_OFF  120
#define CC_ALL_NOTES_OFF  123

// ---- AKAI MPK mini knobs K1..K8 (factory program: CC1..CC8) -
#define AKAI_K1   1   // -> tune      (selected instrument)
#define AKAI_K2   2   // -> decay
#define AKAI_K3   3   // -> tone
#define AKAI_K4   4   // -> level
#define AKAI_K5   5   // -> tempo
#define AKAI_K6   6   // -> swing
#define AKAI_K7   7   // -> master volume
#define AKAI_K8   8   // -> drive
