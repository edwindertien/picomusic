# picomusic: technical handoff (context.md)

Read this before changing anything. README.md is the user-facing document; this file is for whoever (human or Claude) continues the work.

## Project-wide rules

- **One repository, seven instruments** (tonewheel, synth, strings, bass, drums, piano, dx7). Selection is only through `platformio.ini` (`pio run -e <inst>`). Instrument sources live in `firmware/<inst>/`, shared code in `firmware/shared/`. Do not put `#ifdef INSTRUMENT` in shared code.
- **Hardware:** every instrument runs on the same tower: Pico 2, USB shield PCB (MIDI in GP5, USB host D+/D- GP6/GP7), Pico-Audio, Pico-LCD-1.14. Only the tonewheel has its own keyboard; the others were tested mainly with an AKAI MPK mini.
- **Audio board:** one switch, `[audio] rev2_1` in platformio.ini (default 0 = PCM5101A "Initial Version"). It sets `-DAUDIO_BOARD_REV2_1`, which picks pins and PIO program. Wrong setting = silent. Each `config.h` keeps a fallback default of 1 that the build flag overrides.
- **Threading contract (all instruments):** core 0 = MIDI/UI/serial/libm; core 1 = audio loop in SRAM, no libm, no locks, SPSC rings and single-word mailboxes; panic/all-notes-off are counters, not events.
- **Tests:** `tests/run_tests.sh [inst] [quick]` stages `firmware/shared` + `firmware/<inst>` flat into `tests/.build/<inst>/src` and runs the suites (written for flat projects) unchanged. `tests/.build` is disposable. Verify with the tests before updating docs.
- **Paths inside the per-instrument notes below** were written for stand-alone projects: read `src/` as `firmware/<inst>/` (+ `firmware/shared/`), `host_test/` as `tests/<inst>/`, `tools/` as `tools/<inst>/`. `tools/piano` detects both layouts (`bankconf/core.py`).
- Piano effects live in `firmware/shared/stereo_fx.h` (shared with dx7); `piano_fx.h` is a thin wrapper.
- No Pico 1 builds (PIO2 + M33 cycle counter needed).
- Offline builds: PlatformIO needs `tool-scons`; if the registry is unreachable use a local stub package.
- Deliver updates as one zip; update README/doc only after changes are verified.

## DX7 (firmware/dx7), added in the merge

- Engine: 6 sine operators, 32 algorithms (`DX7_ALG`, msfa layout), log units 256 = octave, amplitude `2^((A-3840)/256)`, sine table 4096, amp table 4352. Modulator phase shift = 2 x amp cycles; feedback = (y0+y1)*0.25*2^(fb-7) cycles. Fine = fraction of coarse; coarse 0 = 0.5.
- Files: `dx7_patch.h` (VMEM pack/unpack, algorithm table, scaling), `dx7_params.h` (16 params, CC map), `dx7_presets.h` (12 voices), `dx7_manager.h` (voices, envelopes on the core-1 control tick, stealing), `midi_handler.h` (plus SysEx), `lcd.cpp/h`, `main.cpp`.
- SysEx: `F0 43 0n 09 20 00 <4096> cs F7`, parsed from UART and USB CIN 4-7; banks not persisted.
- Not implemented: pitch EG, osc key sync, LFO sync. Envelope/LFO shapes approximate. **Confirmed working on hardware (author, Nov 2026); CPU unmeasured** (N_POLY 12, reduce if red).
- Tests: `tests/dx7` (algorithms, Bessel sidebands, SysEx, race, audio driver, demo render).
- Pitfalls found: negative left shift in velocity scaling (use `*16`), name copy overread in preset (bounded loop).

## Open items (project level)

- Chase remaining small errors on every instrument (all confirmed working on hardware, Nov 2026).
- Confirm USB shield opto wiring and pass-through against the real board.
- Tonewheel AKAI knob map (`AKAI_REMAP`, main.cpp) is compiled but untried on hardware.
- Author to add: project licence, more photos.

## Drum editor (tools/drums), added after the merge

- `drum_gui.py` -> Flask app `drumconf/app.py` + single-page `drumconf/static/index.html` (vanilla JS). `drumconf/core.py` = document model (JSON), `to_image`/`from_image` = the 4247-byte machine image (same layout + CRC32 as `DrumManager::serialize`; zlib.crc32 == firmware crc32), library, projects. `drumconf/engine.py` stages `firmware/shared` + `firmware/drums` + `tests/drums/plat` and compiles `engine/drum_render.cpp` (real DrumManager) against `tests/drums/mock`; render modes pattern [loop] / song / hit / kit. `drumconf/device.py` = pyserial link (`dump`, `image <hex>`, `save`, `stop`, `hit`).
- Firmware: `drum_hex.h` + serial commands `dump` / `image <hex>` in `firmware/drums/main.cpp` (line limit raised to the image size only for lines starting `image `). Refused while playing.
- Loop render: stop the sequencer 500 samples before the end, then fold the tail onto the start (otherwise the next round's first hit is doubled and clips).
- Library: `build_library.py` writes `library/{patterns,kits,sounds}.json`; user entries in `library/user/`; projects in `projects/`. Patterns are textbook grooves, not checked by ear; kits/sounds are offsets of the factory kit.
- Tests: `tests/drums/test_editor.py`, `test_hex.cpp` (run by `tests/run_tests.sh drums`). USB protocol tested against a fake machine only: **not tried on the real Pico**.

## Tonewheel AKAI map (firmware/tonewheel/main.cpp, config.h AKAI_REMAP)

K1 volume, K2 drive, K3 chorus, K4 Leslie (thirds), K5..K8 drawbar groups (16'+5-1/3', 8'+4', 2-2/3'+2', 1-3/5'+1-1/3'+1'). Translated inside `midi_cc()` into the organ's own CCs. Compiles; not tried on hardware. Demo renders for tonewheel/synth: `tests/render_demos.sh` (uses `tests/{tonewheel,synth}/render.cpp`; not part of run_tests.sh).

---

# Per-instrument notes (carried over unchanged from the stand-alone projects)



<!-- ===== tonewheel ===== -->

## [tonewheel]

## Project Context — Pico Tonewheel Organ (tone generator)

Technical handoff for developers and for new AI-assisted sessions. Read it together with `README.md`
(what it is and how to build it). This file is about **why it works the way it does and what will break if you change it.**

Status: all items below were confirmed working on the hardware by the author unless marked *(host-verified only)*.

---

### 1. What this is

Dual-unit Hammond-style organ. This repository is the **tone generator**: Raspberry Pi **Pico 2 (RP2350)**,
Waveshare Pico-Audio (I²S; Rev 2.1 = CS4344 or Initial Version = PCM5101A, selected by `AUDIO_BOARD_REV2_1`, see 4.3) and Pico-LCD-1.14 (ST7789V). Input = MIDI from UART (GP5), USB host (GP6/GP7, one device)
and the native USB port. Companion: the *pico MIDI keyboard* unit (RP2040) that sends notes and CC12–27.

Toolchain: PlatformIO, `https://github.com/maxgerhardt/platform-raspberrypi.git`, earlephilhower core, Adafruit TinyUSB (3.7.x),
FortySevenEffects MIDI library, vendored Pico-PIO-USB v0.5.3 in `lib/pio_usb/`.

### 2. Current feature set

- 16-note (pool of 20) polyphonic tonewheel synthesis, 9 partials/note, drawbars, percussion (first-note-only), key click
- Effects chain: overdrive → chorus → **Leslie (stereo)**; vibrato inside the voices
- **Load governor** (adaptive polyphony with graceful fade-out) and `perf` console command
- Control by MIDI CC, joystick + LCD, serial console. CC map in section 6.
- USB MIDI host: **one** device (hub OK for one device)

### 3. Layout

```
platformio.ini            envs: pico2_tone (240 MHz, full) · pico2_nohost (200 MHz) · pico_tone (OUTDATED, see 9)
include/                  all headers (header-only DSP)
  config.h                every tunable + CC map + helper midi_switch3()
  tonewheel_manager.h     voice pool, spinlock, tick(), effect order, governor
  tonewheel_voice.h       one note: 9 partials, compact index list, tickRaw(), applyVibrato(), fade
  effects.h               Overdrive · Vibrato · Chorus · Leslie, _fx_sin()
  perf.h                  DWT cycle counter (raw registers, ARM-only; host stub)
  drawbars.h percussion.h click.h oscillator.h wavetable.h
  midi_handler.h usb_host.h audio_driver.h audio_pio.pio.h audio_pio_i2s.pio.h
  lcd*.h  (UI in src/lcd.cpp)     mclk.h voice.h voice_manager.h  = unused/reference
src/  main.cpp  lcd.cpp  audio_pio.pio  audio_pio_i2s.pio
lib/pio_usb/              vendored + compatibility stub (section 8)
docs/                     figures; docs/src/make_figures.py regenerates them
```

### 4. Architecture

#### 4.1 Cores and the shared organ
- **Core 0 `loop()`**: `midi_handler_poll` → `organ.serviceGovernor()` → `usb_host_poll` → `ui_handle_buttons` → LCD flush every 33 ms → `handleSerial` (1 char per pass). **No `delay()`** in `loop()`.
- **Core 1 `loop1()`** (`__not_in_flash_func`): per 256-sample buffer: for each sample `organ.tick(L,R)` then `audio_driver_put(L,R)` (interleaved), then `organ.governor(cycles, budget)`, then the LCD load value.
- One global `TonewheelManager organ`. Core 0 writes under `spin_lock_unsafe_blocking`; core 1 `spin_try_lock_unsafe` — it renders even if the lock is taken (worst case one slightly stale sample) and **never waits**.
- Parameters are `uint8_t`/enum single writes (atomic on ARM): no lock needed from core 0.

#### 4.2 Hard rules (each cost hours; do not "simplify")
1. **Never `spin_lock_blocking()`** — disables interrupts → TinyUSB drops microframes → USB MIDI dies under chord bursts.
2. **Never double-pass audio** (compute buffer, then push). The PIO FIFO holds a few samples (~100–180 µs). A starved FIFO stops LRCK; the **CS4344 hardware-mutes** (the PCM5101A likewise stops when its clocks halt) (shows up as noise whose pitch falls with load, then silence). Always interleave compute and put.
3. **Core 1 must not call**: `Serial`, LCD/SPI, LittleFS, NeoPixel `show()`. Core 1 must never block.
4. **Audio code runs from SRAM.** Only `loop1` was in RAM; everything it called was an ordinary function in *flash*, executed through the 16 KB XIP cache that core 0 (never idle) constantly evicts → large unpredictable stalls. Fix: `__attribute__((always_inline))` on `tick`, `Overdrive::process`, `Chorus::process`, `Leslie::process`, `tickRaw`, `_fx_sin`. Verify with `arm-none-eabi-g++ -S` that the audio path is one call-free function (currently ~1230 static instructions; calls only to cold `memset`/`sqrtf` error branches).
5. **Init order**: `audio_driver_init()` (claims PIO2) **before** `usb_host_init()` (TinyUSB takes PIO0 TX / PIO1 RX — 22 and 32 of 32 slots; PIO1 is exactly full).
6. **TinyUSB host callbacks must not make `_sync` calls** — blocks `USBHost.task()` and starves hub enumeration.
7. **No `float` in the per-oscillator path.** Hardware FPU is fine elsewhere, but the vibrato float convert/multiply per partial per sample was ~half the voice cost.
8. **DWT**: CMSIS `CoreDebug`/`DWT` are not exposed in arduino-pico. Use raw addresses `0xE000EDFC` (DEMCR bit 24), `0xE0001000` (CTRL bit 0), `0xE0001004` (CYCCNT). The DWT is **per core**: enable it on core 1.

#### 4.3 PIO / clocks
RP2350 has 3 PIO blocks: PIO0 USB TX · PIO1 USB RX · PIO2 I²S (13 instr., the original Waveshare program, `.side_set 2`, **do not use the earlephilhower I²S library** — BCK/LRCK bit order is incompatible with the board wiring). 240 MHz gives exact PIO-USB divisors (TX ÷5, RX ÷2.5); the audio clkdiv is computed from `clock_get_hz()`.

**Two Pico-Audio boards (`AUDIO_BOARD_REV2_1`).** Two physically different Waveshare boards share the name. `config.h` selects the board (default `1` = Rev 2.1 / CS4344; `0` = Initial Version / PCM5101A; overridable with `-DAUDIO_BOARD_REV2_1=0`) and `audio_driver.h` derives **pins and PIO program** from it:

| | Rev 2.1 | Initial Version |
|---|---|---|
| DIN / BCK / LRCK | GP22 / GP26 / GP27 | GP26 / GP27 / GP28 |
| Program | `audio_pio.pio` | `audio_pio_i2s.pio` (8 instructions) |
| BCK · PIO cycles per frame | 64 × fs · 128 (clkdiv ≈ 42.5 at 240 MHz) | 32 × fs · 64 (clkdiv ≈ 85.0 at 240 MHz) |
| `audio_driver_put(l, r)` word | `l << 16 \| r` | `r << 16 \| l` (LRCK = 0 is the left channel and the low half-word) |

The Rev 2.1 program holds every data bit for **two** BCK periods; the Rev 2.1 accepts that. The PCM5101A clocks DIN in on **every** rising BCK edge (TI SLAS859C), so that program sounds severely distorted on it (measured by emulation: ≈ 2 dB SNDR through a standard receiver; the only receiver that recovers it samples every *second* edge). The Initial Version needs standard I²S, BCK = 32 × fs, which the datasheet lists for 16-bit data — consistent with the earlier finding that the earlephilhower I²S library (standard I²S) does not work on the Rev 2.1. **Never assume a PIO audio program is portable between DACs.** `audio_pio_i2s.pio.h` was assembled from its `.pio` source by a small hand assembler that reproduces `audio_pio.pio.h` word for word; replace it with `pioasm` output if preferred. The wrong `AUDIO_BOARD_REV2_1` value is **silent** (wrong pins); the boot banner prints the selection.

### 5. DSP design notes

#### 5.1 Voice engine (`tonewheel_voice.h`, `tonewheel_manager.h`)
- Partial = `{phase, inc, baseInc, amp, pad}` (16 bytes, one record per drawbar index) + compact list `_idx[]` of sounding partials (level>0, freq<22 kHz). `_idx` entries are always valid indices 0..8, so a racy read from core 1 can glitch one sample but never crash.
- `tickRaw()` returns the **unscaled** Σ(sine·amp); the manager applies `×559241 >> 32` (= ÷7680 = the old `>>8`, `/10`, `/3`). int32 headroom: 180 partials × 32767 × 256 < 2³¹.
- **Vibrato**: LFO runs every `VIBRATO_BLOCK`=16 samples; multiplier in **Q30** (`VIB_ONE = 1<<30`); `applyVibrato(q)` does `inc = baseInc·q >> 30` per sounding partial. New/retuned voices get the current state immediately (`setVibratoState`, `_updatePartials` re-applies). Q16 was *not* enough (phase error at high partials). Block-rate vs per-sample: same sound; noise floor below −93 dB.
- **Fade**: stolen voices fade over `VOICE_FADE_SAMPLES` (Q16 gain); `fading` voices are not "held" (`currentNote=255`, excluded from counts and from the duplicate-note check). A hard cut measured 24× the signal's own step (a click); the fade measures none.
- `_fx_sin(phase)` = parabola + refinement (error ≈ 0.001). **The previous polynomial was wrong at the ends of its range (±0.54 → a 1.08 step once per LFO cycle)** — this was the real cause of the long-chased "chorus loop-around click". Callers must pass phase ≥ −0.5 (truncation = floor).

#### 5.2 Percussion / click
First-note-only trigger; 8′ drawbar −25 % while percussion is on; level tracks `drawbarSum`. Click level scale: CC84 → 0–255 internal; LCD/serial use 0–127 (a pre-existing inconsistency, harmless).

#### 5.3 Chorus
Dual quadrature BBD: two 2048-sample delay lines, LFOs 90° apart (`sin`, `cos`), centre 20 ms, swing `depth·2/127` ms, rate 0.2–3 Hz, wet 0–50 % by `mix`. 5 ms swing (the first version) gave ±44 cents — far too much.

#### 5.4 Overdrive (`Overdrive`, effects.h) — redesigned, measured
Signal path: split at 120 Hz (bass stays clean, delayed 7 samples) → gain 0…`OD_MAX_GAIN_DB` (linear in dB, `OD_GAIN_CURVE`=1.0) → **2× oversampling** (15-tap half-band, coefficients 0.313925991 / −0.093403660 / 0.044110376 / −0.026487629, polyphase, doubled ring buffers) → asymmetric cubic soft clipper (`c(u)=u−u³/3`, ±2/3; negative half `0.6·c(u/0.6)`) with **first-order ADAA** (antiderivative F, threshold |du|>2e-3 else midpoint) → half-band decimation → 2× one-pole LPF (16 kHz → 5 kHz with drive) → DC blocker (R=0.9986) → **loudness-following makeup** `m = k(drive)·√(Ex/Ey)` (detectors on the *distorted-band input* and the *processed band*, 20 ms attack / 5 ms release, updated every sample) → + clean bass. 10 ms on/off crossfade; parameters from a 128-entry table built in the constructor (no libm in the audio path).
- Why: a plain waveshaper at this drive **aliases at ≈ −30 dB** (inharmonic hash = "noise"); ADAA+2× brings it to ≈ −55 dB (ear-weighted ≈ −58…−61), the old effect's floor, with ~10 dB more distortion energy.
- Measured with: single note A4 (all partials harmonics of 220 Hz → anything between harmonics is aliasing), 5 test signals (1 quiet note … six-note full drawbars), A-weighted level vs clean.
- **Makeup calibration** (do it again if the clipper/filters/curve change): with 0 dB makeup the chain's A-weighted level vs clean was `0.089 − 1.163·s` dB, `s=(d/127)^1.3` (fit error ≤ 0.25 dB). So `k_dB = (OD_LEVEL_DB + 1.163)·s − 0.089`. Result: +0.1 dB at low drive → +2.6 dB at max (mean), all signals within ~2 dB.
- Rejected: updating the gain every 4 samples (sample-and-hold images → up to 7 dB worse aliasing at mid drive); gain > 36 dB (no extra clipping at max, more aliasing); fixed makeup (dense chords end up quieter than clean).
- Cost ≈ 250–300 cycles/sample, only while drive>0. Pitfalls seen: `1/g` compensation made it quieter; the asymmetric shaper alone leaves ~−100 LSB DC.

#### 5.5 Leslie (`Leslie`, effects.h)
Crossover 800 Hz, **bilinear (TPT) one-poles**, 12 dB/oct each side, high band polarity-inverted so the bands sum to an allpass (exactly flat; the naive `high = x − low` leaked bass into the horn and a plain one-pole form drooped 1 dB at HF). Horn: delay swing ±`LESLIE_HORN_DOPPLER_MS`(0.5) (≈ ±2.2 % pitch at 6.7 Hz) + AM 60 %; drum: low band delayed 7 samples + AM 40 %. Two mics a quarter turn apart → stereo. `gain = (1−D/2) + (D/2)·cos θ`, `delay = base − A·cos θ`. Rotor speeds approach their targets exponentially (`ACCEL_S`/`DECEL_S` per rotor). Stop: when both rotors < `LESLIE_STOP_HZ`, the wet weight fades (0.4 s) and the effect **bypasses bit-exactly** (zero CPU). Makeup ×1.25 (level within ~1.5 dB of dry on typical registrations). Mode comes from `leslie.setSwitch(cc27)`; `LESLIE_POS_*` map the switch positions.

#### 5.6 Load governor (`TonewheelManager::governor`, `serviceGovernor`)
Per 256-sample buffer on core 1: cycles in the voice loop vs. everything else → `_perOsc` (cycles/oscillator/sample) and `_rest` (cycles/sample), **rising fast (α 0.2), falling slowly (α 0.03)** (underestimating is the dangerous direction). Capacity = `(GOV_TARGET_LOAD_PCT%·budget − rest)/(perOsc·partialsPerNote)`, clamped to `[GOV_MIN_VOICES, MAX_ACTIVE_VOICES]`. `voiceLimit`: down at once; up +1 after `GOV_UP_BUFFERS` (~1 s) of spare capacity. **Emergency**: measured load > `GOV_EMERGENCY_PCT` → limit = held·target/load (proportional). Core 0 `serviceGovernor()` fades out held notes beyond the limit; `noteOn` steals the oldest held note the same way. Synthetic tests: converges in ~80 ms, no flapping, recovers; a heavy chord is overloaded for at most one buffer. `perf` prints the live numbers.
Context: before this, the engine (float vibrato on all partials, effects, everything in flash) overran at 6 notes (pitch −½ semitone) and froze at 7 (DAC mute).

### 6. MIDI map (config.h)

| CC | Meaning |
|----|---------|
| 12–20 | drawbars 1–9 |
| 21 vol · 22 drive · 23 chorus amount (depth+mix) | pots |
| 24 percussion · 25 vibrato · 26 click | three-state: 2 / 62 / 126 → `midi_switch3()` thresholds 32 / 94 |
| 27 | Leslie: low=slow, mid=stop, high=fast (`LESLIE_POS_*`) |
| 80–84 | percussion on/off, harmonic, decay, level; 84 click level |
| 86–90 | vibrato depth/rate, chorus depth/rate/mix |
| PC 0–3 | Full / Jazz / Flute / off |
`MIDI_CC_VOLUME` was 7, then 10; it is now **21**. `MIDI_CC_DRIVE` was 85; now **22** (85 unused). The keyboard unit's 16 analog controls = CC12–27 exactly.
Dispatch order in `midi_cc()`: print everything → volume → `handleFxCC` → `handleCC` (drawbars/perc/click).

### 7. Serial commands
`help info all db pre perc click vol drive vib d|r cho d|r|m|a les stop|slow|fast perf pio`. Every received CC is printed.

### 8. USB host facts and compatibility stub
- Needs the **latest** Adafruit TinyUSB (host MIDI class `midi_host.*` does not exist in 3.4.x). Callbacks (3.7.x): `tuh_midi_mount_cb(idx, const tuh_midi_mount_cb_t*)`, `tuh_midi_umount_cb(idx)`, `tuh_midi_rx_cb(idx, xferred)`, `tuh_midi_tx_cb(idx, xferred)`, `tuh_midi_packet_read(idx, uint8_t[4])`.
- `hcd_pio_usb.c` (inside TinyUSB) calls `pio_usb_host_endpoint_close()`, absent in v0.5.3 → stub (declaration in `pio_usb.h`, body at the end of `pio_usb_host.c`).
- 15 kΩ pull-down on D+ (GP6) = host mode. One device works; **two devices behind a hub enumerate but only the first gets its MIDI interface** — cause not isolated (hub/deferred-attach path, probably the old pio-usb). To investigate: `-DCFG_TUSB_DEBUG=2` with `SERIAL_TUSB_DEBUG Serial`; likely fix: use the Pico-PIO-USB bundled inside TinyUSB.
- AKAI MPK mini: needs to be present at power-up; its current draw can put noise on the audio — diode + 100 µF + 100 nF.
- Do not call `_sync` descriptor functions in `tuh_mount_cb`.

### 9. Known issues / not done
- Two USB devices through a hub (above).
- **`pico_tone` (RP2040) env is stale**: `perf.h` reads the M33 DWT (hard fault on M0+).
- Exact cycle costs unmeasured on hardware; numbers in the docs are static instruction-count estimates. Use `perf`.
- Velocity ignored (intentional). Pitch bend supported.
- Initial Version (PCM5101A) path: confirmed on hardware in the analog synth with the identical driver; on this organ it is host-verified only until flashed here.
- Open question for docs: pin the TinyUSB version in `platformio.ini`.

### 10. How the DSP was verified (host harness + cross-compiler)
Not in the repo (yet). Recreate: compile the real headers on a PC with `g++ -std=gnu++17` against tiny stubs — `Arduino.h` (min/max/Serial no-ops), `hardware/sync.h` (`spin_lock_*` no-ops), and `wavetable.h`'s `sineTable`. `perf.h` already has a host fallback. Tests that exist:
- **CC mapping** (2/62/126 for all switches, pots, no collision with CC12–20)
- **Voice-engine equivalence** old vs new, same event script: ±3 LSB (vibrato off); with the LFO held identically ±14 LSB (−69 dB)
- **Leslie**: ramp timing, bit-exact bypass, Doppler/AM depth, L/R correlation, level vs dry on real organ signals, click-free mode switching
- **Overdrive**: Python prototype (`scipy`) for design, C++ float32 matches it (aliasing −57.4 vs −57.7 dB); A-weighted loudness over 5 signals × drives; aliasing via the "harmonics of 220 Hz" trick; edge cases (on/off switching, pot sweep, silence, extreme input)
- **Governor / fade**: synthetic cost plants (pessimistic, optimised, effects toggled, wrong model), hard-cut vs fade click test
- **`main.cpp`** compiled on the host with stubs and driven through `handleSerial()` (one char per call!) and `midi_cc()`
- **Audio driver, both boards** *(host-verified on the organ; hardware-verified in the analog-synth project, which has the identical driver)*: the real `audio_driver.h` against a mocked pico-sdk PIO API that records the program, config, clkdiv, pins and every pushed sample; replayed through a PIO emulator and decoded with an I²S receiver model (rising-edge sampling). Rev 2.1: program byte-identical, 44100 Hz. Initial Version: bit-exact both channels. Lives in the analog synth repo (`host_test/test_audio.py`, run with `AUDIO_SRC=../include:../src`).
- **Cross-compile** `arm-none-eabi-g++ -mcpu=cortex-m33 -mfloat-abi=softfp -mfpu=fpv5-sp-d16 -O2 -S` to inspect inlining, calls, loop instruction counts (oscillator loop ≈ 15 integer instructions; whole audio path one function).
Lessons: measure every "inaudible" optimisation (the 4-sample gain hold cost 5–7 dB of aliasing); a test must run the code the way the firmware does (e.g. `tick()` so fades finish).

### 11. Working agreement
Hardware-first, incremental: each change is tested on the instrument before the next. Deliver **only the changed files** (keeping `include/` vs `src/`); update README/context at verified breakpoints, not mid-debugging. When debugging goes in circles, revert to the last known-good state and rebuild incrementally. State uncertainty plainly.

### 12. Next ideas
- Companion keyboard unit: documentation + link in the README.
- Combined MIDI-in + USB-host board.
- Fix the hub / two-device case (newer pio-usb).
- Further modules on the same platform — **analog synth** (VCO/VCF/ADSR, Moog ladder: `s += g·(tanh(in) − tanh(s))` per stage; call `tanf` only on cutoff change), **FM**, **electric piano** (samples), **drum machine** (different main loop). Shared: `audio_driver.h`, `midi_handler.h`, `usb_host.h`, LCD drawing primitives, `oscillator.h`, `wavetable.h`, `perf.h`, the governor pattern and the effects (Leslie/chorus/overdrive are generic). Only voice engine, manager and UI differ.
- If CPU ever becomes limiting again: a **shared bank of 91 tonewheels** (fixed cost independent of the number of notes: per sample 91 table lookups + per-wheel weights updated on note on/off); it changes the partial tuning slightly to equal temperament and removes the need for voice stealing.


<!-- ===== synth ===== -->

## [synth]

## Project Context — Pico Analog Synth (v3)

Hand-off document for the `picosynth` firmware. Read it together with `README.md` (user-facing: controls, CC map, panel) before changing anything. This file records the architecture, the rules that keep it correct, what was learned the hard way, how it is tested, and what is open.

State as of **November 2026**: the restored v3 build runs on the hardware (Pico 2, Waveshare Pico-Audio Rev 2.1, Pico-LCD-1.14, AKAI MPK mini on the USB host). Stuck notes, broken voice stealing, lag and the missing display reported against v2 are resolved. Both Waveshare Pico-Audio versions (Rev 2.1 / CS4344 and Initial Version / PCM5101A) are supported through `AUDIO_BOARD_REV2_1` and verified on hardware; the same driver has been ported to the tonewheel organ. Not yet recorded: measured CPU load on the Pico (see [Open items](#15-open-items)).

---

### 1. What this is

![The complete setup](docs/fullsetup.jpg)

*Complete setup: AKAI MPK mini on the USB host, synth module, two speakers.*

![The assembled module](docs/stack.jpg)

*The module: Pico-LCD-1.14 (top), Pico-Audio (middle), Pico 2 (bottom) in a 3D-printed holder.*

A polyphonic subtractive synth (2 wavetable oscillators + noise → 4-pole ladder filter → amp, two ADSRs, LFO vibrato, bend, mono glide, tube overdrive) on the same platform layer as the Pico Tonewheel Organ (`tonewheel_usb`, V1.0): I²S on PIO2, USB host on PIO0/PIO1, MIDI from USB device + USB host + UART, ST7789 panel on SPI1. Platform files are shared **verbatim** with the organ: `audio_driver.h` + `audio_pio.pio(.h)` + `audio_pio_i2s.pio(.h)` (the two-board audio driver — keep the copies in sync), `usb_host.h` (only the manager type differs), `midi_handler.h` (product name differs), `effects.h`, `lib/pio_usb/`, `platformio.ini`.

Engine/UI files are the synth's own. The synth was rebuilt (v3) on the organ's V1.0 architecture because v2 could not be made reliable (section 2).

### 2. History: why v3 exists

| Version | What happened |
|---|---|
| v1 (`analog_synth`) | Hand-written PIO header (wrong encodings, wrong init name, no `pio_version` for RP2350) → **no sound** until the organ's real `audio_pio.pio.h` was copied. 6-page bar-graph UI. |
| v2 (`analog_synth2`, `analog_synth_v2`) | Worked, but: stuck notes, broken stealing, clicks/pops at max filter env, lag, then the display had to be disabled. |
| **v3** (`analog_synth_v3`) | Restored on the organ V1.0 architecture, new voice-ownership model, new LCD pipeline. |
| v3 + two shields (Nov 2026) | Initial Version (PCM5101A) board support: distorted with the organ's PIO program (it holds each bit for two BCK periods), fixed with a standard-I²S program selected by `AUDIO_BOARD_REV2_1`. Ported to the organ. |

Root causes found for v2 (all addressed in v3):

1. **Envelope state written by both cores.** Core 0 set `RELEASE` while core 1 advanced `ATTACK→DECAY→SUSTAIN`; if both wrote in the same sample the release was lost → a **stuck note**. (Most probable cause of the reported hangs; v3 removes the possibility rather than proving the old failure on hardware.)
2. **Voice overwrite from core 0 while core 1 rendered it**, and an idle slot kept a stale `note` that, when reused, cleared *another* voice's note-map entry → hung notes.
3. **Display:** a full redraw was ≈ 65 000 single-byte SPI calls. In arduino-pico, `SPI.transfer(uint8_t)` does a register write plus a blocking round trip *per byte*; core 0 was stuck > 100 ms, so USB polling stalled. (`transfer(tx, nullptr, n)` is a tight `spi_write_blocking` loop — verified in arduino-pico 5.2.1 `SPI.cpp`.)
4. **Filter instability:** above ≈ 14 kHz nominal the Euler coefficient is unstable (measured peak 2.8× at 16 kHz, 11.6× at 20 kHz). Base cutoff (≤ 4 kHz) + envelope sweep (≤ 12 kHz) reaches it: the "click/pop at max env".
5. **CPU:** per-sample cutoff computation (`pow2` + quantise), per-sample pitch maths, waveform `switch` per sample, `tanf()` on every sample at one stage, hot code possibly executing from flash.
6. **Process errors** (see section 10): stale files in the project directory, per-translation-unit `static` arrays in headers, incomplete `lib/pio_usb`.

### 3. Files

| File (`src/`) | Lines | Role |
|---|---|---|
| `config.h` | 139 | Pure `#define`s: feature flags, **audio shield selector**, polyphony (cap / pool / steal fade), control rate, sound constants, CC map, AKAI knobs, the `HOT` macro |
| `synth_params.h` | 98 | `ParamId`, `PARAM_INFO[]` (CC, colour group, grid cell), `PATCHES[][]`, `paramForCC()` — the single source of truth |
| `synth_manager.h` | 356 | `SynthManager`: parameter → engine derivation, note allocation / stealing / glide (core 0), `tick()` (core 1) |
| `synth_voice.h` | 169 | `SynthVoice` + `EngineParams`: the cross-core contract, `control()` (per 16 samples), `render()` (per sample) |
| `synth_env.h` | 65 | Linear ADSR advanced in blocks; time/rate helpers (core 0) |
| `synth_filter.h` | 80 | Huovilainen ladder, Padé `tan`, stability limit, `sane()` |
| `synth_osc.h` | 23 | Phase-accumulator oscillator (one table read per sample) |
| `wavetable.h` | 20 | `Wavetable` interface: 4 waveforms, `table(w)` pointer accessor |
| `wavetable.cpp` | 25 | Table storage (defined once) and generation |
| `effects.h` | 194 | The organ's `Overdrive` (used), `Vibrato` and `Chorus` (unused here). Shared file, keep identical to the organ |
| `lcd.h` | 17 | Public interface: `lcd_begin`, `ui_flush`, `ui_handle_buttons`, `ui_midi_activity`, `ui_mark_all_dirty` |
| `lcd.cpp` | 454 | Knob panel: widgets with change keys, dirty rectangles, bulk SPI, joystick/buttons, ST7789 init |
| `main.cpp` | 300 | `setup`/`loop` (core 0), `loop1` (core 1, SRAM, CPU load), MIDI callbacks, event log, serial commands, audio-board banner |
| `midi_handler.h` | 164 | USB-MIDI device + UART MIDI parser (organ's file; product name "Analog Synth") |
| `usb_host.h` | 112 | USB MIDI host via Pico-PIO-USB (organ's file; manager type differs) |
| `audio_driver.h` | 91 | PIO2 I²S driver, **selects pins + PIO program by `AUDIO_BOARD_REV2_1`**, `audio_driver_put` (swaps half-words for the I²S program), `audio_driver_debug`. Shared with the organ |
| `audio_pio.pio` | 83 | Rev 2.1 (CS4344) program: BCK 64 × fs, each bit held 2 BCK (organ's, verbatim) |
| `audio_pio.pio.h` | 85 | Assembled header for the above (organ's, verbatim — do not hand-edit) |
| `audio_pio_i2s.pio` | 72 | Initial Version (PCM5101A) program: standard I²S, BCK 32 × fs, 1 bit per BCK |
| `audio_pio_i2s.pio.h` | 84 | Header assembled from the `.pio` source by a hand assembler (reproduces the organ's header word for word); replaceable by `pioasm` output |

| Other | Role |
|---|---|
| `lib/pio_usb/` | Vendored Pico-PIO-USB 0.5.3 (14 files, with the `endpoint_close` stub) |
| `host_test/` | PC test harness: mocks, `run_tests.sh`, `test_filter/engine/lcd.cpp`, `render_panel.cpp` |
| `tools/` | `make_figures.sh`, `gen_tables.py`, `diagrams.py`, `panel_images.py`, `audio_timing.py` — regenerate `docs/` and the README tables |
| `docs/` | Figures used by README.md and context.md. Generated by `tools/`: `panel*.png`, `pipeline.png`, `cores.png`, `voices.png`, `audio_timing.png`. Hand-supplied photos (not generated): `fullsetup.jpg`, `stack.jpg` |

Shared with the organ: `audio_driver.h`, `audio_pio.pio(.h)`, `audio_pio_i2s.pio(.h)`, `effects.h`, `lib/pio_usb/*`, `platformio.ini` (the audio files were extended for two boards and ported to the organ in Nov 2026). `usb_host.h` differs only by `#include "synth_manager.h"` and `extern SynthManager synth;`.

Dependency rule: `config.h` is **pure `#define`s** (it also defines the `HOT` force-inline macro). Nothing in `config.h` may include another project header.

### 4. Cross-core contract (the part that must not be broken)

Core 0 = `loop()`: MIDI, USB host, buttons, LCD, serial. Core 1 = `loop1()`: audio only. There is **no spinlock** in the synth; correctness comes from ownership:

| # | Rule | Where |
|---|---|---|
| R1 | Core 0 may write **any** voice field only while `live == 0`, then publishes: `__dmb(); live = 1;` | `SynthManager::_start` |
| R2 | While `live == 1` core 0 may only: increment `relReq` / `fastReq` (release / steal-fade request), write `tgtInc` (glide target), read `aenv.level` (stale is fine) | `noteOff`, `_retire`, `noteOn` (glide) |
| R3 | Core 1 consumes requests by comparing the counters with its private `relSeen` / `fastSeen` (counters only increase, so no request is lost and none is applied twice) | `SynthVoice::control` |
| R4 | Core 1 hands a voice back by `__dmb(); live = 0;` as its **last access** to it (envelope reached IDLE during the previous block, so the amp ramp is already at zero) | `SynthVoice::control` |
| R5 | Core 1 reads the `live` flags into a bitmask, **then** `__dmb()` (acquire), then renders the live voices | `SynthManager::tick` |
| R6 | Core 0 executes `__dmb()` (acquire) at the start of `_start`, pairing with R4 | `SynthManager::_start` |
| R7 | Shared parameters are single words (`float`/`uint8_t`/`uint32_t`) in `EngineParams ep`, `ep.bend`, `_modRatio`. They may be stale by one control block. **Never** build an invariant across two of them. | `EngineParams`, `_derive` |
| R8 | Core 1's hot path: no libm, no `Serial`, no `malloc`, no LittleFS, no flash-resident callee (`HOT` = `always_inline`; `loop1` is `__not_in_flash_func`) | `config.h`, `main.cpp` |
| R9 | **All** manager calls come from `loop()` context (single thread). If one ever arrives from an interrupt, add locking — **not** `spin_lock_blocking` (it disables interrupts and loses USB-MIDI packets) | — |

Why the organ's `spin_try_lock_unsafe` pattern was *not* copied: the organ's voice has no envelope state, so a torn read is harmless there. The synth's voice has a state machine written by core 1, so core 0 must never write it.

### 5. Control-rate design

`CONTROL_DIV = 16` samples (2.756 kHz). Per voice, a private countdown `ctr` triggers `control()` (so voices naturally stagger by their start time). `start()` calls `control()` once to prime the first block.

`control()` does, in order: finished-check (IDLE → hand back) → apply release/fast-release requests → `advance(16)` both envelopes → amp ramp (`amp = ampTgt; ampTgt = env.level; ampStep = (ampTgt−amp)/16`) → glide + pitch ratio → both oscillator increments → waveform pointers → filter target `g` (+ `gStep`) → filter sanity → mixer flags.

Global control (`_globalControl`, once per 16 samples): LFO (sine from the wavetable, or sample & hold) and bend → `_modRatio = 2^(semi/12)` by a 5th-order exponential series (|semi| ≤ 4, error < 3·10⁻⁷). Voices multiply their increment by it.

`tan(π f/fs)` is a Padé approximation (error 0.036 % over 20 Hz…12 kHz), so core 1 never calls libm.

Release/steal fade: linear, `STEAL_RATE = 1000/(STEAL_RELEASE_MS·fs)` per sample.

### 6. Voice allocation (`SynthManager`, core 0)

Bookkeeping (core 0 only): `_held[]`, `_dying[]`, `_note[]`, `_age[]`, `_map[128]` (note → slot of the voice whose key is down), `_noteInc[128]`, `_clock`, `_lastInc`.

`noteOn(n)`:
1. `_map[n]` set (same key again, note-off lost) → `_retire` the old voice.
2. Glide on and a held voice exists → **legato**: write `tgtInc`, remap, return.
3. While `countedVoices() ≥ MAX_ACTIVE_VOICES`: `_pickVictim` (quietest voice whose key is up, else oldest held) → `_retire` (fast release, `_dying = true`, stops counting).
4. `_findFree()`; if none, `_waitFree()` (≤ `STEAL_RELEASE_MS + 4` ms, ticks nothing — core 1 does the work); then `_start`.

Invariants (checked by `test_engine` after every event):
- `live ∧ held ⇒ map[note] == slot` and not dying;
- `map[n] ≠ NONE ⇒ voice live ∧ held ∧ note == n`;
- counted ≤ `MAX_ACTIVE_VOICES`, live ≤ `SYNTH_POOL`.

`allNotesOff` = normal release of held voices; `panic` = fast release of everything (CC 120, button A, serial `panic`).

### 7. Filter

`SynthFilter`: `s += g·(sat(in) − sat(s))` ×4, resonance `res4 · s4` subtracted from the input, input drive ×1.2 (inherited). `sat` = Padé tanh-like (default) or the division-free cubic (`FILTER_CHEAP_SAT 1`).

Measured (resonance 0): the real −3 dB point is **0.22–0.26 × nominal** (nominal 1000 Hz → 226 Hz; 4000 → 1050; 8000 → 3326; ≥ 12000 fully open). The instrument was voiced with this; the new filter reproduces the old response within 1 %. Nominal cutoff: `30·2^(CUT/18) + FENV/127·env·12000`, clamped to `FILTER_MAX_HZ` (12 000). A true-Hz mapping would be a different (brighter) instrument — make it an option, not a silent change.

### 8. Parameters, CCs and patches

`synth_params.h` is the single source of truth: `ParamId` enum, `PARAM_INFO[]` (label, name, serial name, CC, colour group, grid cell), `PATCHES[][]`, `paramForCC()`.

**Add a parameter**
1. Add it to `ParamId`. Knob parameters must come **before** `P_LFOWAVE` (`N_KNOBS == P_LFOWAVE`); parameters without a knob go after it.
2. Add its `PARAM_INFO` row: unique CC (`test_params` enforces uniqueness), group, and a **free grid cell** (unique; the free cell is row 0 / col 7; row 2 cols 5–7 belong to the status block).
3. Add the matching **column** to every `PATCHES` row (same order as the enum). `test_params` catches a shifted row through the VOL / GLD / LFOW sentinels.
4. Add its `case` to `SynthManager::_derive` (knob 0..127 → engine value; this runs on core 0, `powf` is fine there).
5. Add the engine field to `EngineParams` and read it in `SynthVoice::control` / `render`.
6. Add the `#define CC_…` to `config.h`.
7. Regenerate the documentation: `tools/make_figures.sh` rewrites `docs/` and writes `tools/.gen/t_cc.md`, `t_akai.md`, `t_patches.md` from the headers — paste those over the three README tables.

**Add a patch:** add a name to `PATCH_NAMES`, a row to `PATCHES`, bump `N_PATCHES`.

CC routing notes: with `AKAI_REMAP 1`, CC1–8 are claimed by the MPK knobs; the functions normally on CC1/3/4/5 (mod wheel, LFO rate, LFO wave, glide time) are on CC 76–79 instead. **CC 64 is the amp sustain level** (inherited), not a damper pedal.

### 9. LCD (`lcd.cpp`)

Panel: ST7789 240×135, RGB565, SPI1 (62.5 MHz requested), RAM offset **x+40, y+53**, init sequence identical to the organ's.

![control panel while playing](docs/panel_played.png)

*Rendered by `tools/panel_images.py` from the real `lcd.cpp` (ST7789 emulator): Init patch, 8 voices, bend down, CPU 58 % (orange), RES selected. Colour groups: green osc, cyan filter, orange amp env, magenta filter env, yellow master / FX / modulation.*

- Full framebuffer `FB[240·135]` (64.8 KB) in RAM + 4 KB staging buffer `TXB` for byte-swapping (RGB565 goes out big-endian).
- **Widgets:** ids `0..N_KNOBS−1` = knobs, `W_TOP`, `W_STAT`. Each widget has a change key (`_knobKey[]`, `_topKey`, `_statKey`); `scan()` runs every 20 ms, redraws a widget into `FB` when its key changed and sets its bit in `_pending`.
- **Push:** `ui_flush()` sends **one** pending rectangle per call (`pushRect`: set window, then chunked bulk `SPI1.transfer(TXB, nullptr, n)`), so `loop()` keeps polling USB between rectangles. The very first call paints the whole screen once.
- Callers never mark anything dirty (state is polled); `ui_midi_activity()` only stamps a time, and the MIDI dot dims by itself because the top-bar key includes "is the dot lit".
- Knob arcs/pointers use precomputed integer tables (`ARC_DX/DY`, `DOT_DX/DY`, 109 steps of 2.5°, bottom-left → bottom-right); no `sinf`/`cosf` while drawing.

Geometry (all in `lcd.cpp`):
```
cellX(col) = col·29 + (col ≥ 4 ? 8 : 0)        col 7 ends exactly at x = 239
cellY(row) = 19 + row·38                       cell = 29 × 37 px, knob centre = (cellX+14, cellY+22), R = 12
top bar    y 0..17        separator x = 120      status block = cell (col 5,row 2) .. right edge
```
Buttons are sampled every 5 ms (debounce); UP/DOWN/LEFT/RIGHT repeat after 350 ms at 70 ms; step 2, or 8 after 700 ms; `WAVE` steps one waveform per press (value = wave·32 + 16).

**Add a widget:** give it an id < 32 (`_pending` is a `uint32_t`), a rect in `widgetRect`, a `draw…` function that repaints its whole rect, a key computed in `scan()`.

### 10. Audio loop and boot order

`setup()` order matters: `midi_handler_init` → `Serial` → `Wavetable::init` → `synth.init` → `audio_driver_init` (**claims PIO2 first**) → `usb_host_init` (takes PIO0/PIO1) → `lcd_begin`. `setup1()` is just `delay(500)` (core 1 starts after core 0 has finished).

`loop1()` runs 256 samples per call: `tick()` then `audio_driver_put()` per sample (blocks until the PIO FIFO has room, which paces the loop). **Never compute a whole buffer and then push it** — the FIFO holds only 8 samples; a long compute-only pass starves LRCK and the CS4344 auto-mutes (~200 ms of stalled LRCK).

**Two audio boards.** `audio_driver.h` picks the pins and the PIO program from `AUDIO_BOARD_REV2_1`. Rev 2.1: `audio_pio.pio`, 128 PIO cycles per frame, clkdiv = sys/(128·fs) ≈ 42.5 at 240 MHz (BCK 2822.4 kHz), word `left<<16 | right`. Initial Version: `audio_pio_i2s.pio`, 64 cycles per frame, clkdiv = sys/(64·fs) ≈ 85.0 (BCK 1411.2 kHz), word `right<<16 | left` (the half sent while LRCK = 0 is the left channel). The PCM510xA also mutes when BCK/LRCK stop, so the interleave rule above applies to both DACs. `pio` (serial) prints board, divider, BCK and LRCK.

CPU load: DWT cycle counter via raw registers (`0xE000EDFC`, `0xE0001004`, `0xE0001000`; CMSIS structs are not exposed in arduino-pico), budget `F_CPU·256/44100` cycles, fast-attack/fast-decay IIR, shown as the top-bar square and in `stat`.

### 11. Build and toolchain gotchas

1. `audio_pio.pio.h` must be the organ's generated file. A hand-assembled one produced silence.
2. **`static` data in a header is one copy per translation unit.** Tables initialised in one `.cpp` stay zero in another. That is why `Wavetable` storage lives in `wavetable.cpp`, and why the organ's `lcd_driver.h` (`static _tile_buf`) must not be included from two files.
3. `ENABLE_USB_HOST`: `config.h` uses `#ifndef … #define … 1`, so the `-DENABLE_USB_HOST=0` envs don't trigger a redefinition warning.
4. `lib/pio_usb/` must be complete (14 files); the TinyUSB hcd needs `pio_usb_ll.h`.
5. `usb_host.h` needs the **complete** `SynthManager` type (`#include "synth_manager.h"`), not a forward declaration.
6. When files are renamed or removed between versions, **delete the old ones from `src/`** — a leftover `lcd.cpp` redefined the new stub's functions; a leftover `wavetable.cpp` clashes with a new `Wavetable` API. Replace the file set together.
7. `F_CPU` is a build macro (PlatformIO passes `board_build.f_cpu`), usable in `constexpr`.
8. `pico_tone` (RP2040) cannot build audio: `audio_driver.h` uses `pio2`. Use `pico2_tone`.
9. The Waveshare Pico-Audio has **no USB socket**; the USB-A host port is external wiring on GP6/GP7.
10. `Serial.printf("%.2f")` works in this core; the event log defers all printing out of the MIDI callbacks.
11. The AKAI MPK mini enumerates reliably only after a short power-on delay on the USB host.
12. **Never assume a PIO audio program is portable between DACs.** The organ's program holds every data bit for two BCK periods; the CS4344 on the Rev 2.1 accepts it, the PCM5101A (which clocks DIN in on every rising BCK edge) reads each bit twice and sounds severely distorted. This was diagnosed without a scope by emulating the PIO program from its assembled words and decoding the pins with a receiver model, after first validating the receiver on a known-good standard-I²S program (≈230 dB vs ≈2 dB). Do the same for any new DAC: find its datasheet receive rule first, then check the program against it.
13. `AUDIO_BOARD_REV2_1` defaults to 1 and is overridable with `-D`; it cannot be detected in software, and the wrong value is silent (Initial Version pins on a Rev 2.1 board, or the reverse). The boot banner prints the selection.

### 12. Test harness (`host_test/`)

The real source files compile and run on a PC against mocked Arduino / Pico headers: `mock/Arduino.h` (clock, GPIO, `String`, `Serial`), `mock/SPI.h`, `mock/hardware/sync.h`, and `plat/` replacements for the three hardware-bound platform headers. `stage.sh` copies `src/` into `stage/` swapping in `plat/`. `run_tests.sh [quick]` builds and runs everything (filter, display, engine, audio driver; the engine/display/filter suites under **AddressSanitizer + UBSan + float-cast-overflow**).

| Suite | Notes |
|---|---|
| `test_filter` | A/B against the previous filter (`old/synth_filter_body.h`), `tan` accuracy, bounded output at any cutoff, NaN/runaway detection |
| `test_engine` | `#define private public` (after the std headers) exposes core-0 bookkeeping for invariants. `delayHook` runs audio while core 0 "waits" (single-thread tests). Real two-thread tests: audio flat out (worst-case race windows) and real-time paced |
| `test_audio` | Runs the **real** `audio_driver.h` against `mock/hardware/pio.h` (records program, config, clkdiv, pins, pushed words), replays it with `pio_emu.py` (PIO emulator for the opcodes used + I²S receiver model) and checks both boards. Rev 2.1: program byte-identical to the organ's, 44100 Hz, half-rate receiver recovers the audio. Initial Version: bit-exact both channels. `AUDIO_SRC=../include:../src` tests the tonewheel layout |
| `test_lcd` | Includes `lcd.cpp` directly (reaches `FB`, `_sel`, `_pending`) and **emulates the ST7789**: decodes `0x2A/0x2B/0x2C`, applies the +40/+53 offsets, rebuilds the screen from the actual byte stream |

Results at hand-off: filter pass; display pass; engine pass — 250 000-event torture (3 670 steals, invariants after every event), two-thread race 1.26 M control events vs 13.1 M samples, no stuck voices, no sanitizer findings.

**ThreadSanitizer:** build `test_engine` with `-DTSAN_MODEL -fsanitize=thread` and `mock/tsan_sync.cpp`; `__dmb()` is then modelled as `__tsan_release/__tsan_acquire` on one address (TSan ignores `volatile` ordering and C++ fences). `./test_engine threads` runs only the race test. The remaining reports (23 distinct pairs) are exactly the designed shared words: `relReq`/`fastReq`, `live`, `ep.*`, `aenv.level` reads, `_modRatio`. **No** report shows core 0 *writing* the envelope / filter / oscillator state of a live voice (core 0 only *reads* `aenv.level`, by design) — that is the empirical check of rules R1/R2. Any new report outside that list is a bug.

**Not covered:** ARM code generation and real timing, memory ordering on a real Cortex-M33 (x86 is stronger), real USB, the analogue chain, anything audible.

### 13. Key numbers

| Item | Value |
|---|---|
| Voice | 144 B × 20 = 2.9 KB; `SynthManager` ≈ 3.9 KB; wavetables 4 × 8 KB = 32 KB |
| LCD | framebuffer 64.8 KB + 4 KB staging; first paint 22 SPI calls / 64 811 B; one knob 6 calls / 2 157 B; largest rectangle 8 651 B ≈ 1.15 ms (assuming ≈ 60 MHz SPI) |
| Audio clocks (240 MHz) | Rev 2.1: BCK 2822.4 kHz (64 × fs), clkdiv ≈ 42.5, 128 cycles/frame. Initial Version: BCK 1411.2 kHz (32 × fs), clkdiv ≈ 85.0, 64 cycles/frame |
| Control rate | 16 samples = 2.756 kHz; steal fade 4 ms; `_waitFree` bound 8 ms |
| Levels | `VOICE_SCALE` 4800 × VOL/255; default VOL 80 → ≈ 3000 per voice (of 32767), same as v2 at its default |
| PC benchmark (x86, relative only) | 12 sustained voices 144 → 122 ns/sample; 12 voices with a moving filter envelope 156 → 106 ns/sample; `FILTER_CHEAP_SAT` no difference on x86 |

### 14. Decisions that look odd but are deliberate

- Filter input drive `×1.2`, `VOICE_SCALE 4800`, default VOL 80: reproduce the level and character of v2.
- `FILTER_MAX_HZ 12000`: stability, not taste (section 2, item 4).
- `MOD_MAX_SEMITONES 2.0`: parity with v2 (the organ uses 0.25).
- VOL is applied **after** the overdrive (as in the organ), so drive character is independent of volume.
- Attack/decay/sustain are captured at note-on; **release is read at key-up**.
- Patches are complete parameter sets.
- Minimum envelope time is 3 ms, to avoid clicks.
- Glide = mono legato; the next note after silence glides from the last pitch.
- Waveform, detune, mix, cutoff, resonance, vibrato apply live to held notes (read each control block).
- `CC 64` = amp sustain level (inherited, documented trap).

### 15. Open items

Ordered by value:

1. **Record hardware numbers.** CPU % with 1 / 6 / 12 held notes (+ the 4 ms steal burst, + LCD updating, + USB host active). Check the map file / `nm` that `loop1` and the voice code sit in RAM (`0x2000…`), not flash (`0x1000…`); if `SynthVoice::*` appears as separate symbols it was not inlined. Then decide whether to raise `MAX_ACTIVE_VOICES` / `SYNTH_POOL` or try `FILTER_CHEAP_SAT 1` (unmeasured on the Pico).
2. **Sustain pedal.** Move amp sustain off CC 64 to a free number, then implement the damper (hold flag + deferred releases in `noteOff`).
3. **Chorus.** `effects.h` already has the dual-quadrature chorus; it needs depth/rate/mix parameters. The panel is full (only row 0 / col 7 is free), so use CC-only parameters or a second page.
4. **Velocity** → amplitude (and optionally filter), with a curve.
5. **Band-limited oscillators** (PolyBLEP or mip-mapped wavetables) for high notes.
6. **True-Hz filter mapping** as an option (see section 7).
7. **Patch storage** (LittleFS) — must not run on core 1.
8. USB hub with two devices (limited by the vendored `pio_usb` v0.5.3).
9. **Obergransad (Pico W, RP2040) audio** — it has its own driver (official `<I2S.h>`, not these PIO programs) and the same `AUDIO_BOARD_REV2_1` name. Its Rev 2.1 path has not been checked against the finding in section 11 (item 12). Needs the project sources.

### 16. Working conventions for this project

- Hardware-first, incremental: change one thing, flash, report; verify new code on the host harness before flashing.
- Deliver changed files individually (not a re-zipped project) so each change can be diffed.
- Update README / this file **after** a change is verified on the hardware.
- Copy platform files verbatim from the organ; don't re-derive them.
- Figures in `docs/` are generated by `tools/` (matplotlib + the LCD emulator); they are documentation only and not needed to build.


<!-- ===== strings ===== -->

## [strings]

## Project Context — Pico String Machine

Hand-off document for the `picostrings` firmware. Read it with `README.md` (user-facing: controls, CC map, panel). This file records the architecture, the rules that keep it correct, how the voicing was calibrated, how it is tested, and what is open.

State: a **new project** (November 2026), built on the platform and rules of the analog synth (`picosynth`) and the tonewheel organ. Engine, display and audio driver are verified **on a PC**; **nothing has been flashed**. The voicing constants are a first guess to be tuned by ear.

---

### 1. What this is

A polyphonic string machine (ARP/Solina style): phase-locked 16′/8′/4′ saws per key with a slow envelope, shared registration filters, a 3-tap ensemble chorus, stereo output, sustain pedal. Same hardware as the synth: Pico 2 (RP2350), Waveshare Pico-Audio (either version, `AUDIO_BOARD_REV2_1`), Pico-LCD-1.14, MIDI from USB device + USB host + UART.

**Shared platform files** (keep identical across the three projects): `audio_driver.h`, `audio_pio.pio(.h)`, `audio_pio_i2s.pio(.h)`, `usb_host.h` (only the manager type differs), `midi_handler.h` (product name differs), `lib/pio_usb/`, `platformio.ini`. The LCD renderer (`lcd.cpp`) is the synth's with string-machine specifics. The engine is new.

### 2. Files

| File (`src/`) | Lines | Role |
|---|---|---|
| `config.h` | 147 | Pure `#define`s: feature flags, polyphony (cap / pool / steal fade), audio-shield selector, control rate, **voicing constants** (filters, trims, ensemble, level), CC map, AKAI knobs, `HOT` |
| `strings_params.h` | 95 | `ParamId`, `PARAM_INFO[]` (CC, group, grid cell, display format), value-mapping helpers (`attackSeconds()`…), `PATCHES[][]`, `paramForCC()` — the single source of truth |
| `strings_manager.h` | 312 | `StringsManager`: parameter derivation, allocation / stealing / **sustain pedal** (core 0), `tick()` = voices → buses → registration filters → tone → ensemble → limiter (core 1) |
| `strings_voice.h` | 155 | `StringVoice` + `EngineParams`: the cross-core contract, PolyBLEP saws, envelope, pizzicato; `control()` per 16 samples, `render()` per sample |
| `strings_dsp.h` | 91 | Shared DSP: `Svf` (registration filters), `Ensemble` (3-tap chorus), `softSat` (limiter) |
| `wavetable.h` | 10 | Sine table interface (LFOs and ensemble only) |
| `wavetable.cpp` | 7 | Sine table storage (defined once) and generation |
| `lcd.h` | 17 | Public interface: `lcd_begin`, `ui_flush`, `ui_handle_buttons`, `ui_midi_activity`, `ui_mark_all_dirty` |
| `lcd.cpp` | 442 | Knob panel (same renderer as the analog synth): widgets with change keys, dirty rectangles, bulk SPI, joystick/buttons, ST7789 init; string-machine value formats and status block |
| `main.cpp` | 301 | `setup`/`loop` (core 0), `loop1` (core 1, SRAM, CPU load), MIDI callbacks, event log, serial commands, audio-board banner |
| `midi_handler.h` | 164 | USB-MIDI device + UART MIDI parser (organ's file; product name "String Machine") |
| `usb_host.h` | 112 | USB MIDI host via Pico-PIO-USB (organ's file; manager type differs) |
| `audio_driver.h` | 91 | PIO2 I²S driver; selects pins + PIO program by `AUDIO_BOARD_REV2_1`. Shared with the synth and the organ (keep identical) |
| `audio_pio.pio` | 83 | Rev 2.1 (CS4344) program (organ's, verbatim) |
| `audio_pio.pio.h` | 85 | Assembled header (organ's, verbatim — do not hand-edit) |
| `audio_pio_i2s.pio` | 72 | Initial Version (PCM5101A) standard-I²S program |
| `audio_pio_i2s.pio.h` | 84 | Header assembled from the `.pio` source |

| Other | Role |
|---|---|
| `lib/pio_usb/` | Vendored Pico-PIO-USB 0.5.3 (14 files, with the `endpoint_close` stub) |
| `host_test/` | PC harness: mocks, `run_tests.sh`, `test_dsp.py`, `test_engine.cpp`, `test_lcd.cpp`, `test_audio.py`, `probe.cpp`, calibration scripts, `bench.cpp` |
| `tools/` | `make_figures.sh`, `gen_tables.py`, `diagrams.py`, `voicing.py`, `panel_images.py`, `render_demo.py` — regenerate `docs/` and the README tables |
| `docs/` | Figures and the demo recording used by README.md. Generated by `tools/`, except `audio_timing.png`, which is copied from the analog synth project (its `tools/audio_timing.py` draws it from the real PIO programs) |

Dependency rule: `config.h` is **pure `#define`s** (it also defines `HOT`, the force-inline macro). Nothing in it may include another header.

### 3. Cross-core contract (identical to the analog synth — do not break)

Core 0 = `loop()`: MIDI, USB host, buttons, LCD, serial. Core 1 = `loop1()`: audio only. **No spinlock.**

| # | Rule | Where |
|---|---|---|
| R1 | Core 0 writes **any** voice field only while `live == 0`, then publishes: `__dmb(); live = 1;` | `StringsManager::_start` |
| R2 | While `live == 1` core 0 may only increment `relReq` / `fastReq` and read `levelNow()` | `noteOff`, `_retire`, `setPedal` |
| R3 | Core 1 consumes requests by comparing the counters with private `relSeen` / `fastSeen` (counters only increase: nothing is lost or applied twice) | `StringVoice::control` |
| R4 | Core 1 hands a voice back with `__dmb(); live = 0;` as its **last access** (the envelope reached IDLE during the previous block, so the amp ramp is already at zero) | `StringVoice::control` |
| R5 | Core 1 reads the `live` flags into a bitmask, **then** `__dmb()`, then renders the live voices | `StringsManager::tick` |
| R6 | Core 0 executes `__dmb()` at the start of `_start`, pairing with R4 | `_start` |
| R7 | Shared parameters are single words in `EngineParams ep`, `ep.bend`, `_modRatio`; they may be stale by one control block. Never build an invariant across two of them | `_derive` |
| R8 | Core 1 hot path: no libm, no `Serial`, no `malloc`, no flash-resident callee (`HOT` = `always_inline`; `loop1` is `__not_in_flash_func`) | `config.h`, `main.cpp` |
| R9 | All manager calls come from `loop()` context. If one ever arrives from an interrupt, add locking — **not** `spin_lock_blocking` (it disables interrupts and loses USB-MIDI packets) | — |

The registration filters and the ensemble hold state that only core 1 touches. Their **coefficients are written once at `init()`** (so there is no race); the only live-tunable sections are single-word parameters (`toneA`, `ensDepth`, `ensMix`, `ensShimmer`, `ensSlowInc`, weights).

### 4. Signal chain and its numbers

`voices → buses (16′, 8′, 4′, pizz) → Svf per registration → Σ weights → tone one-pole → Ensemble → gain → softSat → int16 L/R`.

- **Voice** (`strings_voice.h`): `ph` is the 16′ accumulator; 8′ = `ph << 1`, 4′ = `ph << 2` (phase-locked). Per footage `t = (float)phase / 2³²`, `dt = increment / 2³²`; `sawBlep(t, dt) = 2t − 1 − polyBlep(t, dt)`. A footage with `dt ≥ 0.45` is skipped (`ok16/ok8/ok4`). `use16/use8/use4/usePz` (from the registration levels) decide which saws are computed at all. Unison: a second accumulator `ph2` at the 8′ rate × `detuneRatio`, averaged into the 8′ saw. Random start phase per note (`seed`) so a retriggered key does not add coherently to its own tail.
- **Envelope:** per-voice linear `level` (ATTACK → HOLD → RELEASE → IDLE), advanced 16 samples per control tick; the amplitude is `level²`, ramped linearly per sample (`amp += ampStep`). Release time is read at key-up (`p.relRate`); a steal uses `STEAL_RATE`.
- **Pizzicato:** `pz *= pzCoef` per control tick (exponential), `gate` attacks in `PZ_ATTACK_MS` and releases with the REL rate; pizz amplitude = `pz × gate`; the pizz bus is `0.5·(saw8 + saw4)`.
- **Registration filters:** TPT state-variable (Zavalishin), `g = tan(πf/fs)`, band-pass output scaled to unity peak gain. BASS and CELLO use the low-pass output, VIOLA / VIOLIN / PIZZ the band-pass. Designed at init with `tanf` (core 0).
- **Ensemble:** buffer 2048 floats; centre delay `ENS_D0` = 5 ms; per tap `d = D0 + depth·(AS·sinSlow + AF·shimmer·sinFast)` samples, taps offset by 120° (`0x55555555`) in both LFOs; linear interpolation; `L = x·dry + wet·(t0 + 0.6 t1)`, `R = x·dry + wet·(t2 + 0.6 t1)`, `dry = 1 − 0.45·mix`, `wet = 0.62·mix`. The 120° spacing makes the three offsets sum to zero (tested).
- **Vibrato / bend:** one global ratio per 16 samples (`_globalControl`): `2^(semi/12)` by a 5th-order series (|semi| ≤ 4).
- **Output:** `softSat(x·gain)` (Padé tanh-like; linear for small signals), `gain = OUT_GAIN · (VOL/127)²`.

### 5. Voice allocation and the pedal (`StringsManager`, core 0)

Bookkeeping (core 0 only): `_held[]` (key down), `_sust[]` (key up, pedal holds it), `_dying[]` (fast release requested), `_note[]`, `_age[]`, `_map[128]` (note → slot of the voice whose **key is down**), `_noteInc16[]`, `_clock`, `_pedal`.

`noteOn(n)`: retire a still-held voice for the same key → while `countedVoices() ≥ MAX_ACTIVE_VOICES` `_pickVictim` and `_retire` → `_findFree` (else `_waitFree`, ≤ `STEAL_RELEASE_MS + 4` ms) → `_start`.
`_pickVictim`: the quietest live, not-dying voice with the key **up** (release tail or pedal-sustained, by `levelNow()`), else the oldest held voice.
`noteOff(n)`: unmap; if the pedal is down mark `_sust` instead of requesting a release. `setPedal(false)`: release every `_sust` voice. `allNotesOff()` releases held and sustained voices; `panic()` fast-releases everything.

Invariants (checked by `test_engine` after every event of the torture test): held ⇒ mapped, not sustained, not dying · sustained ⇒ live, not held, not dying · `map[n] ≠ NONE` ⇒ live ∧ held ∧ `note == n` · counted ≤ cap · live ≤ pool.

### 6. Parameters, CCs and patches

`strings_params.h` is the single source of truth. **Add a parameter:**
1. add it to `ParamId` (order = joystick navigation order; every parameter has a knob, `N_KNOBS == N_PARAMS`);
2. add its `PARAM_INFO` row: unique CC (tested), group, a **free grid cell** (tested unique, not in the status block: row 2, columns 5–7), display format, label ≤ 4 characters;
3. add the column to **every** `PATCHES` row (same order as the enum; the VOL sentinel test catches a shifted row);
4. add its `case` to `StringsManager::_derive` (runs on core 0; `powf` is fine there) and the field to `EngineParams`;
5. use it in `StringVoice` / the chain on core 1;
6. add `#define CC_…` to `config.h`;
7. run `tools/make_figures.sh` and paste `tools/.gen/t_cc.md`, `t_akai.md`, `t_patches.md` over the README tables.

**Add a patch:** a name in `PATCH_NAMES`, a row in `PATCHES`, bump `N_PATCHES`; then `host_test/calibrate_patches.py` for an equal-loudness volume. **Add a registration:** a bus in `StringVoice::render`, an `Svf` in `StringsManager`, a weight and a flag in `EngineParams`, a parameter (above).

CC routing: with `AKAI_REMAP 1`, CC1–8 are the MPK knobs (tone, attack, release, ensemble depth/rate/mix, volume, vibrato depth). CC 11 aliases volume, CC 64 is the **sustain pedal**, CC 93/94 are the GM chorus-send / celeste-depth meanings (ensemble mix / depth).

### 7. Voicing and calibration (how the constants were set)

1. **Filter frequencies / Q** (`FILT_*`): chosen from how the registrations are described (bass and cello dark low-passes, viola / violin / pizz band-passes of rising centre frequency). **Not derived from a reference unit — tune by ear.**
2. **`TRIM_*`**: after changing a filter, run `python3 host_test/calibrate_trims.py`. It plays each registration alone at middle C (mix 0, tone open) and prints trims that equalize their RMS to the cello's. `test_dsp.py` asserts all five within 1 dB at middle C (measured 0.15 dB). Away from middle C they differ by design: the filters are fixed, so the bass fades above ~C5 and viola/violin grow with pitch.
3. **`OUT_GAIN`**: set so that a 6-note chord on the Ensemble patch peaks near −10 dBFS (0.3 FS) at its volume, with room for ten fingers; `softSat` only bends above ~0.6.
4. **Patch volumes**: `calibrate_patches.py` equalizes the RMS of a 6-note chord across the four patches (done: 90 / 85 / 72 / 112).
5. **Envelope / ensemble constants** were chosen for typical string-machine behaviour (slow attack, long release, 5 ms centre delay, slow sweep ±2.4 ms, fast shimmer 6.2 Hz). Tune by ear.

### 8. LCD (`lcd.cpp`)

Identical renderer to the analog synth: framebuffer `FB[240·135]` + 4 KB staging buffer, widgets with change keys (`_knobKey[]`, `_topKey`, `_statKey`), `scan()` every 20 ms redraws changed widgets into `FB` and marks them pending, `ui_flush()` sends **one** pending rectangle per call as a bulk SPI transfer (`SPI1.transfer(buf, nullptr, n)`; the per-byte `transfer(uint8_t)` is ~65 000 calls per frame and stalls USB). RAM offset x+40, y+53. Geometry: `cellX(col) = col·29 + (col ≥ 4 ? 8 : 0)`, `cellY(row) = 19 + row·38`, cell 29×37, knob R = 12; the status block is the column-5 cell of row 2 to the right edge.

String-machine specifics: `valueText()` formats by `PARAM_INFO[].fmt` (ms/s, Hz, cents); the status block has two rows of 12 voice pips (hence the `MAX_ACTIVE_VOICES ≤ 24` static assert), the pitch-bend bar and the SUSTAIN lamp; button B toggles the latching pedal. **The font draws a lowercase `g` like a `9`** — keep `g` out of patch names.

### 9. Audio loop and boot order

`setup()`: `midi_handler_init` → `Serial` → `Wavetable::init` → `machine.init` (designs the filters, loads patch 0) → `audio_driver_init` (claims PIO2 **first**) → `usb_host_init` (PIO0/PIO1) → `lcd_begin`. `setup1()` is `delay(500)`.

`loop1()`: 256 samples per call, `tick(l, r)` then `audio_driver_put(l, r)` per sample (the put blocks and paces the loop). **Never compute a buffer and push it afterwards** — the FIFO holds a few samples; if it runs dry the clocks stop and the DAC mutes. CPU load: DWT cycle counter via raw registers, budget `F_CPU·256/44100`, shown as the top-bar square and in `stat`.

Two audio shields: see the analog synth's `context.md` and this README ([Audio shield versions](README.md#audio-shield-versions)) — the wrong `AUDIO_BOARD_REV2_1` is silent, and **a PIO audio program is not portable between DACs**.

### 10. Build and toolchain gotchas

Same as the analog synth: `config.h` pure defines; `static` data in headers is one copy per translation unit (hence `wavetable.cpp`); `ENABLE_USB_HOST` is `#ifndef`-guarded; `lib/pio_usb/` must be complete; `usb_host.h` needs the full `StringsManager` type; delete stale files from `src/` when files are renamed; `pico_tone` (RP2040) cannot build audio (PIO2); `F_CPU` is a build macro. Additional here: the `Ensemble` object is 8 KB inside `StringsManager` (11.6 KB total) — it is a global, never put one on a stack.

**`softSat` clamp (fixed 2026-11-12).** `x(27+x²)/(27+9x²)` reaches exactly 1.0 at |x| = 3 and keeps rising (1.0058 at 4). An earlier version clamped at |x| = 4, so a hard-driven input could overshoot full scale and the `int16_t` conversion would wrap into a loud click. It is clamped at 3 now, and `test_softsat_bound` in `test_engine.cpp` fails on the old version. (It could never trigger at the default `OUT_GAIN`; it would only have appeared under extreme overdrive.) Any new output stage must either use `softSat` this way or clamp explicitly before converting.

### 11. Test harness (`host_test/`)

The real sources compile and run on a PC against mocked Arduino / pico-sdk headers (`mock/`, `plat/`); `stage.sh` copies `src/` into `stage/` swapping in `plat/` for the three hardware-bound headers; `run_tests.sh [quick]` builds with ASan + UBSan + float-cast-overflow and runs everything.

- **`probe.cpp`** is the key tool: a scriptable wrapper around the real `StringsManager` (commands on stdin: `set`, `patch`, `on`, `off`, `pedal`, `bend`, `run N`, `skip N`, `write file`, `resp` (filter response), `ensprobe` (the Ensemble alone on an impulse train), `rawsaw`). Python (`dsp_common.py`) drives it, so every sound property can be measured from the real code.
- `test_dsp.py` (alias rejection, filter shapes, loudness balance, ensemble invariants, envelope timing, headroom), `test_engine.cpp` (`#define private public` after the std headers to inspect bookkeeping; `delayHook` runs audio while core 0 "waits"; two-thread tests), `test_lcd.cpp` (includes `lcd.cpp` and **emulates the ST7789**), `test_audio.py` (real `audio_driver.h` + mocked pico-sdk + PIO emulator + I²S receiver model, both boards).

Results at hand-off (cap 16, pool 24): all pass; torture 250 000 events / 29 410 steals / max 21 live; two-thread 772 000 control events against 3.5 million samples; no sanitizer findings. **Not covered:** ARM code generation and timing, real USB, the analogue chain, how it sounds. ThreadSanitizer was run on the analog synth (same protocol), **not** on this tree.

### 12. Key numbers

| Item | Value |
|---|---|
| Memory | `StringVoice` 96 B × 24 = 2.3 KB; `StringsManager` 11.6 KB (ensemble buffer 8.2 KB); sine table 8 KB; LCD framebuffer 64.8 KB + 4 KB staging |
| Control rate | 16 samples = 2.756 kHz; steal fade 6 ms; `_waitFree` bound 10 ms |
| PC benchmark (x86, relative only) | shared chain 51 ns/sample; 6 notes 98 ns; 16 notes (default cap) 175 ns; 16 notes, all registrations + unison 347 ns. Analog synth, 12 voices: 122 ns on the same machine |
| Levels | `OUT_GAIN` 0.15; 6-note chord peaks ≈ 0.25–0.4 FS at the patch volumes |
| Audio clocks (240 MHz) | Rev 2.1: BCK 2822.4 kHz, clkdiv ≈ 42.5. Initial Version: BCK 1411.2 kHz, clkdiv ≈ 85.0 |

### 13. Decisions that look odd but are deliberate

- **Default cap 16 (pool 24)**, not 24/32: the CPU is unmeasured on the Pico and an overrun gives the pitch-fall / DAC-mute failure. Raise it after looking at the CPU square.
- **Filters and ensemble are shared, not per voice** — as in the original machines; it makes CPU nearly independent of polyphony and keeps the sound coherent.
- **Phase-locked octaves from one accumulator** (divider-chain behaviour; one add per sample per voice).
- **Envelope squared**, no decay/sustain stage: closer to an exponential release; strings hold at full level while the key is down.
- **Release time read at key-up**; attack captured at note-on.
- **Velocity ignored**, like the originals.
- **Unison detune only on the 8′ footage** and only up to 18 cents: movement comes mostly from the ensemble.
- **`ENS` dry/wet law** (`dry = 1 − 0.45·mix`, `wet = 0.62·mix`) keeps the level within 0.4 dB across the mix range.
- **Patch 0 is called "Ensemble"**, not "Strings": the display font cannot draw a `g`.

### 14. Open items

Ordered by value:

1. **First flash.** Check the banner, `stat` and the CPU square with 6, 12 and 16 notes (with the default patch and with every registration + DET up). Record the numbers here. Then decide the voice cap.
2. **Tune the voicing by ear** (`FILT_*`, `ENS_*`, patch tables), then re-run `calibrate_trims.py` and `calibrate_patches.py`.
3. **Separate envelopes per registration** (a real machine has attack/release per section) — needs a parameter set per section and one envelope per voice per section, or per-section gain envelopes on the buses.
4. **Aftertouch swell**, velocity-to-brightness.
5. **CPU governor** like the organ's (drop the oldest notes gracefully when the audio core runs out of budget) instead of a fixed cap.
6. **More registrations** (solo / brass / hautbois) and a keyboard split.
7. ThreadSanitizer run on this tree; hardware-confirm the sustain-pedal stealing behaviour.
8. USB hub with two devices (vendored `pio_usb` v0.5.3 limit).

### 15. Working conventions

- Hardware-first, incremental: change one thing, flash, report; verify new code on the host harness before flashing.
- Deliver changed files individually (not a re-zipped project) so each change can be diffed; update README / context **after** a change is verified on the hardware.
- Copy platform files verbatim from the sibling projects; keep `audio_driver.h` and the two PIO programs identical across them.
- Measure before claiming: loudness trims, patch volumes and every performance statement here came from the harness, and PC timings are labelled as such.


<!-- ===== bass ===== -->

## [bass]

## picobass: hand-off context

For the next engineer (or Claude). Read this before changing anything. Companion to `README.md`.

### 1. What it is
A Moog-style mono bass synth + 303-style step sequencer on the same platform as picosynth / the tonewheel organ / picostrings / picodrums: Pico 2 (RP2350 240 MHz), Waveshare Pico-LCD-1.14, Waveshare Pico-Audio (two board versions), AKAI MPK mini on USB host. **Status at hand-off: verified on a PC only; never flashed; never listened to.**

### 2. Files
| File | Role |
|---|---|
| `config.h` | pure #defines (sizes, MIDI, audio board, filter range, voicing) |
| `bass_tables.h/.cpp` | boot-time tables: exact ladder `g` and `k_crit` per 1/16 octave; `2^(i/256)`. `exp2fast`, `ladderLookup` |
| `bass_dsp.h` | PolyBLEP saw / square, triangle, noise, `softSat`, `Ladder`, `Env` |
| `bass_params.h` | 33 parameters, value mappings, patches, demo patterns, `paramForCC`, note names, `parseStep` |
| `bass_voice.h` | `BassVoice`: `derive()` (core 0), `noteOn/gateOff/control/render` (core 1) |
| `bass_manager.h` | `BassManager`: key stack, patches, patterns, **sequencer**, transport, save image, `tick()` |
| `bass_storage.h` | LittleFS wrapper (core 0, stopped only; temp file + rename) |
| `lcd.h/.cpp` | 4-page UI renderer + buttons |
| `main.cpp` | core split, MIDI callbacks, serial console |
| `midi_handler.h`, `usb_host.h` | MIDI in (real-time messages handled; host path calls the shared parser) |
| `audio_driver.h`, `audio_pio*.pio(.h)` | shared with the other projects, unchanged |

No `wavetable.*`: the triangle is computed from the phase.

### 3. Threading contract
Core 0 = `loop()`, core 1 = `loop1()` calling `tick()`. No locks, no `spin_lock_blocking`.
* **core 0 -> core 1:** the voice event is ONE aligned 32-bit store: `evtWord = seq<<24 | vel<<16 | note<<8 | flags` (flags: 1 gate, 2 legato, 4 accent). It is a **state**, not an edge: if two events land in one control tick (16 samples) only the newest survives and nothing is inconsistent (a lost gate-off before a gate-on is harmless because the on carries its own legato flag). Transport `ctlCmd`+`__dmb()`+`++ctlSeq`; pattern `reqPat`+`++reqPatSeq`; clock `++clkSeq`; `++panicSeq`.
* **core 1 -> core 0:** single-byte flags `stepNow patNow playing gateNow noteNow`. There is no record ring in this project: step-write runs on core 0.
* Pattern memory (`stepNote`, `stepFlg`, `patLen`) is written by core 0 only, read by core 1: independent bytes.
* `BParams` (derived coefficients) is written by core 0 one aligned word at a time; a mix of old and new fields for one control tick is harmless. `derive()` recomputes everything on any knob change (cheap; uses powf/expf: core 0 only).
* The core-1 hot path has no libm, Serial, malloc. `bass_tables_init()` must run **before** the audio core starts (main.cpp does it before `audio_driver_init`).

### 4. The ladder (the part to understand)
Stage: `s += g*(sat(u) - sat(s))` x4; `u = in - k*s4`; every stage reads the PREVIOUS sample's state of the stage before it. So each stage is `H = g/(z-1+g)` and the loop is `k*H^4` with **no extra delay**. Self-oscillation where `arg(z-1+g) = pi/4`: `g = 1 - cos w + sin w`, `k_crit = 4 sin^4 w / g^4`. (My first derivation added a `z^-1` for the feedback and was wrong above ~800 Hz; the measurement against the real code caught it. Re-derive before changing the topology.)
* `g` reaches 1.0 at ~5.5 kHz -> `FILTER_OCTAVES 8.1`. Above, the Euler-style stage is unstable.
* RES: `r = 1.08*(v/127)^1.6`, feedback `k = r * k_crit(fc)`. Above critical the saturation pulls the self-oscillation flat: `g *= 1 + 0.2022*(r-1)*(1 + 0.10*max(0, oct-4)^2)` (measured; error <= 8 cents to 2.6 kHz).
* picosynth's `g = tan(pi f/fs)` puts the pole at ~0.5x nominal (see README section 1 and the figure). Porting `bass_tables` to picosynth would close its "true-Hz filter" open item.
* The saturation (`softSat`) is part of the sound: a full-level triangle through the ladder loses ~3.4 dB of 3rd harmonic. Tests measure waveforms at LOW input level (ladder linear) and assert the saturation separately.
* Voice-level protection: `Ladder::sane()` resets the filter on a blow-up; `incFor()` caps the oscillator at ~17 kHz (an overflow of `uint32` was found by UBSan).

### 5. Voice and keyboard
* One control tick (16 samples): envelopes, glide (`pitch += (tgt-pitch)*glideK`), oscillator increments (`exp2fast`), cutoff in octaves -> `(g, k)`, amplitude step. Per sample: 5 oscillator terms, 4 ladder stages.
* **Glide and no-retrigger only when `legato && gate`**: overlapping notes (keys) or a slide (sequencer). A new note after a release jumps and retriggers.
* Key stack (core 0, `STACK_N` 8): last-note priority; releasing the top key posts a LEGATO note for the previous key; pedal marks keys released-but-held. **A reference model in `test_engine.cpp` checks 40,000 random operations.**
* Transpose mode: while the sequencer plays, `noteOn` only sets `transpose = note - KEY_ROOT` and returns; stopped, keys play.
* Step-write on core 0: `noteOn` also writes the step at `cursor` and advances; the pedal writes a rest.

### 6. Sequencer
Per sample: `_cnt -= 1; if (_cnt <= 0) _fireAndAdvance()`; `_cnt += dur` keeps fractions. Even steps `2*L*s`, odd `2*L*(1-s)`, `L = 44100*15/bpm`. `_fireStep`: gate step -> `_voiceOn(note + transpose, vel, legato = _prevSlide, accent)`, then `_gateCnt` = `dur*gateFrac` samples (slide step: 0 = hold); rest -> `_gateOff()`. `_prevSlide` makes the next note legato. The pattern queue is applied in `_advance()` at the wrap. External clock: every 6 ticks; gate from the EMA of the tick interval (`_haveTick` stops the first interval being measured from time zero). STOP keeps the position and calls `_gateOff()`.

### 7. Parameters, patches, calibration
`PARAM_INFO[]` is the single table (label, name, serial cmd, CC, group, page, col, row, format). A patch sets everything except TEMPO and SWING. **`loadPatch` must not touch the tempo** (it once rounded a fractional BPM; `_applyAll` no longer sets it). `calibrate_patches.py [--apply]` equalises the VOL of each patch on a 4-bar groove to `TARGET_DB` (-19.5): re-run it after changing any voicing.

### 8. Display
Same renderer: a POD key per widget, redraw only on change, one rectangle per `ui_flush`. Rules: no rectangle > 8700 bytes; **a widget must draw only inside its own rectangle** (the SEQ cursor frame once stuck out one pixel and was never sent: the fuzz caught it); page change repaints the whole screen once (64,811 B); at most 32 widgets (`_pending` is a `uint32_t`; knob widget indices are per page).

### 9. Storage
`serialize()` = 'BAS',1 + dims + `pv[33]` + patch + bpm + flags + selPat + `patLen[16]` + notes + flags + CRC32 (580 bytes). `deserialize()` validates length, magic, dims and CRC before touching anything, then sanitises. File written to a temp name and renamed over the old one (never delete first). Save only while stopped (a flash write stalls both cores).

### 10. Test harness
`host_test/run_tests.sh`. `probe.cpp` wraps the real `BassManager` with `BASS_TRACE` (every voice event: sample, kind, note, velocity, accent, pattern, step). `rawosc` renders the raw oscillators (PolyBLEP vs naive); `filtresp` / `filtsweep` drive the ladder alone. Do not benchmark in the sanitizer build; `bench.cpp` at -O2 must consume the audio.
Measure harmonics with a windowed DTFT **at the exact frequency**, not the nearest FFT bin (scalloping reads up to 1.4 dB wrong).

### 11. Key numbers
Control tick 16 samples (2756 Hz); sequencer error <= 1 sample; self-oscillation <= 8 cents to 2.6 kHz; PolyBLEP saw alias -41 dB at C2 (-30 dB at C6); save image 580 B; first LCD paint 64,811 B / 22 calls; largest LCD transfer 8651 B (1.15 ms); PC bench (x86 -O2): idle 1.9, one note 17.6, worst case 21.4, demo pattern 11 ns per sample.

### 12. Open items
1. **First flash**: sound on both shields, CPU square with the worst-case patch (Acid, RES 127, drive up), record the numbers.
2. **Listen** and voice by ear; re-run `calibrate_patches.py` afterwards.
3. Flash save on real hardware (audio hiccup length); MIDI clock with a real DAW (jitter).
4. Aliasing with high resonance on high notes (2x oversampling is the fix if audible).
5. Ideas: LFO, a second filter env for accent, per-step velocity / ties, song chain, port `bass_tables` to picosynth.


<!-- ===== drums ===== -->

## [drums]

## picodrums: hand-off context

For the next engineer (or Claude). Read this before changing anything. Companion to `README.md` (user-facing).

### 1. What it is
An 808-style drum machine on the same platform as picosynth / the tonewheel organ / picostrings: Pico 2 (RP2350, 240 MHz), Waveshare Pico-LCD-1.14, Waveshare Pico-Audio (two board versions), AKAI MPK mini on USB host. 16 synthesised voices (= 16 pads = 16 tracks), sample-exact sequencer on core 1, 4-page LCD, MIDI clock, flash save. **Status at hand-off: verified on a PC only; never flashed; never listened to.**

### 2. Files
| File | Role |
|---|---|
| `config.h` | pure #defines (sizes, MIDI, audio board, voicing constants, CC map) |
| `drum_params.h` | instruments, knob table (`PARAM_INFO`), default kit `KIT[16][5]`, GM pad map, demo patterns, `paramForCC` |
| `drum_dsp.h` | `sineF` (interpolated table), `noiseF`, one-pole `lp1/hp1`, `softSat`, coefficient helpers |
| `drum_voices.h` | `DrumVoice`: `derive()` (core 0: knobs -> coefficients), `trigger/control/render` (core 1) |
| `drum_manager.h` | `DrumManager`: params, pads, transport, patterns, song, **sequencer**, save image, `tick()` |
| `drum_storage.h` | LittleFS wrapper (core 0, stopped only) |
| `lcd.h/.cpp` | 4-page UI renderer + buttons (dirty rectangles, one transfer per `ui_flush`) |
| `main.cpp` | core split, MIDI callbacks, serial console |
| `midi_handler.h`, `usb_host.h` | MIDI in; changed for real-time messages (section 9) |
| `audio_driver.h`, `audio_pio*.pio(.h)`, `wavetable.*` | shared with the other projects, unchanged |

### 3. Threading contract (the rules that make it lock-free)
Core 0 = `loop()`; core 1 = `loop1()` calling `tick()`. **No locks, no `spin_lock_blocking`** (it disables interrupts and loses USB-MIDI).
* **core 0 -> core 1, single writer each:** pad hit `voice.trigVel` then `__dmb()` then `++trigSeq`; transport `ctlCmd` + `__dmb()` + `++ctlSeq` (START / STOP / CONT); pattern `reqPat` + `++reqPatSeq`; MIDI clock `++clkSeq`; `++panicSeq`. Core 1 keeps private `*Seen` copies and compares. **Two hits on the same instrument inside one control tick (16 samples) collapse to one** (latest velocity wins); documented, tested as exactly-once for hits >= 100 us apart.
* **core 1 -> core 0:** `RecEvt` ring (`REC_RING` 32, producer core 1, consumer core 0 in `service()`); single-byte position flags `stepNow patNow songPosNow playing`.
* **Pattern memory** (`pat`, `patLen`, `song`) is written by core 0 only and read by core 1: independent bytes, a read can at worst see an edit one step early or late.
* **DParams** (derived sound coefficients) are written by core 0 one aligned word at a time while core 1 reads them. **All filters are one-pole with a single coefficient** so a knob turn can never tear a filter. Do not introduce a multi-coefficient filter (biquad / SVF) without double-buffering.
* Hot path: `tick()` and everything it calls are `HOT` (force-inline) so they live in SRAM inside `loop1()`; no libm, no Serial, no malloc. (`expf/powf/cosf` appear only in `derive()`, core 0.)
* Flash writes (`drum_storage.h`) stall both cores: **never while playing** (UI and serial enforce it).

### 4. Sequencer
* Per sample on core 1: `_cnt -= 1; if (_cnt <= 0) _fireAndAdvance()`. `_cnt += dur` (not `=`) so fractional samples carry over: no drift (max error 1 sample over 33 steps).
* Step durations: even `2*L*s`, odd `2*L*(1-s)`, `L = 44100*15/bpm`, `s` = swing 0.5..0.75.
* `_fireStep` triggers every track whose step level != 0 (level -> velocity 55/100/127), then `_advance()` moves `_step`, wraps at `patLen[_patCur]`, and **at the wrap** applies the queued pattern (or the next song slot). That is why a pattern change lands exactly on the bar line.
* STOP keeps the position (CONTINUE resumes); START resets to step 0 and fires immediately; a START in the same control tick as a pattern request plays the new pattern (pattern requests are processed first).
* External clock: `_clockTick()` fires a step every 6 ticks; `_firstTick` makes the first tick after START/CONT fire immediately. Internal timing is skipped (`extSync`).
* Record: `_recordHit` runs on core 1 at the moment the trigger is consumed; fraction through the current step = `(curDur - cnt)/curDur` (internal) or `tickPhase/6` (external); `> REC_QUANT_NEXT` goes to the next step (wraps modulo the pattern length).

### 5. Sound
Seven types (`DrumType`), see `drum_voices.h` header comment. All envelopes are exponential, advanced once per control tick (16 samples) and linearly interpolated. A hit sets amplitude immediately (no ramp-in), the attack is instant by design. A voice is released (`live = false`) when its main envelope falls below 1e-3 (metal 2e-3). `CHOKE_MS` 4: the closed hat sets `choked` on the open hat, which swaps its decay coefficients for the fast one.
Gain staging: voice peak = `level^2 * VOICE_GAIN(2.0) * velocity * pan`; mix; `softSat(x * drive * OUT_GAIN)`; `* volGain`; `int16`.

### 6. Calibration
`host_test/calibrate_kit.py [--apply]` measures each instrument's peak power (`sqrt(L^2+R^2)`, independent of pan) at LEVEL 127 / velocity 127 and rewrites the LEVEL column of `KIT[]` so it reaches the target in `TARGET[]`. Re-run it after changing any voicing constant. Everything else in `KIT` (tune, decay, tone, pan) is hand-set.

### 7. Display
Same renderer as the other projects: per widget a POD "key" of what it shows; `scan()` redraws a widget only if its key changed and sets a bit in `_pending`; `ui_flush()` sends ONE rectangle per call. Rules: **no rectangle larger than 8700 bytes** (`test_lcd` enforces; the INST status block is three 25-row widgets for this reason, the song rows are 212x19); a widget's rectangle must cover everything it draws (the cursor frame once stuck out 1 px: the fuzz caught it); a page change repaints the whole screen once (64,811 B). Widget ids are per page (max 14 < 32 bits of `_pending`).
Buttons: A tap = play/stop, A held 0.6 s = record; B = page; joystick per page.

### 8. Storage
`serialize()` -> 4247 bytes (`'DRM',1`, dims, kit, bpm float, swing, vol, drive, flags, padBase, selPat, patLen[16], pat[4096], song[32], CRC32). `deserialize()` validates length, magic/version, dims, CRC **before touching anything**, then sanitises (clamps lengths, levels, song slots) so a valid-CRC-but-nonsense file cannot crash the sequencer. Changing `N_INST/N_STEPS/N_PATTERNS/SONG_MAX` invalidates saves (dims are in the header): intended. The file is written as `/drums.bin.tmp` and **renamed over** the old one; never delete the old file first (a power cut would lose both).

### 9. MIDI changes in the shared files
`MIDI_REALTIME` (config.h; default 0 in `midi_handler.h` if undefined). With it on: real-time bytes are handled **before** the status-byte logic (they may sit between the data bytes of another message and must not reset the running status), and USB `cin 0xF` packets skip the channel filter (`0xF8 & 0x0F` = "channel 9"). Byte `>= 0xF8` is now always swallowed without disturbing the parser, flag or not. `usb_host.h` calls the shared `_dispatchUsbPacket()` instead of carrying a copy (the copy was untestable). The other projects still have the old parser: they would need a stub `midi_realtime()` to adopt this.

### 10. Test harness
`host_test/run_tests.sh` (see README section 9). `stage.sh` copies `src/` to `stage/` replacing the three hardware-bound headers with `plat/` mocks (the MIDI test stages the real `midi_handler.h` separately in `stage_midi/`). `probe.cpp` wraps the real `DrumManager` (with `DRUM_TRACE`, which logs every voice start as sample/inst/vel/pattern/step) for the Python suites. **Do not benchmark in the sanitizer build** (5-10x slower; that produced a bogus 657 ns figure once): use `bench.cpp` at -O2. A benchmark must consume the audio output or the optimiser deletes it.
Mock gaps found on the way: `Serial.println()`, `String::toFloat`: added.

### 11. Gotchas learned here
* `softSat` clamp must be at **|x| = 3**, not 4 (the rational curve reaches 1.0 at 3 and keeps rising). Fixed here and in picostrings; regression test in both. Any new output stage must clamp before `int16`.
* `stage.sh` overlays `plat/midi_handler.h`; copy the real one yourself for the MIDI test.
* Test the *test*: weak assertions (`or True`, stubs) existed in the first draft; a regression test must be shown to fail on the old code.
* `cycleStep` table is indexed by the CURRENT level: `{2,0,3,1}` (off->normal, ghost->off, normal->accent, accent->ghost).
* The audio-shield warning applies: wrong `AUDIO_BOARD_REV2_1` = silence.

### 12. Key numbers
sizeof(DrumManager) 7692 B (patterns 4 KB, 16 voices); save image 4247 B; control tick 16 samples (2756 Hz); sequencer error <= 1 sample; first LCD paint 64,811 B / 22 calls; largest LCD transfer 8651 B (1.15 ms); PC bench (x86 -O2): idle 14 ns, demo pattern 24 ns, all 16 voices 69 ns, stress 79 ns per sample (string machine worst case 349 ns).

### 13. Deliberate decisions
Fixed 16 voices (no pool: drums are one voice per instrument); one-pole filters only; synthesis not samples; sequencer on core 1; pad hits via counters, not queues; swing off under external clock; record quantises on core 1 and applies on core 0; save refused while playing; linear pad map by default (36-51) with GM as an option; per-pattern length instead of per-track polymeter.

### 14. Open items
1. **First flash**: confirm sound on both shields; read the CPU square with a busy pattern; record real numbers.
2. **Listen** and voice by ear (all in `derive()` and `config.h`); re-run `calibrate_kit.py` afterwards.
3. Confirm the MPK mini's pad notes (`log notes`); adjust `PAD_BASE_NOTE` if not 36.
4. Verify flash save on real hardware (the audio hiccup length, `board_build.filesystem_size`).
5. Verify MIDI clock in with a real DAW; consider a tick-interval PLL to smooth jitter and apply swing.
6. Ideas not built: per-track mute/solo, per-track length (polymeter), velocity-sensitive tone, band-limited hat oscillators, kit presets, pattern chain on a pad, aftertouch.


<!-- ===== piano ===== -->

## [piano]

## picopiano: hand-off context

For the next engineer (or Claude). Read this before changing anything. Companion to `README.md`.

### 1. What it is
A 24-note (32-slot) sampled keyboard engine on a 4 MB Pico 2. 4-bit ADPCM samples play in place from flash. It ships with the Salamander Grand V2 (CC BY 3.0, see `NOTICE.md`, 3.0 MB) and supports **several instruments, velocity layers and seamless loops**, a **four-effect pipeline** (wah, tremolo / auto-pan, chorus, reverb) and a laptop-side **bank configurator** (`tools/bankconf`: CLI + Flask GUI). Same platform as picosynth / organ / picostrings / picodrums / picobass. **Status at hand-off: verified on a PC only; never flashed; never listened to; the GUI never opened in a real browser.**

### 2. Files
| File | Role |
|---|---|
| `config.h` | pure #defines (`N_POLY` 24 audible notes, `N_VOICES` 32 slots, MIDI, audio board, `OUT_GAIN`) |
| `piano_adpcm.h` | **codec 2**: IMA quantiser + 4-tap sign-error LMS predictor (`AdpcmState`: `pred`, `idx`, `h[4]`, `w[4]` Q13; `init`, `restore`, `predict`, `decode`, `encode`). Shared by the firmware and `tools/adpcm_tool.cpp`; `LMS_SHIFT` 7 |
| `piano_bank.h` | **generated by `tools/bankconf`** (format v2, `PIANO_CODEC 2`): `PianoZone` (data offset, sample count, loop start/end, start predictor + `lh[4]`, `lw[4]`, `lidx` = the FULL decoder state at the loop start, start step index, rate, root, key range, velocity range, reference velocity, gain, cents) and `PianoInstrument` (name, credit, zone range, 16 knob defaults) |
| `piano_data.cpp` | **generated**: the data as a C array (10 MB of source, 3,000,084 bytes) |
| `piano_params.h` | **16 parameters** (page 1 the piano, page 2 the effects), mappings, keyboard geometry, `paramForCC`. The tool READS this file for the knob names and defaults |
| `piano_fx.h` | `PianoFx`: wah, tremolo / auto-pan, chorus, reverb on the stereo mix (60,248 bytes of state) |
| `piano_manager.h` | `PianoVoice` (decode + Hermite + low-pass) and `PianoManager` (events, allocation, `tick()`) |
| `lcd.h/.cpp` | 8 knobs (two pages) + 88-key keyboard (8 strips) + instrument name (top bar) + credit line (widget 17) |
| `main.cpp` | core split, MIDI callbacks (program change = instrument), serial console (`inst n`) |
| `banks/*.yaml`, `tools/bankconf/`, `tools/bank_gui.py` | the bank configurator (section 4) |
| `midi_handler.h`, `usb_host.h`, `audio_driver.h`, `audio_pio*.pio(.h)` | shared with the other projects |

### 3. Threading contract
Core 0 = `loop()`, core 1 = `loop1()` calling `tick()`. No locks.
* **Core 1 owns every voice.** Core 0 never touches voice state.
* **core 0 -> core 1:** an SPSC ring (`EVT_RING` 64, producer core 0, consumer core 1) for note-on / note-off / pedal. A note-on event CARRIES everything that needs libm or the zone table: the **zone index**, the playback **increment** (`rate/44100 x 2^((key-root)/12)`), `gL`, `gR` (velocity curve x zone gain x una corda x pan) and the filter coefficient `lpA`, so core 1 never calls `pow/exp/sin` and never searches a table. **`panic()` and `allNotesOff()` are counters (`panicSeq`, `offSeq`), not events**: a full ring must not be able to drop them (found by the display test).
* **Single-word globals** written by core 0, read by core 1: `volGain`, `decCoef`, `pitchMul`, and every field of `PianoFx` (`wahMix`, `revFb`, LFO increments, the `on` mask ...). `_relCoef[88]` is a float array written by core 0 on a knob change; a mix of old and new for one control tick is harmless. `PianoFx`'s delay lines and filter states belong to core 1 alone.
* **core 1 -> core 0:** `keyMask[3]` (a bit per audible key, for the display), `voicesNow`. Racy reads are fine for a display.

### 4. Sample bank and the bank configurator (`tools/bankconf`)
* **Config** (`banks/*.yaml`): total `budget`, `normalize_peak`, `instruments[]` each with `name` (12), `credit` (38), `gain` dB, `defaults` (knob names from `piano_params.h`), `rate_rules` (by register), and samples from `import` (folder + pattern `{note}`/`{midi}`), `sfz` or explicit `samples`. `expand_config()` resolves everything: key ranges split at midpoints between neighbouring roots inside each velocity layer (ties to the lower root); `vref` = 80 for a full-range sample, else the layer's middle velocity.
* **Per sample (`core.build`)**: ffmpeg decode to mono 44.1 kHz; onset = first sample above 2 % of the peak, minus 3 ms; one global **scale** on every `maxdur` found by bisection (starting interval 0.2 .. 1.5, as the original script did, so the shipped grand is reproduced exactly) to hit the budget; one normalisation gain per INSTRUMENT (its loudest sample = 0.92); non-looped: raised-cosine fade-out; looped: resample, `find_loop()` (the waveform before the loop end vs before the loop start, whole periods, 0.25 .. 1 s, step p/16 then refine by 1), 12 ms crossfade IN THE ZONE'S OWN RATE (never in 44.1 kHz: rounding the loop points on resampling would break the seam), data stored only up to the loop end; soxr to the zone's rate; `adpcm_tool enc`; `adpcm_tool state` gives the full decoder state at the loop start (`h0..h3 w0..w3 idx`); SNR by decoding again.
* **Tuning:** `fundamental_cents()` measures the true f0 (lowest strong partial among 1..4, +-150 cents window). It feeds docs and tests ONLY: playback keeps the recording's own tuning (stretch tuning).
* **Outputs:** `piano_bank.h`, `piano_data.cpp` (to `src/`), `bank.bin`, `bank.json` (to `tools/.gen/`). The grand rebuilt from `banks/grand.yaml` is **byte-identical** to the original bank (md5 8bda1733a09a3e63e6142540089f2f32, 3,000,084 bytes); `test_bank.py --full` checks it when `samples/salamander` exists (`tools/fetch_salamander.sh`).
* **Warnings** (`check_config`, `build`): unmapped keys, no velocity layer for a velocity, shifts over 4 semitones, increments >= 1, SNR < 20 dB, scale < 0.2 ("very tight"), credit > 38 characters, bank over budget.
* **GUI** (`app.py` + `static/index.html`, vanilla JS, no build step): `/api/state|estimate|import|browse|waveform|build|job/<id>|export|save|load|audition`. **Audition compiles `host_test/probe.cpp` against the BUILT bank (cached by hash) and runs the firmware's own engine**: there is no second implementation of the player. It needs g++ and the project tree (`host_test/mock`). `estimate` is instant (no encoding); `build` is a background thread.
* **Parameter table coupling:** `PIANO_N_DEFAULTS` in the generated header must equal `N_PARAMS` (`static_assert`). After changing `piano_params.h`, rebuild the bank (about 90 s for the grand); the sample data comes out identical.
* Start predictor = first sample, step index 0. Streams are byte-aligned to 4; low nibble first.

### 5. The voice
* **Zone per (key, velocity) on core 0** (`selectZone`): velocity layer first (nearest if none contains it), then key range, then nearest root, ties to the lower root. For the grand: every key within one semitone, `inc` in (0, 1) so at most one ADPCM sample is decoded per output sample (tested for the shipped bank only; a user bank may exceed 1, the engine's ceiling is 8 and the tool warns). `pitchMul` (tune x bend) multiplies `baseInc` per control tick.
* **Loops:** `if (loopEnd && pos >= loopEnd) { pos = loopStart; st.restore(zp->lh, zp->lw, zp->lidx); }` before every history step. A looped zone has `n == loopEnd`.
* `render()`: step the 4-sample history while `frac >= 1`, Catmull-Rom between `h1` and `h2`, optional one-pole low-pass, times `amp` (interpolated envelope).
* **Velocity:** `gain = (vel/Z.vref)^p x (1 - 0.45 x soft) x 10^(gain10/200)` with `p = 0.9 + HARD/127`; cutoff `fc = 20 kHz x 2^(-(vref-vel)x8/127) x tone x 2^(-3.5 soft)`, bypassed (coefficient 1) above 15 kHz. **Velocity >= vref with TONE 64 and no una corda is bit-for-bit the recording** (tested for the grand, vref 80). There is deliberately NO register term in the cutoff: it once darkened bass notes at velocity 80.
* **Envelope:** `e` multiplies by `decCoef` (string decay, 1.0 = natural) and, when released, by `_relCoef[key]` (damper) or `_fastCoef` (15 ms, for re-struck keys and panic). A voice ends when released and `e < 1e-4`, or when its data runs out (the fade-out is in the data) after 4 extra history steps.
* **Allocation (`_alloc`):** `N_POLY` (24) is a soft limit of voices that are not already fast-fading; the pool is `N_VOICES` (32). At the limit the victim (the quietest RELEASING voice, `level() = e x (gL+gR)`; else the oldest) is **fast-released** (15 ms T60, ended at -50 dB so it is gone in ~13 ms), NOT cut, and the new note takes a free slot from the spare 8. Only if no slot is free is the quietest voice cut (trace kind 6). **A hard cut is a click**: measured at 10.7x the local second difference at the steal (with a reference of 4.3x at random points); `host_test/click_probe.py` and a DSP test keep it at <= 5x. At 200 notes/s the 8 spares suffice (tested); beyond ~400/s a cut can happen.
* **Effects** (`piano_fx.h`): after the voices are summed and multiplied by `volGain`, `fx.process(L, R)` runs wah -> tremolo / auto-pan -> chorus -> reverb, then `softLimit`. With all knobs off `process()` returns at once (bit-exact bypass, tested against `host_test/golden_piano.json`). Wah: Chamberlin SVF, coefficient every 16 samples, `exp2x` polynomial; the LFO is a parabolic sine from a 32-bit phase. Reverb wet gain 7.5 (calibrated by measurement: the first version, at 0.9, was 20 dB too quiet to hear).
* **Output limiter `softLimit`** (`LIMIT_KNEE` 0.72): exactly linear below the knee, `K + (1-K) t/(1+t)` above, bounded below 0.995, NaN/inf-safe (`ax < 16` test is false for NaN). It replaced `softSat` = `x(27+x^2)/(27+9x^2)`, which has unit slope only at 0 and gave -33 dB distortion on a velocity-127 note at the default volume. Output conversion TRUNCATES (`(int16_t)(y*32767)`): tests that compare against a scaled reference need a ~4 LSB tolerance.
* Gain staging: voice `y` x `gL/gR` (centre = 1.0 per channel) summed, x `volGain = (vol/127)^2 x OUT_GAIN (0.5)`, effects, `softLimit`, int16. A mezzo-forte triad at the default volume peaks near 0.45; four velocity-127 notes reach a linear 1.0, ten under the pedal 1.4.

### 6. Display
Same renderer rules: a POD key per widget, redraw on change, one rectangle per `ui_flush`, nothing over 8700 bytes (largest 8651 B). Widgets: 0 top bar, 1-8 the eight knobs of the CURRENT page (`_page = _sel / 8`; each slot's change key includes the knob id, so a page turn redraws), 9-16 keyboard strips, **17 the credit line** (the selected instrument's). The top bar is `instrument name (8 chars, gold) | knob name + value (16 chars max) | voices PED UNA`. `keyRect()` is the single source of key geometry: **`whiteIndexOf(k)` is the number of white keys BELOW k, so a black key's boundary is `whiteX(wi)`** (it was `wi+1` once). **PRESS**: a tap (armed on press, fired on release) auditions; held 600 ms it selects the next instrument and then does nothing on release. `test_lcd.cpp` has a 1500-step fuzz over all 16 knobs checking that the panel equals the framebuffer.

### 7. Test harness
`host_test/run_tests.sh` (about 12 minutes: the 88-key pitch sweep, the bank rebuild, audition compiles): `test_dsp.py` 54, `test_engine.cpp` 118 (also run against a synthetic multi-instrument bank by `test_bank.py`), `test_lcd.cpp` 49 check lines, MIDI 16 + 19, audio driver, `test_fx.py` 33, `test_bank.py` 76, `test_gui.py` + `gui_dom_test.js` 69. `probe.cpp` wraps the real `PianoManager` with `PIANO_TRACE` (kinds: 1 start, 2 victim chosen, 3 end, 4 release, 5 fast release, 6 HARD cut) and has commands `inst`, `zone? key vel`, `fxsine hz amp n`, `fximp n`. **`golden_piano.json`**: md5 of five renders; any change to the engine's output (even a rounding) fails `test_fx.py`. Re-recorded on 14 Nov for the limiter and the beam encoder; `golden_piano_v1.json` is the pre-change original, and the old `softSat` + a classic-encoder bank (`adpcm_beam: 0`) reproduce it exactly on the current engine (that is how the 14 Nov re-record was validated). Update it only on purpose, and say why. Test tips: the reference for "player vs recording" is the decoded ADPCM resampled ideally, **aligned with a fractional delay** (the engine's latency is ~1.5 zone samples). Background runs must be started with `setsid nohup ... < /dev/null &` and a NEW log file name each time (a stale log once showed an old traceback as if it were the current run). A test that cannot fail (`True`, `or True`) is worse than none: grep for them.

### 8. Key numbers
Grand bank 3,000,084 bytes, 30 zones; zone SNR 24-46 dB; keys' pitch within 6 cents of the recording; stretch tuning A0 -24, C4 0, C8 +100 cents; control tick 16 samples; PC bench (x86 -O2, relative, ~10 % run to run): one note ~18-28 ns, 24 voices 95-106 ns per sample; **effects**: wah 8.2, tremolo 2.3, auto-pan 2.6, chorus 5.7, reverb 13.7, all four 21.7 ns/sample, `sizeof(PianoFx)` 60,248 bytes; wah resonance 334 / 1006 / 2741 Hz at WPOS 0 / 64 / 127; reverb RT60 0.44 / 0.89 / 5.56 s; first LCD paint 64,811 B; largest transfer 8651 B. Grand note length vs its share of the flash: 3.0 MB 15 / 9.7 / 4.8 s, 1.5 MB 7.4 / 4.8 / 2.4 s (bass / mid / treble).

### 8b. Lesson
The steal click was missed by every automatic test and found by playing more than 20 notes with the pedal. When adding a limit (voices, ring slots, buffers), test what happens AT the limit with a measurement of the signal, not just that nothing crashes.

### 4b. The codec (why it is what it is)
* **Measured cause of the hiss** (do not re-derive): per-band ADPCM error vs signal over time on the C4 and C6 zones: above 5 kHz the source has no signal (-106 dB after the attack) but plain IMA's error sits at -55 (attack) .. -84 dB there, a fixed distance below the note's overall level. The tool `encw` (perceptually weighted / noise-shaped beam search, LPC-weighted error) was built and **rejected**: HF error -5..-12 dB but LF/mid +15..+30 dB, worst noise-to-signal ratio where signal exists got worse. The predictor is the lever.
* **Predictor choice** (offline simulation on the real zones, same window, SNR dB; IMA -> NLMS float / sign-error LMS int, order 4): C2 37.6 -> 42.6, C4 38.3 -> 45.5, C6 34.3 -> 41.6, C7 28.5 -> 39.7. Sign-sign LMS and order 2/8 variants were worse or dearer. `LMS_SHIFT` scan (6/7/8/9, greedy, full zones): 7 is the best compromise (C2 42.9, C4 44.0, C6 42.7, C7 41.3); 8 wins at C4 but loses 2.5 dB at C7. Float NLMS was rejected: it needs a division per sample and PC/Pico float results can differ in the last bit, which breaks an encoder-in-the-loop.
* **Rules the format relies on:** integer only; `>>` of negative values is arithmetic (floor); weights clamped to +-32000 so a loop state fits int16; the prediction is clamped to int16; the update uses the history BEFORE it is shifted; zone start = history `pred0` x4, weights (8192,0,0,0), index `idx0`. The Python reference decoder in `test_bank.py` is a second implementation of exactly this.
* **Beam encoder** (`encb`): every path carries a full `AdpcmState`, cost = squared error, no state merging (states rarely coincide), keep the best B per sample. Beam 16 gains about +2.5 dB over greedy at ~1 s per zone. Results (whole zones, greedy -> beam 16): C2 42.9 -> 45.5, C4 44.0 -> 46.7, C6 42.7 -> 44.6, C7 41.3 -> 43.2.
* **Build gotchas:** `adpcm_tool.cpp` must include everything it uses (`<string>` was missing and only compiled with a `-include` flag); the binary's file name carries a hash of `adpcm_tool.cpp` + `piano_adpcm.h`; `test_dsp.py` rebuilds its own decoder every run (it used to reuse an old `/tmp/adpcm_tool`, which made a good codec-2 bank look like garbage).

### 4c. The strike, the source and the levels (measured; do not re-derive)
* **The MP3 source was not the cause of 'noise at the hit'.** `ffprobe` of the Tone.js Salamander MP3s: 38.8 kbit/s, 44.1 kHz stereo. Around the hit the pre-onset level is the same as in the lossless file (-50/-47 dB MP3 vs -41/-43 FLAC re peak): no pre-echo. What the MP3 lacks is everything above ~5 kHz (C4, first 100 ms: -99 dB above 6 kHz vs -62 dB lossless). That is why the old analysis saw 'no signal above 5 kHz'.
* **The cause is the codec at the strike.** 1 ms-window SNR from 3 ms before the hit, lossless C4, codec 2 + beam 16: 34, 39, 45, 36, **27**, 35, 42, 41 dB, vs 46 dB for 0.2-1 s. Plain IMA: the same dip (24-29). Tried without effect: lead-in 3/1/0 ms, fade-in 0.3 ms, start step index 0..70 (worst window 22-29 dB in every case). Conclusion: a 4-bit quantiser cannot follow the hammer transient.
* **PCM head** (`pcm_head_ms`, default 9, measured from the zone start, so ~3 ms of lead-in + ~6 ms after the hit): the zone's data = `head` int16 LE samples + the ADPCM stream. `AdpcmState::push()` feeds head samples into the history (weights and index untouched); the stream starts from `init(pred = last head sample, idx0)` with history = the last 4 head samples (`adpcm_tool` accepts them as optional trailing arguments `h1 h2 h3`). `idx0` is picked per zone from {0, 20, 35, 50, 65} by a greedy pass over the first 150 ms after the head (the signal is loud at the handover, index 0 starts too fine). Simulated on lossless C4: head 6 ms -> worst window after the head 37.8 dB (29.4 without), 12 ms -> 35.0, total SNR unchanged (45.0-45.3). A loop jump must land after the head (`loopStart > head`, enforced by the tool).
* **Per-zone normalisation** (`normalize: zone`, default): stored = x * (0.92 / zone peak); run-time zone gain = `20 log10(G_instrument / g_zone)` = `pk_zone / pk_instrument_max` in dB (always <= 0), so playback level equals the old per-instrument scaling. The grand's stored peaks used to span -13..-0.7 dBFS (median -4.6). The DSP test builds the reference from the decoded zone **times the zone gain**; without it the player-vs-recording SNR collapses to exactly `-20 log10|1 - gain|` (9.2 dB for -3.7 dB), which is how the missing gain in the test was recognised.
* **Level matching** (`level_match`): per instrument, the median over the zones of the layer containing velocity 80 of `RMS_dB(first second x G) + 1.404 x 20 log10(80 / vref)`; offsets are `median - level`, clamped to +-12 dB. It is an approximation (pan, transposition, layer differences): `python3 -m bankconf levels` measures through the engine. Survey on the real all.yaml (RMS of the first second, default knobs): vel 80 medians Grand -21.6, Rhodes -24.3, Clavinet -26.3 dBFS; the grand falls 15 dB from key 30 to 96, the clavinet is flat to 0.5 dB, the Rhodes drops 6-12 dB above key 84. Rhodes at vel 110 median -28.1 (quieter than at 80: unexplained; check how the source layers are normalised and `vref`).
* **Lossless Salamander V3** (`sfzinstruments/SalamanderGrandPiano`): 641 FLAC, 748 MB, `Samples/<Note>v<1..16>.flac`, sharps written `D#1`; layer ranges from `Data/notes.txt`: v10 = 73..80. Partial clone (`--filter=blob:none --no-checkout`) + `git checkout HEAD -- <files>` fetches only the wanted layer (45 MB). Run git with `-c gc.auto=0` (its background 'Auto packing' messages flood the output).
* **Test-estimator rule**: a pitch comparison between two different estimators on a recording with beating unison strings can differ by 4 cents (A5: -3.99); compare the engine with the *decoded zone* using the same estimator (0.05 cents measured) and keep the tool's own measurement as a looser (6 cents) check.

### 4d. Reverb level and the noise floor (measured)
* **The reverb was calibrated on one 500 Hz sine and was wrong for music.** Played notes: wet energy over 6 s vs dry at REV 127 was +6.8 (C4), +0.7 .. +16.4 (C2, by reverb time), +5.5 .. +11.2 (C6), +4.3 .. +10.8 (chord) dB: up to x3 in amplitude. Comb filters are not level-flat (peak gain 1/(1-fb) = 30 dB at fb 0.97) and bass notes hit their resonances. Fixes in `piano_fx.h`: squared taper `revWet = t^2 * 10^(comp/20)`, an input one-pole high-pass (`REV_HP_K` 0.021 = 150 Hz), and a 5-point interpolated table `REV_COMP_DB` = {11.8, 10.6, 9.1, 7.0, 3.1} dB at RSIZE 0/32/64/96/127, fitted so the AVERAGE over five signals is -3 dB (result -7.3 .. +0.6, mean -3.1). Calibrate effect levels on played notes and chords, never on a steady sine; `test_fx.py` does.
* **The firmware's idle output is exactly zero** (tested at 20-30 s, 35-45 s with the longest reverb, and VOL 0): a noise heard between notes is not from the sample path or the effects. Levels of a grand note at VOL 100/127 are tabulated in README section 5 ("Where noise can come from").
* **Default VOL stays 100.** Trying VOL 115 for the grand (single notes still clean) failed the existing test 'a mezzo-forte triad at default volume leaves headroom (peak < 0.7)': the triad peaked at 0.82. Headroom is set by chords, not single notes; do not raise the default level without re-checking that test.

### 4e. The user's recording (piano.wav, 14 Nov) and what it showed
* Format: 14.9 s, 44.1 kHz, stereo 16-bit; right channel -34.5 dBFS RMS, left -83.9 (not recorded/connected: **do not read a silent channel as a hardware noise floor; a mid-keyboard note is almost centred** (pan law `cos/sin(pi/4 (1 + w pan))`), I made that mistake once in the conversation and retracted it). Notes: keys 71, 65, 72, 65, 65, 72, 72, 72 (onsets 3.25 .. 10.74 s), peaks -11.2 .. -22.9 dBFS.
* Haze (8-16 kHz) minus piano band (40-4000 Hz), 100-300 ms after the hit: -31.3 .. -38.5 dB (std 2.6), correlation with note peak +0.94 (loud notes have MORE haze relative to the note); constant for 3 s as both decay; no 12-20 kHz energy (floor -95 dBFS), no onset click; limited at ~12 kHz = Nyquist of a 24 kHz zone. The current engine on a PC, dry, same keys: -50.8 .. -58 (vel 60-127) and -62.7 .. -69.4 (vel 30). So the recorded haze is ~20 dB above the current bank: an older bank/codec, non-default settings, or a Pico-side effect (not reproducible on a PC: flash-cache stalls, CPU overload, audio path). Unknown which; `bankconf render` + `noise_report.py` are the way to find out.
* Open question for the user: which zip/bank (MP3-based? codec?), which settings (VOL HARD REV CHOR), which instrument; and the same notes recorded again with all effects off.

### 4f. Layered instruments and the loudness curve (measured on the user's allthree.wav, 14 Nov)
* **Recording** (one phrase on grand, Rhodes, clavinet at the same soft-ish velocity, right channel only): RMS of the 150 ms after the hit, medians: grand -35.9, Rhodes -24.7 (+11.2), clavinet -20.2 dBFS (+15.7); peaks -25.1 / -15.3 / -4.8. The pitch estimator picked different partials per timbre (keys 71-72, 55-60, 88-89): ignore pitch there.
* **Velocity sweep through the engine** (keys 55/57/60, all.yaml, default knobs) with the OLD per-layer law: at velocity 25-45 Rhodes +8 and clavinet +13 dB over the grand (reproduces the recording), a -10 dB step at the clavinet layer boundary (vel 65 -> 80) and -9 dB for the Rhodes at 54 -> 55. Layers: Rhodes (1-54, vref 27), (55-101, 78), (102-127, 114); clavinet (1-72, 36), (73-127, 100); vref = middle of the layer's range. Cause: `g = (vel/vref)^p` inside a layer boosts a soft layer by (72/36)^1.4 = +8.3 dB at its top while the next layer starts at (73/100)^1.4 = -4.4 dB; the sources are normalised, so no natural level difference compensates (the clavinet README says 'Normalized all samples').
* **Fix** (`layer_levels: law`, default): the tool normalises per LAYER (`GL`), stores zone gain `... + 20 p log10(vref/80)` (p = 1.404) so that a layer played at velocity v sounds at `(v/80)^p` on one curve; `natural` keeps the old behaviour (needed by the 16-layer Salamander if ever used, whose layers carry real level differences). After the fix: clavinet-grand +0.3..+2.6 dB, Rhodes-grand -0.7..-3.0 dB (RMS150) at velocities 25-127; Rhodes peaks stay ~6 dB below the grand (its crest factor is lower). Level matching now measures the first 300 ms (`LEVEL_WINDOW`): over 1 s a sustaining Rhodes and a decaying piano compare unfairly (the first version left the Rhodes -5.7 dB below the grand).
* The sweep script that produced these tables is `/tmp/real/velsweep.py` in the session; the permanent test is the 'layer_levels' block in `test_bank.py` (synthetic two-layer instrument: smooth rise across the boundary in `law`, a step in `natural`).

### 8c. Lessons from the second session
* **A refactor of a working engine needs a golden file**, not just tests that pass: the five md5 renders caught nothing because nothing broke, which is the point; they made the instrument / layer / loop / effects rewrite safe.
* **Make the new tool reproduce the old output byte for byte before adding features** (the bank tool did, md5 equal), then add loops and layers.
* **Look at the screen.** The hard-coded credit line was invisible to every test and obvious in the rendered panel.
* **Check the expectation independently before changing a failing test** (the loud/soft gain test: the engine was right, the expectation was computed from the wrong normalisation).
* **Calibrate an effect's level by measurement** (the reverb was 20 dB too quiet at first).

* **A stale binary gives a false PASS** (it happened again: a failed rebuild left the old test binary, which printed 'ALL PASS'). Delete the binary before building and check the build's exit code.
* **When a file changes and you did not change it, find out before building on it.** On 14 Nov the working tree changed under me: the golden file was re-recorded, a `test_limiter` and limiter tests appeared in two test files, and `docs/` images went back to an older state, none by any command of mine (I could not identify the writer; another session on the same sandbox is the likely explanation). I compared the tree with the last verified package, read every unexplained line, **reconstructed the old engine to prove the old golden still reproduced** (so only the limiter and encoder had changed), regenerated the figures from the engine, and verified the final package by unzipping it fresh and running everything.
* **A plausible cause is not a measured cause.** The MP3 pre-echo explanation for the hiss at the hit was written down before it was tested and was refuted by the measurement (no pre-echo); the real cause (the codec's SNR dip at the strike) was found by looking at 1 ms windows around the onset. State a hypothesis as a hypothesis until it has a number.
* **Measure, then change, and measure again after the change.** After the limiter fix the user reported the noise was still there: the measurement that had not been made (ADPCM error per frequency band over time) showed the real cause. A test that proves one cause fixed says nothing about the next one.
* **A test estimator can be tuned to a noise floor by accident.** The pitch test picked the most prominent harmonic; when the codec noise fell, a sharper partial won and the test failed although the engine was right (the fundamental was within 0.1 cent). Fixed by using the tool's own definition (the lowest clearly present partial).
* **A test that uses a convenient signal can hide a whole class of bug.** The reverb level test used one sine; real notes got up to 16 dB more wet energy. Calibrate and test with the real material (notes, chords), at several settings of every related knob.
* **Do not infer a hardware property from a channel that may not have been recorded.** And do not raise a default level by reasoning about single notes: the existing triad-headroom test (peak < 0.7 at the default volume) caught VOL 115 immediately (0.82).
* **Test the response of a parameter across its whole range, not at two points.** The layer/velocity bug sat between velocities 80 and 110 (the only ones I had surveyed); one recording at a soft touch and a sweep over 25-127 found a 13 dB difference and a 10 dB step.
* **Measure, then change.** The 'noisy at high velocity' report was tested against three suspects with the real engine (ADPCM error over time: steady -38 dB; limiter distortion: -33 dB; both measured) before anything was edited.

### 9. Open items
0. **Listen again, loud and soft, with the PCM head and the lossless grand.** If hiss remains: a longer head (`pcm_head_ms` 12-20; 20 ms gave worst-window 35 dB too), a higher sample rate for the treble zones, an 8-tap predictor (+0..+3 dB measured offline), or a second velocity layer.
1. **First flash:** program size (must be < ~3.9 MB), the CPU square with 20 keys + pedal and with effects on, XIP behaviour with 24 streams; then listen, **especially to the effects** (reverb level, chorus depth, wah gain were set by measurement).
2. **Open the GUI in a real browser** (layout, the file browser, audition playback); it has only been run through the API and a Node DOM stub.
3. **Rhodes / clavinet:** `banks/all.yaml` builds against the real libraries (`tools/fetch_rhodes.sh`, `fetch_clavinet.sh`; jRhodes3c is CC BY-NC-SA 4.0, Stevie Clavinet has an unknown licence: private use). Built and played through the engine, **not listened to**; the clavinet's SNR is low (mean 27.9 dB). Layers switch at note-on (no crossfade); loops only at the sample's end.
4. If 4-bit ADPCM is not good enough: re-encode with a better encoder (noise shaping, lookahead); with a 16 MB board go 8-bit / more layers / longer notes.
5. Ideas: release samples; sympathetic resonance; half-pedal; per-key tuning blend; a velocity crossfade between layers; effect settings per instrument beyond the knob defaults; an expression-pedal mapping for the tremolo rate.
