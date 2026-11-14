Digital synthesis\
a reader built on the picomusic tower

1. [Before the first sample](#ch0)
2. [Sound as numbers](#ch1)
3. [Oscillators](#ch2)
4. [Filters](#ch3)
5. [Envelopes, LFOs and control rate](#ch4)
6. [Additive synthesis and the tonewheel organ](#ch5)
7. [Divide-down organs and string machines](#ch6)
8. [Sampling](#ch7)
9. [Drum synthesis](#ch8)
10. [FM synthesis and the DX7](#ch9)
11. [Effects](#ch10)
12. [Real-time engineering on a microcontroller](#ch11)
13. [What the engines sound like](#ch12)
14. [What is missing, and what to build next](#ch13)
15. [Reading list and glossary](#refs)

Chapter 0

# Before the first sample

Seventeen ways to make a sound, one small chip to make them on.

This reader follows the seven instruments in the *picomusic* project (a tonewheel organ, an analog synth, a string machine, a bass synth, a drum machine, a sampled piano and a DX7-style FM synth) and uses them as a tour through digital sound synthesis. Each chapter starts with the instrument or sound that people originally built, explains how it is turned into numbers, goes down to the details that real instruments and plug-ins care about, and ends with what that looks like on a Raspberry Pi Pico 2.

The hardware every instrument runs on: Pico 2 at the bottom, then the USB/MIDI shield, the audio shield and the LCD shield on top.

The hardware every instrument runs on: Pico 2 at the bottom, then the USB/MIDI shield, the audio shield and the LCD shield on top.

## The budget that shapes everything

A synthesizer is a program that must produce one new number every 1/44100 of a second and may never be late. Late means a click, or on this hardware a stretched clock and a muted DAC. Everything in this reader is a trade between sound quality and the time available for one sample.

**44 100**samples per second, stereo, 16 bit

**5 442**core cycles per sample at 240 MHz

**5.8 ms**one 256-frame audio buffer

**2**Cortex-M33 cores: one for audio, one for everything else

5 442 cycles sounds like a lot until you have 16 voices, each with several oscillators, a filter, envelopes and then effects. The RP2350 has a hardware floating-point unit and hardware divide, which is why most of the engines use plain floats. It has no cache-friendly big memory (520 KB of SRAM, 4 MB of flash for code and samples), which is why memory shows up as a design constraint in the sampling chapter.

### Pico 2 tower The ground rules

- **Core 1** does audio only: it computes a buffer and feeds the I²S state machine, which also paces the loop. It runs from SRAM so flash cache misses never make it late.
- **Core 0** handles MIDI, the display, the serial console and every calculation that needs the maths library.
- The audio core aims to stay under **80 % load**; each instrument shows a CPU square on its display (green under 50 %, orange to 80 %, red above).

Chapter 1

# Sound as numbers

Sampling, aliasing and the two ways a digital oscillator can lie.

Air pressure over time is a continuous curve. A digital system keeps only its value at regular instants. The sampling theorem says nothing is lost if the curve contains no frequency at or above half the sample rate (22 050 Hz here, the *Nyquist frequency*). The catch is the "if".

Top: a 5 kHz sine is described well by about nine samples per cycle. Bottom: a 39.1 kHz sine produces exactly the same samples as a 5 kHz one. Frequencies above

Top: a 5 kHz sine is described well by about nine samples per cycle. Bottom: a 39.1 kHz sine produces exactly the same samples as a 5 kHz one. Frequencies above Nyquist fold back as aliases.

Real instruments never have this problem, because air does not sample. A digital oscillator that generates a sharp-edged wave (sawtooth, square) contains harmonics that extend far above 22 kHz. Those harmonics do not disappear; they *fold* down into the audible range at frequencies that have no musical relationship to the note. This is aliasing, and it is the single most important difference between a good and a bad virtual instrument.

## Where aliasing comes from

- **Sharp waveforms** (saw, square, pulse): infinite harmonic series.
- **Nonlinear processing** (clipping, overdrive, waveshaping): every distortion creates new harmonics, usually above Nyquist.
- **Pitch-shifting by interpolation** in samplers, and **FM with a large index**: sidebands run past Nyquist.
- **Hard sync and ring modulation** of rich waves.

The remedies come in three families: generate the wave already band-limited (BLEP, wavetables per octave), compute at a higher rate and filter down (oversampling), or accept it and hide it (a low-pass filter after the oscillator, or a sound that is noisy anyway, like a hi-hat).

## Other numbers worth knowing

- **Quantisation:** 16 bits give about 96 dB between full scale and the rounding noise. Plenty for output, tight for intermediate maths in fixed point. Floating point removes the worry.
- **Latency:** a buffer of N frames adds N/44100 s. The tower uses 256 frames (5.8 ms), small enough to feel instant.
- **Jitter and clocking:** a DAC needs a steady clock; a late buffer shows up as a pitch wobble or a dropout.

### Pico 2 tower I²S output, 16 bit, two DAC boards

The audio leaves the chip through a PIO state machine that speaks I²S, at 44.1 kHz in every instrument. The two Waveshare Pico-Audio versions need different bit timing: the CS4344 board tolerates a program that holds each data bit for two clock periods, the PCM5101A board samples on every clock edge and needs standard I²S. Using the wrong one gives silence or heavy distortion, which is why a single `rev2_1` switch in `platformio.ini` picks both pins and program.

Why the PCM5101A board distorts with the wrong PIO program: it reads each bit twice.

Why the PCM5101A board distorts with the wrong PIO program: it reads each bit twice.

Chapter 2

# Oscillators

From vibrating things to a counter and a table.

## Origins

An **analog VCO** charges a capacitor and resets it, producing a sawtooth whose pitch follows a control voltage. A **tonewheel** spins a toothed iron wheel in front of a magnet. A **divide-down organ** (Solina, Roland strings, most 1970s home organs) runs twelve master oscillators at the top octave and divides them down with flip-flops, so every octave is locked exactly. Each of these has a signature: the VCO drifts, the tonewheel is nearly a pure sine, the divider chain is perfectly in tune but static.

## The digital oscillator: a phase accumulator

Nearly every digital oscillator is a counter. Each sample, a *phase* register is incremented by a step proportional to the frequency, and wraps around at one cycle. The phase then picks a value: directly (the phase itself is a sawtooth), through a comparison (square), or through a table lookup (sine, or any single cycle you stored).

A phase accumulator (left) wraps at 1. A one-cycle table (middle) is read at that phase. The result (right) is a sine whose pitch is set by the step size alone.

A phase accumulator (left) wraps at 1. A one-cycle table (middle) is read at that phase. The result (right) is a sine whose pitch is set by the step size alone.

Integer phase is attractive: a 32-bit unsigned register wraps for free, and the top bits index a table. An octave up is `phase << 1`, which is how the string machine gets three phase-locked octaves from one accumulator.

## Wavetables and the stack

A **wavetable** oscillator reads a stored cycle. Store many cycles with slowly changing shapes and sweep the position (by hand, an envelope or an LFO) and you have the *wavetable scanning* of PPG, Waldorf and Serum-style instruments. Table size sets the noise floor; interpolating between table entries cleans it up. A correct implementation keeps a version of each table per octave, with the harmonics above Nyquist already removed.

A wavetable stack: each line is one stored cycle. Moving a position control smoothly morphs the timbre.

A wavetable stack: each line is one stored cycle. Moving a position control smoothly morphs the timbre.

## Making a saw without aliasing: PolyBLEP

A naive sawtooth jumps from +1 to −1 once per cycle, and that sample-sharp jump is what creates the unlimited harmonics. **BLEP** methods (band-limited step) subtract a small correction around each jump so the edge is smoothed over about two samples, which is almost the same as band-limiting. PolyBLEP uses a two-piece polynomial as that correction: cheap enough to use in every voice.

A 3.1 kHz sawtooth at 44.1 kHz. Top: naive, with an aliasing floor between and above the true harmonics. Bottom: PolyBLEP, where the floor falls by tens of dB f

A 3.1 kHz sawtooth at 44.1 kHz. Top: naive, with an aliasing floor between and above the true harmonics. Bottom: PolyBLEP, where the floor falls by tens of dB for almost no cost.

Other approaches: DPW (differentiate a parabolic wave), minBLEP (precomputed minimum-phase step tables, best quality, more memory), additive banks limited to harmonics below Nyquist, or oversampling.

### Pico 2 tower Wavetable, PolyBLEP and one that doesn't bother

- **Analog synth:** two phase-accumulator oscillators read a 4096-point single-cycle table. The saw and square tables are *not* band-limited, so high notes alias; the filter hides most of it (a documented limitation).
- **Bass and string machine:** PolyBLEP saws and squares. On the PC the test measures at least 15 dB less alias energy than a naive saw at every pitch from C4 to C8.
- **808 drum hats:** six naive squares on purpose. Some aliasing sounds right for metal, and the hat is mostly noise anyway.

The analog synth voice: oscillators, ladder filter, amplifier, with two envelopes and an LFO.

The analog synth voice: oscillators, ladder filter, amplifier, with two envelopes and an LFO.

Chapter 3

# Filters

The ladder that every synth sound is built around.

## Origins

Subtractive synthesis takes a harmonically rich oscillator and carves it with a resonant low-pass filter whose cutoff moves. The classic design is the **Moog transistor ladder**: four identical one-pole stages in a chain with the output fed back to the input. The feedback amount is the resonance; at the right level the loop sustains itself and the filter becomes a sine oscillator at the cutoff frequency.

Left: each pole adds 6 dB per octave of slope; the ladder has four, so 24 dB per octave. Right: feeding the output back (k) puts a peak at the cutoff, and at k

Left: each pole adds 6 dB per octave of slope; the ladder has four, so 24 dB per octave. Right: feeding the output back (k) puts a peak at the cutoff, and at k = 4 the loop oscillates. The passband falls as resonance rises, the famous Moog bass loss.

## Digital implementation

1. **Direct translation.** Replace each stage by a one-pole recursion `y += g·(x − y)`, with `g` from the cutoff. Cheap, but the cutoff-to-`g` mapping is only right at low frequencies, and the feedback loop has a one-sample delay that detunes the resonance.
2. **Nonlinear stages.** The real transistor pairs saturate (`tanh`-like). Models such as Huovilainen's put a saturation in every stage. Resonance then becomes level-dependent, which is a lot of the "character".
3. **Zero-delay feedback (TPT).** Solve the feedback loop within the sample instead of using last sample's value. Self-oscillation stays in tune and the cutoff is accurate up to near Nyquist. This is the approach of most modern analog-modelling plug-ins.
4. **Oversampling.** Saturating filters create harmonics; running them 2× or 4× oversampled reduces aliasing at a multiplied cost.

## Stability and tuning are the details that matter

A digital filter has a ceiling beyond which it becomes unstable, and a mapping that is exact at 200 Hz can be a semitone off at 5 kHz. A self-oscillating filter played from a keyboard should track one cent per cent, which is a test few implementations pass.

### Pico 2 tower Two ladders, one lesson learned the hard way

- **Analog synth:** a Huovilainen-topology ladder (`s += g·(sat(in) − sat(s))` per stage, resonance from stage 4). Its coefficient becomes unstable above about 14 kHz nominal, so the cutoff ceiling is fixed at 12 kHz, where the filter is open anyway.
- **Bass synth:** while building it, the team worked the loop out exactly and found that the synth's `g = tan(π f / fs)` placed the real pole at about *half* the nominal frequency. The bass uses exact coefficients with a measured compensation. Result: it self-oscillates within 0.4 cents of the target from 55 Hz to 3.5 kHz and tracks the keyboard.

Measured on the bass synth's own filter code. Left: response with rising resonance. Middle: self-oscillation lands on the nominal cutoff, whereas the old mappin

Measured on the bass synth's own filter code. Left: response with rising resonance. Middle: self-oscillation lands on the nominal cutoff, whereas the old mapping would sit near 0.5. Right: pitch error stays within ±10 cents.

The ladder runs at 44.1 kHz without oversampling; with high resonance on high notes some aliasing is possible (not measured), which is why the cutoff is capped at 5.5 kHz in the bass.

Chapter 4

# Envelopes, LFOs and control rate

Making notes begin, last and end, and moving things slowly.

## Origins

A piano string rings and fades; an organ holds until the key is released; a violin bow swells. Synthesizers abstract this into an **envelope generator**: a control signal that rises on key press (attack), settles (decay) to a held level (sustain) and falls on release. It multiplies the amplifier and, often, the filter cutoff. An **LFO** is an oscillator below 20 Hz that wobbles pitch (vibrato), cutoff (wah) or amplitude (tremolo).

ADSR. Analog envelopes are exponential because they are capacitors charging; a straight-line ramp (dashed) sounds mechanical, especially for decay and release.

ADSR. Analog envelopes are exponential because they are capacitors charging; a straight-line ramp (dashed) sounds mechanical, especially for decay and release.

## Digital implementation

- **Control rate.** Nothing about an envelope needs 44 100 updates per second. Compute it every N samples and interpolate between values. Cheaper by a factor of N, and the interpolation removes zipper noise.
- **Exponential segments** by one multiplication per step (`x *= r`), no `exp()` in the loop.
- **Click-free everything.** A sudden change in gain is a click. Retriggering a voice, stealing a voice and changing a parameter all need a short fade.
- **Voice allocation.** What happens when the 13th note arrives and there are 12 voices? Steal the oldest or the quietest, but fade it out over a few milliseconds.

### Pico 2 tower Control at 2 756 Hz, voices that fade before they are stolen

Every engine advances its envelopes, LFOs and parameter smoothing once per 16 samples (`CONTROL_DIV`, 2 756 Hz) and interpolates in between. Polyphony is a limit on *sounding* notes plus a slightly larger pool of slots, so a stolen voice can fade out for a few milliseconds in a spare slot while the new note starts in a free one. A compile-time check enforces that the pool is bigger than the limit.

What happens at the polyphony limit in the analog synth: the new note starts in a free slot while the stolen voice fades.

What happens at the polyphony limit in the analog synth: the new note starts in a free slot while the stolen voice fades.

Chapter 5

# Additive synthesis and the tonewheel organ

Every sound is a sum of sines, and one famous instrument built that into hardware.

## Origins

Fourier showed that any periodic sound is a sum of sine waves at multiples of the fundamental. The **Hammond B-3** (1935) is that idea built in iron: ninety-one tonewheels spin at fixed speeds, each producing a nearly pure sine, and a key sends nine of them at once (footages of 16′, 5⅓′, 8′, 4′, 2⅔′, 2′, 1⅗′, 1⅓′, 1′, the harmonics of the note) to a mixer whose nine sliders are the **drawbars**. Pull a drawbar out for more of that harmonic. The look and sound come from the details: key click from the contacts, leakage between wheels, percussion that comes from a decaying second or third harmonic, and a rotating speaker.

Nine drawbars on the tonewheel organ: each sets the level of one harmonic.

Nine drawbars on the tonewheel organ: each sets the level of one harmonic.

Left: adding odd harmonics builds a square wave; each extra sine sharpens the edges. Right: two drawbar registrations as nine bars, with the multiple of the fun

Left: adding odd harmonics builds a square wave; each extra sine sharpens the edges. Right: two drawbar registrations as nine bars, with the multiple of the fundamental under each footage.

## Digital implementation

Additive synthesis is the most literal digital method: N sine oscillators, each with its own amplitude, summed. It is exact and gives total control over the spectrum, but the cost is N oscillators per note. Resynthesis tools analyse a real sound into hundreds of partials with time-varying amplitudes and replay them (this is where Razor-style additive engines and spectral editors live). Three things make additive synthesis cheaper in practice:

- Only the partials below Nyquist need computing.
- Many partials share a phase accumulator: the 2nd harmonic is the phase times two.
- Sines come from one table, with linear interpolation.

## The effects that make the organ

A plain sum of sines is clinical. The organ sound needs **key click** (a short noise burst), **percussion** (a decaying 2nd or 3rd harmonic that triggers only on the first key of a chord), **overdrive** from the tube preamp, **vibrato/chorus** from a scanner, and the **Leslie** speaker: a rotating horn and drum whose Doppler shift and amplitude modulation, picked up by two microphones, give the shimmering stereo movement.

The tonewheel signal chain: voices, percussion and click, overdrive, chorus, Leslie, master volume.

The tonewheel signal chain: voices, percussion and click, overdrive, chorus, Leslie, master volume.

### Pico 2 tower Nine partials × 16 notes, and a governor that counts

- Each note is nine sine partials with Hammond footages; 16 notes of polyphony means up to 144 oscillators in flight.
- The **governor** measures what an oscillator really costs on this chip each buffer and works out how many fit under 80 % load. Without it, a heavy chord with every effect on would stretch the I²S clock (pitch falls) and finally make the DAC lose lock.
- **Overdrive:** an asymmetric soft clipper, 0 to +36 dB. A plain waveshaper aliased badly, so it uses 2× oversampling with antiderivative anti-aliasing, keeps the bass out of the clipper, and costs roughly 250 to 300 cycles per sample (about 5 % of the budget) while on.
- **Leslie:** an 800 Hz crossover; the horn gets a modulated delay (about ±2 % Doppler pitch shift at full speed) and 60 % amplitude modulation, the drum 40 %; two virtual microphones a quarter turn apart make it stereo.

Aliasing hash between harmonics, single note A4 at maximum drive: the shipped overdrive (orange) sits 25 to 30 dB below a plain waveshaper (red).

Aliasing hash between harmonics, single note A4 at maximum drive: the shipped overdrive (orange) sits 25 to 30 dB below a plain waveshaper (red).

The Leslie model.

The Leslie model.

The governor keeps the audio core under 80 % load.

The governor keeps the audio core under 80 % load.

Chapter 6

# Divide-down organs and string machines

Sawtooth waves, a few fixed filters, and a chorus that makes it sound like a section.

## Origins

The ARP/Solina String Ensemble (1974) did not model violins at all. It produced a sawtooth for every note (from divider chains), shaped it with a few fixed band-pass filters labelled *cello*, *viola*, *violin* and *bass*, and then ran everything through a lush stereo chorus built on bucket-brigade delay chips. The chorus is the instrument: it blurs the saw into something that sounds like many players.

The string machine signal path. Notice that only the voices are per-note; the filters and the ensemble are shared.

The string machine signal path. Notice that only the voices are per-note; the filters and the ensemble are shared.

## Digital implementation

- Each note is a phase accumulator giving three saws at 16′, 8′ and 4′ by bit-shifting the phase, so the octaves are phase-locked just like a divider chain.
- Instead of a filter per voice, **sum all voices onto buses by footage and filter the sum once**. The filter cost does not grow with the number of notes. This works because the filters are linear and fixed.
- The **ensemble** is a delay line read at three points whose delays are swept by LFOs 120° apart, so the mean delay stays constant and the pitch does not drift. Dry plus a delayed copy gives a comb filter whose notches move; three of them moving make the sound wide and alive.

Left: a delayed copy added to the signal gives a comb of notches every 1/delay hertz. Right: three delay taps swept by LFOs 120° apart.

Left: a delayed copy added to the signal gives a comb of notches every 1/delay hertz. Right: three delay taps swept by LFOs 120° apart.

### Pico 2 tower Cheap polyphony by sharing

One 32-bit accumulator per voice makes the 16′ saw; the 8′ and 4′ are `phase << 1` and `phase << 2`. The saws are PolyBLEP-corrected, and a voice only computes the footages the current registration uses. Five registrations (bass, cello, viola, violin, pizzicato) are fixed filters on the shared buses. The ensemble has three taps around 5 ms with ±2.4 ms slow modulation plus a fast 6.2 Hz shimmer. On the PC the shared chain alone costs 51 ns per sample and 16 notes 175 ns, so sixteen notes cost little more than a handful.

The ensemble section: delay taps, LFO phases and the stereo spread.

The ensemble section: delay taps, LFO phases and the stereo spread.

Chapter 7

# Sampling

Record the real thing, play it back at the right pitch, and fit it in memory.

## Origins

The Mellotron (1963) played tape strips, one per key. The Fairlight and the Synclavier (late 1970s) did it digitally; the E-mu Emulator and the Akai S-series made it affordable. Today's sample libraries (Kontakt instruments, Spitfire, Salamander piano) are the same idea at scale: gigabytes of recordings with rules about which one to play.

## Digital implementation: the three problems

1. **Pitch.** Nobody records all 88 keys at all dynamics, so a sample is reused for nearby notes by reading it faster or slower (a phase increment that is not a whole number) with interpolation between samples. Shifting more than a few semitones starts to change the character and alias.
2. **Dynamics.** A hard-struck string is not a loud soft-struck string; it is brighter and different. So the instrument keeps several **velocity layers** and either switches or crossfades between them.
3. **Length and memory.** A piano note rings for 20 seconds. Looping a sustain section (with a seamless crossfaded loop point) lets a short recording sustain; compression lets more fit.

IMA ADPCM: each sample is stored as a 4-bit difference from a predicted value, and the step size grows or shrinks to follow the signal. Error is largest in the

IMA ADPCM: each sample is stored as a 4-bit difference from a predicted value, and the step size grows or shrinks to follow the signal. Error is largest in the first moment, while the step adapts.

## Nitty-gritty

- **Round robin and key-release samples** (the hammer lift, the damper noise) make a library feel alive; the sampler scripting languages in commercial products exist mostly to manage them.
- **Streaming from disk:** big libraries keep only the start of each sample in RAM and stream the rest.
- **Interpolation quality:** linear is cheap and dull at the top end; cubic or windowed-sinc keep the highs.
- **Compression:** 4-bit ADPCM gives 4:1 at modest cost, with a noise floor that follows the signal.

Why the piano stores 4-bit ADPCM: 3 MB of flash is 34 seconds of 16-bit audio, but 136 seconds at 4 bits, and more if the high notes use a lower rate.

Why the piano stores 4-bit ADPCM: 3 MB of flash is 34 seconds of 16-bit audio, but 136 seconds at 4 bits, and more if the high notes use a lower rate.

### Pico 2 tower A grand piano in 3 MB of flash

- The Salamander Grand ships as 30 zones, **3.0 MB of 4-bit ADPCM**, played straight from flash. The whole firmware uses 74 % of the Pico 2's 4 MB, and there is no file system.
- The codec is IMA ADPCM's quantiser with a new **four-tap adaptive predictor** in integer maths. Measured on the shipped grand, SNR is **35.6 to 51.4 dB, mean 45.4**, against a mean of 38.0 for plain IMA with the same source. The hammer strike, where a 4-bit quantiser struggles, is handled separately.
- **24 notes** of polyphony (32 slots) and velocity layers, seamless loops, pedal and damper behaviour, plus wah, tremolo/auto-pan, chorus and reverb. Pitch shift is at most one semitone, so at most one ADPCM sample is decoded per output sample.
- A **desktop bank configurator** chooses zones, rates and loops against a memory budget.

The bank configurator: zones, key ranges and the memory budget.

The bank configurator: zones, key ranges and the memory budget.

Velocity layers and the loudness curve measured on the engine.

Velocity layers and the loudness curve measured on the engine.

Spectrogram of a note from the shipped bank.

Spectrogram of a note from the shipped bank.

Piano engine architecture.

Piano engine architecture.

Chapter 8

# Drum synthesis

Circuits that ring, noise that hisses, and a clock that must never wobble.

## Origins

The Roland TR-808 (1980) made drums out of a handful of circuits: a **bridged-T resonator** kicked by a pulse gives the kick and the toms; shaped **noise** through filters gives snare and clap; **six square-wave oscillators** at inharmonic ratios, high-passed, give the hi-hats and cymbal. The sounds are not realistic, and that is why they are iconic.

A synthesized kick is a sine with a rapidly falling pitch (left, middle). A metallic hat is six unrelated square waves through a high-pass filter, with no harmo

A synthesized kick is a sine with a rapidly falling pitch (left, middle). A metallic hat is six unrelated square waves through a high-pass filter, with no harmonic structure at all (right).

## Digital implementation

- **Kick/tom:** a sine oscillator with an exponential pitch envelope and amplitude decay. Tune, decay and the sweep depth are the three knobs.
- **Snare/clap:** a pitched tone mixed with filtered noise; the clap fires several short noise bursts.
- **Metal:** six squares at ratios such as 205, 304, 370, 523, 540 and 800 Hz.
- **Sequencer:** the step clock must be **sample accurate**, independent of MIDI polling jitter. Swing delays the odd steps inside each pair.

The sixteen drums at the default kit, velocity 127, rendered by the real engine.

The sixteen drums at the default kit, velocity 127, rendered by the real engine.

### Pico 2 tower All synthesized, sixteen of them, with a sample-counting sequencer

Seven synthesis types cover the 16 instruments, with five parameters per drum (level, tune, decay, tone, pan). The sequencer runs **on the audio core** and counts samples, so step times are exact whatever core 0 is doing; patterns, a song chain, swing and MIDI clock sync are all handled there, with pattern changes queued to the bar line (verified to the sample). Pad and clock events reach the audio core through single-writer mailboxes with no locks.

Sequencer timing verified on the engine: swing, tempo and the bar-line pattern change.

Sequencer timing verified on the engine: swing, tempo and the bar-line pattern change.

A desktop **drum editor** (pattern grid, song chain, kit and sound sliders, a library of 43 patterns) renders audition through the firmware's own engine on a PC and moves the whole state to the machine over USB.

The drum editor, pattern page.

The drum editor, pattern page.

Chapter 9

# FM synthesis and the DX7

Sine waves modulating sine waves, in a pocket-sized algebra.

## Origins

John Chowning discovered in 1967, and published in 1973, that using one audio-rate oscillator to vary the frequency of another produces a rich, controllable spectrum from two sines. Yamaha licensed it and the **DX7** (1983) became the sound of the decade: electric pianos, bells, bass, marimbas. The instrument has six **operators**, each a sine oscillator with its own envelope, wired together in one of **32 algorithms**. An operator is a *carrier* if you hear it and a *modulator* if it feeds another.

Three of the 32 DX7 algorithms. Arrows run from modulator to the operator it modulates; orange operators are carriers; fb is operator feedback.

Three of the 32 DX7 algorithms. Arrows run from modulator to the operator it modulates; orange operators are carriers; fb is operator feedback.

## Why it works

A carrier at frequency *fc* modulated by a sine at *fm* with index *I* has sidebands at *fc ± k·fm* whose amplitudes are Bessel functions Jₖ(*I*). Low index: almost a pure sine. Rising index: energy spreads outward. When fm : fc is a simple ratio the sidebands land on harmonics and the sound is pitched; with an irrational ratio they do not, and you get bells.

Sidebands of one carrier. Left to right: a small index gives few sidebands, a bigger one spreads energy, and an irrational ratio (right) makes the components in

Sidebands of one carrier. Left to right: a small index gives few sidebands, a bigger one spreads energy, and an irrational ratio (right) makes the components inharmonic, as in a bell.

## Digital implementation and nitty-gritty

- In practice the DX7 and its emulations use **phase modulation**: the modulator's output is added to the carrier's phase, which is mathematically the same as FM for sines and cheaper to compute.
- Per operator: a phase accumulator, a sine table read, an amplitude from an envelope, and a multiplication. Six per voice.
- The DX7 works in the **log domain**: levels are 14-bit-ish numbers in which 256 units is one octave of amplitude, and a lookup table converts them. Envelopes, velocity sensitivity, key-level scaling and rate scaling are all added in logs before one conversion.
- **Operator feedback** (an operator modulating itself) turns a sine into a sawtooth-like wave.
- **Aliasing is the price** of a big index: sidebands that reach past Nyquist fold back. Software emulations (Dexed, FM8) use higher internal rates or limit the index.
- The 128-byte VMEM format and the SysEx bulk dump (32 voices in 4 104 bytes) made thousands of voices shareable, which is why the instrument stayed alive.

### Pico 2 tower Six sines, 32 algorithms, and the DX7's own bank format

- The engine follows the DX7 structure: six sine operators, the 32-algorithm table (layout from the open-source msfa project, Apache-2.0), feedback, velocity, key-level and rate scaling, LFO, 12 factory voices. 12 notes of polyphony (pool 16); 113 KB of flash.
- Levels use the log-unit scheme above (256 per octave, a 4 352-entry amplitude table and a 4 096-entry sine table). Core 0 prepares the note data, the audio core runs envelopes and stealing.
- It accepts **DX7 32-voice SysEx banks** over UART, USB device and USB host, with checksum and truncation checks. Banks are not stored in flash; a 4 104-byte dump takes about 1.4 s over a DIN cable.
- Verified on the PC: all 32 carrier counts, VMEM round trip, pitch accuracy and the sideband amplitudes against exact Bessel functions. Not reproduced: the pitch envelope, oscillator key sync, LFO key sync; envelope and LFO shapes are approximations, so it will not match a real DX7 or Dexed patch exactly. It has been played on the tower and works. CPU load has not been measured.

Chapter 10

# Effects

Time, distortion and space: the parts that make a synth sound like a record.

Effects reuse a few building blocks. A **delay line** (a circular buffer) is the heart of echo, chorus, flanger, phaser (via all-pass chains), reverb and physical models. A **filter** makes wah, tone and EQ. A **waveshaper** makes overdrive and fuzz. An **LFO** moves any parameter.

## Reverb

A room is thousands of reflections. The classic cheap model (Schroeder 1962, refined in Freeverb) uses several parallel **feedback comb filters**, each a short delay with feedback and damping, giving a dense decaying tail, followed by **all-pass** filters that smear the echoes without colouring the level. Delay lengths are chosen mutually prime so the resonances do not stack. Left and right channels use slightly different lengths for width. Modern reverbs use feedback delay networks, or **convolution** with a recorded impulse response, which is accurate but costs many multiplications per sample.

A Freeverb-style reverb: four combs in parallel, two all-passes in series. A single impulse comes out as a dense, decaying tail.

A Freeverb-style reverb: four combs in parallel, two all-passes in series. A single impulse comes out as a dense, decaying tail.

## Distortion without alias

Distortion is a sudden gain change, so it creates harmonics without limit. Cures: oversampling (2× to 8×) around the nonlinearity, **antiderivative anti-aliasing** (ADAA), which uses the integral of the nonlinear function to cancel most of the foldover at 1× cost, and filters before and after. This was the tonewheel's overdrive story in chapter 5.

### Pico 2 tower One effects block shared by the piano and the DX7

The piano's effects were extracted to a shared `stereo_fx.h`, which now serves the DX7 as well: **wah** (pedal or auto), **tremolo / auto-pan**, **chorus**, and a **reverb** of four damped feedback combs and two all-passes per channel with different lengths left and right. The input passes a 150 Hz high-pass (bass notes were exciting the comb resonances), and the wet level is made independent of reverb time. RT60 measures 0.44 s, 0.89 s and 5.56 s at the size knob's 0, 64 and 127. Bypass is bit-exact. The tonewheel has its own overdrive, vibrato, chorus and Leslie; the strings its ensemble; the synth and the bass their overdrive.

Chapter 11

# Real-time engineering on a microcontroller

Why the code looks the way it does.

Everything above sits on a platform with rules. These are the ones that matter, and how the tower follows them.

1. **Never be late.** The audio loop has a hard deadline. No allocation, no printing, no waiting on a lock, no unbounded loops.
2. **Separate fast from slow.** Per-sample work does only adds and multiplies. Anything that needs `pow`, `exp`, `tan` (filter coefficients, pitch to frequency) is computed once, on the other core or at control rate, and handed over.
3. **Communicate without locks.** Single-producer/single-consumer ring buffers and single words each valid on their own. Panic and all-notes-off are *counters*, not events, so a full queue can never lose them.
4. **Keep the hot path in fast memory.** The audio loop runs from SRAM; every function it calls is forced inline so a flash cache miss cannot stall a sample.
5. **Measure.** The cycle counter gives each engine an honest CPU figure, and the governor turns it into polyphony.
6. **Prove it off-target.** The real firmware sources compile on a PC against mocked hardware and are tested with sanitizers, two-thread races and an I²S decoder model.

The core split: core 0 hands voices to core 1 with a memory barrier; core 1 owns a live voice.

The core split: core 0 hands voices to core 1 with a memory barrier; core 1 owns a live voice.

PIO allocation: audio (I²S) and the USB host use the programmable I/O blocks, leaving both CPU cores free for the engine.

PIO allocation: audio (I²S) and the USB host use the programmable I/O blocks, leaving both CPU cores free for the engine.

## Float or fixed?

On a chip without a floating-point unit (RP2040, many DSPs) engines are written in fixed point, with care for overflow and scaling. The RP2350's single-precision FPU makes float cheap enough that nearly all of these engines use it. The exceptions are places where integers are natural: phase accumulators (free wraparound), the piano's integer ADPCM decoder, and the DX7's table-driven log arithmetic.

### Pico 2 tower Where the time goes

Concrete figures from the project documentation: the overdrive costs about 250 to 300 cycles per sample while on, the tonewheel's governor allows as many notes as fit under 80 % load, and on the PC the string machine's 16 notes cost 175 ns per sample against 51 ns for the shared chain alone. Real Pico numbers for most engines are the first thing to record on the CPU square. Treat PC benchmarks as relative.

Chapter 12

# What the engines sound like

Spectrograms of the six demo recordings, rendered by the real firmware engines.

These are spectrograms (time across, frequency up, brightness for level) of the demo files rendered from the engines themselves. Each method leaves a visible fingerprint.

The six demos side by side. Look for: the organ's single narrow lines (nearly pure sines) and the broadband smear when the Leslie and overdrive are on; the synt

The six demos side by side. Look for: the organ's single narrow lines (nearly pure sines) and the broadband smear when the Leslie and overdrive are on; the synth's saw harmonics with the filter opening; the FM bells' unevenly spaced lines; the piano's decaying partials; the drums' vertical bars; the string machine's dense, wide spectrum.

The bass synth's own spectrogram: the ladder cutoff sweeping across a saw spectrum.

The bass synth's own spectrogram: the ladder cutoff sweeping across a saw spectrum.

Chapter 13

# What is missing, and what to build next

The strategies and instruments the tower does not have yet, ranked by how well they fit a Pico 2.

Seven instruments cover the classical core. Plug-in families such as Native Instruments' (Massive, FM8, Reaktor, Kontakt, Absynth, Razor, Prism and others) add several more ideas. This is the map of what the tower does not yet do.

Methods against instruments. Teal = used in that instrument. Below the orange line: widely used strategies with no instrument yet.

Methods against instruments. Teal = used in that instrument. Below the orange line: widely used strategies with no instrument yet.

## The missing strategies, and how they would fit

### Physical modelling

Simulate the vibrating object. The simplest is **Karplus-Strong**: a delay line filled with noise and fed back through a mild low-pass filter rings like a plucked string. **Digital waveguides** extend it to bowed strings, flutes and brass; **commuted synthesis** folds in a body resonance.

**Fit: excellent.** One short delay line per voice, a few operations per sample. Dozens of voices are plausible.

### Modal synthesis

A struck bar, bell or plate is a handful of damped resonances at inharmonic ratios. A bank of two-pole resonators excited by a short burst models marimba, vibraphone, bells, tines of an electric piano. (Native Instruments' Prism is, to my knowledge, built on this idea.)

**Fit: excellent.** 8 to 32 resonators per voice are cheap; a Rhodes or a vibraphone is a good next project.

### Wavetable scanning

Hundreds of stored cycles, morphed by a position knob, with oscillator tables built per octave to avoid aliasing; the heart of Serum, Massive and the PPG.

**Fit: good.** 64 frames × 2048 × 16 bits = 256 KB per table, which fits SRAM or flash. Interpolate in both phase and position; per-octave mip-maps handle aliasing.

### Phase distortion and vector synthesis

Casio CZ: bend the phase ramp before the sine lookup (figure below). Vector synths crossfade four oscillators with a joystick.

**Fit: excellent.** Almost free per voice.

### Granular synthesis

Cut a recording into tiny windowed grains and play them overlapped, with independent control of position, pitch and density. Textures, freezes, time-stretching.

**Fit: medium.** Each grain costs an interpolated read; sample memory is the limit (520 KB SRAM, or flash streaming).

### Convolution

Multiply the signal by a recorded impulse response: cabinets, spring tanks, real rooms.

**Fit: limited.** Direct convolution of a short IR (a guitar cabinet of a few hundred taps) is feasible with the M33's dual-multiply instructions; a long room needs partitioned FFT convolution, which will take most of a core.

### Vocoder, formants and spectral processing

Split a voice into 10 to 20 bands, measure each band's level, and impose it on a carrier; or shape a spectrum with formant filters for choir sounds.

**Fit: good.** A 16-band filter bank is a few hundred cycles per sample.

### Full virtual-analog modelling

Zero-delay-feedback filters, component-level circuit models (wave digital filters), oscillator drift and component tolerance, oversampled saturation. The approach behind Monark-style Minimoog emulations.

**Fit: partial.** The ladder already has the tuning and saturation; oversampling and TPT are the next steps.

Karplus-Strong in four panels: noise fills the delay line, the averaging filter removes high harmonics faster than low ones, and the result is a decaying plucke

Karplus-Strong in four panels: noise fills the delay line, the averaging filter removes high harmonics faster than low ones, and the result is a decaying plucked string.

Modal synthesis: five inharmonic resonances, each with its own decay, make a bell.

Modal synthesis: five inharmonic resonances, each with its own decay, make a bell.

Phase distortion: bending the phase ramp (left) turns one cosine into a family of waves (middle) with a controllable harmonic content (right).

Phase distortion: bending the phase ramp (left) turns one cosine into a family of waves (middle) with a controllable harmonic content (right).

Granular: overlapped windowed grains taken from a source recording.

Granular: overlapped windowed grains taken from a source recording.

## Missing instruments, even within the methods we have

- **Electric pianos and clavinets with a physical flavour** (the piano bank can already hold Rhodes and clavinet samples; modal tines would be a different approach).
- **Supersaw / unison leads:** seven detuned saws per voice; PolyBLEP makes it practical, the cost is per saw.
- **Sample-based drums:** an 808 is synthesized here; kits of recorded hits need the sampling machinery of chapter 7 with short samples.
- **Modulation matrix and patching:** a reusable table of sources (LFOs, envelopes, velocity) and destinations, which is the difference between a synth with a fixed layout and Reaktor-style freedom.
- **Arpeggiator, chord memory and a shared step sequencer** on the melodic instruments; the drum and bass sequencers exist but are instrument-specific.
- **Effects:** a stereo delay with tape-style modulated feedback, phaser, flanger, bit-crusher, a compressor, a 3-band EQ.
- **Multi-timbral operation:** the tower runs one instrument at a time. Two or three light engines (a bass, a pad and a drum kit) on one Pico 2 would be a useful stretch, with the CPU governor sharing the budget.
- **Voices and noises:** choir (formants), speech, sound effects.

## A suggested order

1. **Karplus-Strong string instrument** (a few hours; the quickest thing that sounds new).
2. **Modal mallets and electric piano.**
3. **Wavetable synth** reusing the synth's filter, envelopes and effects.
4. **Shared modulation matrix + arpeggiator** back-ported to the existing synths.
5. **Vocoder / formant** using the filter bank approach.
6. **Granular sampler** with samples in flash.

# Reading list and glossary

Where to dig deeper.

## Reading

- J. Chowning, *The Synthesis of Complex Audio Spectra by Means of Frequency Modulation*, JAES 1973.
- K. Karplus and A. Strong, *Digital Synthesis of Plucked-String and Drum Timbres*, Computer Music Journal 1983.
- M. Schroeder, *Natural Sounding Artificial Reverberation*, JAES 1962.
- T. Stilson and J. Smith, *Alias-free digital synthesis of classic analog waveforms*, ICMC 1996. V. Välimäki and A. Huovilainen on antialiased oscillators and the nonlinear ladder (2004 to 2007).
- V. Zavalishin, *The Art of VA Filter Design* (free online): zero-delay-feedback filters.
- J. O. Smith, *Physical Audio Signal Processing* (free online): waveguides, modes, delay lines.
- C. Roads, *Microsound*: granular synthesis. W. Pirkle, *Designing Software Synthesizer Plug-Ins in C++*.
- The open-source **msfa** (Music Synthesizer for Android), the source of the DX7 algorithm layout used here.

## Glossary

Aliasing

Frequencies above half the sample rate folding back as false lower ones.

ADPCM

Adaptive differential pulse-code modulation: store small differences with an adaptive step.

BLEP

Band-limited step: a correction that smooths a waveform's jump to avoid aliasing.

Carrier / modulator

In FM: the operator you hear / the one that shifts another's phase.

Control rate

The slower rate at which envelopes and LFOs are updated, interpolated in between.

Footage

Organ pitch names, 8′ = the written pitch, 4′ an octave higher.

Ladder filter

Four cascaded one-pole low-pass stages with global feedback (Moog).

PIO

The RP2350's programmable I/O blocks; here they generate I²S and the USB host signals.

Phase accumulator

A counter whose value is the oscillator's position within a cycle.

SPSC ring

A single-producer, single-consumer queue that needs no lock.

TPT / ZDF

Topology-preserving transform / zero-delay feedback: a way to solve a filter's feedback loop exactly inside one sample.

Velocity layer

A separate recording for a range of key-press speeds.

All numbers marked as measured come from the picomusic project's own documentation and tests. PC timings are relative, not Pico figures. Descriptions of commercial instruments are general and from public knowledge.