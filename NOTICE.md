# Sample attribution

The piano sounds in `src/piano_data.cpp` / `tools/.gen/bank.bin` are derived from the

**Salamander Grand Piano V3** (Yamaha C5, recorded at 48 kHz / 24 bit with two AKG C414 microphones)
by **Alexander Holm**, released under the **Creative Commons Attribution 3.0** licence (CC BY 3.0).

The recordings are the lossless FLAC files of one velocity layer (layer 10 of 16: velocities 73..80) from the sfzinstruments repository
(`github.com/sfzinstruments/SalamanderGrandPiano`, remapped to SFZ by kinwie, which includes the CC BY 3.0 licence text), fetched by `tools/fetch_salamander.sh`.
(Earlier versions of this project used a 38 kbit/s MP3 copy of the V2 recordings from the Tone.js audio repository; they were replaced because they contain almost nothing above 5 kHz.)

## What was changed (CC BY requires saying so)

Using `tools/bankconf` (config `banks/grand.yaml`), each of the 30 samples was: mixed to mono; trimmed to start 3 ms before the onset; cut to a length that fits the flash
budget (about 2 .. 15 s) with a raised-cosine fade-out; scaled to full scale (its level is restored by a gain at play-back); resampled to 16 / 24 / 32 kHz depending on the register;
and encoded as 4-bit ADPCM with an adaptive predictor, except for the first 9 ms (the hammer strike), which are kept as 16-bit PCM.
At playback the samples are pitch-shifted by up to one semitone, filtered, panned and enveloped. This is not the original recording and is not endorsed by its author.

The firmware source code in this project is separate from the samples; the samples keep their own licence.

## Other sample sets

The firmware plays any bank built with `tools/bankconf`; only the grand above is bundled. **Every instrument has to be credited and its licence respected by whoever redistributes a bank built from it** (the `credit` field of an instrument is shown on the LCD while it is selected). The sample sets looked at while building the tool (November 2026; check each licence yourself before relying on this list):

* **jRhodes3 / jRhodes3c** (Jeff Learman, via github.com/sfzinstruments): **CC BY-NC-SA 4.0**. A bank containing these samples is non-commercial and share-alike.
* **E-Pianos** (Greg Sullivan: Yamaha CP80, Wurlitzer EP200, Hohner Pianet T, via github.com/sfzinstruments): CC BY 3.0.
* **stevie-clavinet** (github.com/jlearman): the README states that its licence is unknown. Private use at your own risk; do not redistribute.
* **TwinCl** (github.com/thecowgoesmoo): MIT, a simulated clavinet-like instrument.

None of these is included in this repository. `tools/fetch_rhodes.sh` and `tools/fetch_clavinet.sh` download the Rhodes and clavinet from their GitHub repositories for **your own use**; do not redistribute those samples or a bank built from them.
