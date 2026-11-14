#!/usr/bin/env python3
"""Analyse a recording of the piano (from the Pico, or rendered by `python3 -m bankconf render`) for hiss that rides on the notes.
    python3 tools/noise_report.py piano.wav [--channel 0|1] [--plot out.png]
It reports, per note: the peak level, the level in the piano's own band (40 Hz - 4 kHz) and in the 'haze' band (8 - 16 kHz) shortly after the hit and a second later, and the
difference (the haze relative to the note). Noise that is part of the sample path scales with the note and decays with it (a constant difference); a hardware floor is
constant in absolute level and shows up between the notes. Compare the Pico's recording with a PC render of the same notes: if the PC render is clean, the difference is on the Pico."""
import sys, subprocess, argparse
import numpy as np

def load(path):
    raw = subprocess.run(["ffmpeg", "-v", "error", "-i", path, "-f", "f32le", "-"], capture_output=True).stdout
    info = subprocess.run(["ffprobe", "-v", "error", "-select_streams", "a:0", "-show_entries", "stream=sample_rate,channels", "-of", "csv=p=0", path], capture_output=True, text=True).stdout.strip().split(",")
    sr, ch = int(info[0]), int(info[1]); return np.frombuffer(raw, dtype=np.float32).reshape(-1, ch).astype(float), sr
def band(seg, sr, lo, hi):
    X = np.abs(np.fft.rfft(seg * np.hanning(len(seg)))) / len(seg) * 2; f = np.fft.rfftfreq(len(seg), 1 / sr); m = (f >= lo) & (f < hi); return 20 * np.log10(np.sqrt((X[m] ** 2).sum()) + 1e-12)
def f0(seg, sr):
    n = 1 << 17; X = np.abs(np.fft.rfft(seg * np.hanning(len(seg)), n)); f = np.fft.rfftfreq(n, 1 / sr); m = (f > 25) & (f < 2500); return f[m][int(np.argmax(X[m]))]

def main(argv=None):
    ap = argparse.ArgumentParser(); ap.add_argument("wav"); ap.add_argument("--channel", type=int, default=-1, help="channel to analyse (default: the louder one)"); ap.add_argument("--plot"); a = ap.parse_args(argv)
    x, sr = load(a.wav); ch = x.shape[1]; lv = [20 * np.log10(np.sqrt((x[:, c] ** 2).mean()) + 1e-12) for c in range(ch)]
    print(f"{a.wav}: {len(x) / sr:.1f} s, {sr} Hz, {ch} channel(s); RMS per channel (dBFS): " + ", ".join(f"{v:.1f}" for v in lv))
    c = a.channel if a.channel >= 0 else int(np.argmax(lv)); s = x[:, c]
    if ch == 2 and abs(lv[0] - lv[1]) > 20: print(f"  NOTE: the channels differ by {abs(lv[0] - lv[1]):.0f} dB. A piano note in the middle of the keyboard is almost centred, so the quiet channel was probably not connected or not recorded: it says nothing about the hardware floor.")
    hop = int(0.01 * sr); W = 2048; fl = np.array([band(s[i:i + W], sr, 8000, 16000) for i in range(0, len(s) - W, hop)]); d = fl[3:] - fl[:-3]; ons = []
    for i in np.where(d > 15)[0]:
        t = (i + 3) * 0.01
        if not ons or t - ons[-1] > 0.25: ons.append(t)
    if not ons: print("no note onsets found (the haze band never rose by 15 dB: either very clean, or no notes)"); return 0
    print(f"\n{len(ons)} notes. Haze band = 8-16 kHz, piano band = 40 Hz - 4 kHz; levels in dBFS, early = 100-300 ms after the hit, late = 1.0-1.5 s")
    print(" onset(s)  pitch(Hz)  peak   piano  haze   haze-piano  | late: piano  haze  haze-piano"); rows = []
    for k, t in enumerate(ons):
        end = ons[k + 1] if k + 1 < len(ons) else len(s) / sr; a1 = int((t + 0.1) * sr); b1 = a1 + int(0.2 * sr); a2 = int((t + 1.0) * sr); b2 = a2 + int(0.5 * sr)
        pk = 20 * np.log10(np.abs(s[int(t * sr):int(min(end, t + 0.5) * sr)]).max() + 1e-12); lf, hf = band(s[a1:b1], sr, 40, 4000), band(s[a1:b1], sr, 8000, 16000); fr = f0(s[int((t + 0.2) * sr):int((t + 0.6) * sr)], sr)
        late = f"{band(s[a2:b2], sr, 40, 4000):6.1f} {band(s[a2:b2], sr, 8000, 16000):6.1f} {band(s[a2:b2], sr, 8000, 16000) - band(s[a2:b2], sr, 40, 4000):7.1f}" if end - t > 1.6 else "  (next note too soon)"
        print(f" {t:7.2f} {fr:9.1f} {pk:6.1f} {lf:6.1f} {hf:6.1f} {hf - lf:9.1f}    | {late}"); rows.append((pk, hf - lf))
    r = np.array(rows); print(f"\nhaze relative to the piano band: mean {r[:, 1].mean():.1f} dB (std {r[:, 1].std():.1f}); for reference the current firmware rendered on a PC gives about -51 .. -58 dB on loud notes and -63 .. -69 dB on soft ones (dry)")
    quiet = np.array([band(s[int(t0 * sr):int(t0 * sr) + int(0.2 * sr)], sr, 40, 20000) for t0 in np.arange(0.05, len(s) / sr - 0.3, 0.1)]); print(f"quietest 200 ms of the whole file: {quiet.min():.1f} dBFS (a hardware hiss floor, if there is one, shows up here)")
    if a.plot:
        import matplotlib; matplotlib.use("Agg"); import matplotlib.pyplot as plt
        fig, ax = plt.subplots(figsize=(13, 5)); ax.specgram(s + 1e-9, NFFT=2048, Fs=sr, noverlap=1536, cmap="magma", vmin=-130, vmax=-20); ax.set_ylim(0, 16000); plt.tight_layout(); plt.savefig(a.plot, dpi=80); print("wrote", a.plot)
    return 0
if __name__ == "__main__": sys.exit(main())
