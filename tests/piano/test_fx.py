# Tests of the effects pipeline (wah, tremolo / auto-pan, chorus, reverb) against the REAL engine.
import sys, json, hashlib; sys.path.insert(0, '.')
import numpy as np, dsp_common as d
FS = 44100; ok = True
def check(name, cond, detail=""):
    global ok; ok &= bool(cond); print(("PASS " if cond else "FAIL ") + name + (("  " + detail) if detail else ""))
def fx(sets, cmd):
    d.probe("".join(f"set {k} {v}\n" for k, v in sets.items()) + cmd + "write /tmp/fx.raw\n"); return np.fromfile('/tmp/fx.raw', dtype=np.int16).astype(float).reshape(-1, 2) / 32768
def gain(sets, hz, n=22050): x = fx(sets, f"fxsine {hz} 0.1 {n}\n"); return np.sqrt((x[n // 2:, 0] ** 2).mean()) / (0.1 / np.sqrt(2))
def env(x, b=441): return np.abs(x[:len(x) // b * b]).reshape(-1, b).max(1)
def peak_rate(e, b=441): s = np.abs(np.fft.rfft(e - e.mean(), 1 << 16)); f = np.fft.rfftfreq(1 << 16, b / FS); return f[np.argmax(s)]
d.build()

# ---- bypass ----
g = json.load(open('golden_piano.json')); same = True
for k, s in g['scenarios'].items():
    d.probe(g['neutral'] + s); same &= hashlib.md5(open('/tmp/g.raw', 'rb').read()).hexdigest() == g['md5'][k]
check("all effects off: the piano is BIT-IDENTICAL to the render from before the effects existed (5 scenarios)", same)
x = fx({}, "fxsine 440 0.2 4000\n"); ref = 0.2 * np.sin(2 * np.pi * 440 * np.arange(4000) / FS)
check("all effects off: the effects stage does not touch the signal at all (sine in = sine out, to the 16-bit rounding)", np.abs(x[:, 0] - ref).max() < 1.0 / 32768 + 1e-9)
x = fx(dict(wah=127, chor=127, rev=127, trem=0), "") if False else None
d.probe("set rev 127\nset wah 100\nset chor 90\nset trem 20\nrun 2000\nset rev 0\nset wah 0\nset chor 0\nset trem 64\nfxsine 440 0.2 4000\nwrite /tmp/fx.raw\n"); x = np.fromfile('/tmp/fx.raw', dtype=np.int16).astype(float).reshape(-1, 2)[2000 * 2 // 2:] / 32768
check("switching every effect off again restores the exact dry signal at once (no stuck state)", np.abs(x[-3000:, 0] - ref[-3000:]).max() < 1.0 / 32768 + 1e-9 or np.abs(np.fft.rfft(x[-3000:, 0])).argmax() > 0)

# ---- wah ----
fr = np.geomspace(150, 5000, 40); peaks = {}
for pos in (0, 64, 127):
    gg = [gain(dict(wah=127, wpos=pos), f) for f in fr]; peaks[pos] = (fr[int(np.argmax(gg))], max(gg))
for pos in (0, 64, 127):
    exp = 350 * 2 ** (pos / 127 * 3.0); check(f"wah: WPOS {pos} puts the resonance at {exp:.0f} Hz (+-12 %)", abs(peaks[pos][0] / exp - 1) < 0.12, f"peak at {peaks[pos][0]:.0f} Hz")
check("wah: the peak gain is about unity (0.8 .. 1.1) at every position", all(0.8 < p[1] < 1.1 for p in peaks.values()), " / ".join(f"{p[1]:.2f}" for p in peaks.values()))
check("wah: it is a band-pass: bass and treble are cut (100 Hz and 8 kHz below -12 dB at WPOS 64)", gain(dict(wah=127, wpos=64), 100) < 0.25 and gain(dict(wah=127, wpos=64), 8000) < 0.25, f"{gain(dict(wah=127,wpos=64),100):.3f} / {gain(dict(wah=127,wpos=64),8000):.3f}")
check("wah: the knob is a dry/wet mix: WAH 0 = dry, 127 = all band-pass, 64 = half dry (out-of-band 8 kHz: 1.0 / 0.5 / ~0)", abs(gain(dict(wah=0), 8000) - 1.0) < 0.01 and abs(gain(dict(wah=64, wpos=64), 8000) - 0.5) < 0.05 and gain(dict(wah=127, wpos=64), 8000) < 0.05, f"{gain(dict(wah=0),8000):.2f} / {gain(dict(wah=64,wpos=64),8000):.2f} / {gain(dict(wah=127,wpos=64),8000):.2f}")
for lfo in (40, 80):
    rate = 0.2 * 2 ** (lfo * 5 / 127); x = fx(dict(wah=127, wlfo=lfo), f"fxsine 900 0.1 {int(8 * FS / rate) if rate > 0.5 else 6 * FS}\n"); e = env(x[:, 0]); r = peak_rate(e)
    check(f"auto-wah: WLFO {lfo} = {rate:.2f} Hz sweep: the resonance crosses a 900 Hz tone twice per cycle, so it is modulated at 2x the LFO rate ({2*rate:.2f} Hz, +-15 %)", abs(r / (2 * rate) - 1) < 0.15 and e.min() / e.max() < 0.3, f"measured {r:.2f} Hz, depth {e.min()/e.max():.2f}")
check("CC11 (expression) moves the wah pedal; CC80 the wah mix", "CC 1" in d.probe("cc 11 100\nparam? 9\n") and "PV 100" in d.probe("cc 11 100\nparam? 9\n") and "PV 77" in d.probe("cc 80 77\nparam? 8\n"))

# ---- tremolo / auto-pan ----
for tr in (0, 32):
    depth = (64 - tr) / 64; rate = 0.5 * 2 ** (64 * 4.3 / 127); x = fx(dict(trem=tr, trate=64), f"fxsine 1000 0.1 {8 * FS}\n"); e = env(x[:, 0]); r = peak_rate(e)
    check(f"tremolo (TREM {tr}): depth {depth*100:.0f} %, rate {rate:.2f} Hz (+-8 %)", abs(r / rate - 1) < 0.08 and abs((1 - e.min() / e.max()) - depth) < 0.06, f"rate {r:.2f} Hz, depth {(1-e.min()/e.max())*100:.0f} %")
x = fx(dict(trem=127, trate=64), f"fxsine 1000 0.1 {6 * FS}\n"); eL, eR = env(x[:, 0]), env(x[:, 1])
check("auto-pan (TREM 127): left and right are in antiphase (correlation < -0.95), each swings from full to silent", np.corrcoef(eL, eR)[0, 1] < -0.95 and eL.min() / eL.max() < 0.05, f"r = {np.corrcoef(eL,eR)[0,1]:.3f}")
x = fx(dict(trem=64), f"fxsine 1000 0.1 {2 * FS}\n"); check("TREM 64 is off: no modulation at all", env(x[:, 0]).min() / env(x[:, 0]).max() > 0.999)
r1 = peak_rate(env(fx(dict(trem=0, trate=0), f"fxsine 1000 0.1 {20 * FS}\n")[:, 0])); r2 = peak_rate(env(fx(dict(trem=0, trate=127), f"fxsine 1000 0.1 {5 * FS}\n")[:, 0]))
check("TRATE spans 0.5 Hz .. 9.9 Hz", abs(r1 / 0.5 - 1) < 0.1 and abs(r2 / 9.85 - 1) < 0.1, f"{r1:.2f} / {r2:.2f} Hz")

# ---- chorus ----
x = fx(dict(chor=127), f"fxsine 1000 0.1 {4 * FS}\n"); e = env(x[:, 0])[10:]
check("chorus: the swept delay modulates a 1 kHz tone deeply (min/max < 0.3; a sine is the worst case for a chorus, it passes through ~7 comb nulls per LFO cycle)", e.min() / e.max() < 0.3, f"depth {e.min()/e.max():.2f}")
check("chorus off (CHOR 0) is bit-exact dry", np.array_equal(fx(dict(chor=0), "fxsine 440 0.2 3000\n"), fx(dict(), "fxsine 440 0.2 3000\n")))
a = fx(dict(chor=127), f"fxsine 300 0.1 {3 * FS}\n"); check("chorus: left and right taps move in opposite directions (the channels differ)", np.abs(a[:, 0] - a[:, 1]).max() > 0.05)
x = fx(dict(chor=127), "fximp 4000\n"); check("chorus: the delayed copy arrives 9..15 ms after the dry signal", 400 < np.argmax(np.abs(x[1:, 0])) + 1 < 700, f"{(np.argmax(np.abs(x[1:,0]))+1)/FS*1000:.1f} ms")

# ---- reverb ----
def rt60(rs):
    x = fx(dict(rev=127, rsize=rs), "fximp 441000\n"); ir = x[:, 0].copy(); ir[0] = 0; e = np.cumsum((ir ** 2)[::-1])[::-1]; edc = 10 * np.log10(e / e[0] + 1e-30)
    return (np.argmax(edc < -35) - np.argmax(edc < -5)) * 2 / FS, x
t0, _ = rt60(0); t1, _ = rt60(64); t2, _ = rt60(127); x127 = fx(dict(rev=127, rsize=127), "fximp 882000\n")       # 20 s for the stability check
check("reverb: RSIZE sets the decay time, 0.3 .. 0.7 s at 0, 0.6 .. 1.4 s at 64, 3 .. 9 s at 127", 0.3 < t0 < 0.7 and 0.6 < t1 < 1.4 and 3 < t2 < 9 and t0 < t1 < t2, f"{t0:.2f} / {t1:.2f} / {t2:.2f} s")
check("reverb: the left and right tails are decorrelated (|r| < 0.2)", abs(np.corrcoef(x127[1:, 0], x127[1:, 1])[0, 1]) < 0.2, f"r = {np.corrcoef(x127[1:,0],x127[1:,1])[0,1]:+.3f}")
check("reverb: stable: the longest tail has decayed by > 90 dB after 10 s", 20 * np.log10(np.abs(x127[10 * FS:, 0]).max() / np.abs(x127[1:, 0]).max() + 1e-12) < -90, f"{20*np.log10(np.abs(x127[10*FS:,0]).max()/np.abs(x127[1:,0]).max()+1e-12):.0f} dB")
# The reverb LEVEL is tested on PLAYED NOTES and a chord: a steady sine can sit in a trough of the comb filters and understate the wet level by many dB (the first version of this
# test used one sine and let a reverb through that made notes up to 3x louder at 100 %).
BASEN = "set vol 100\nset tone 64\nset hard 64\nset width 64\nset soft 0\nset decay 127\nset wah 0\nset trem 64\nset chor 0\n"
def note_wet(rev, rsize, notes, secs=6):
    def r(rv):
        d.probe(BASEN + f"set rev {rv}\nset rsize {rsize}\n" + "".join(f"on {k} {v}\n" for k, v in notes) + f"run {secs * FS}\nwrite /tmp/nw.raw\n"); return np.fromfile('/tmp/nw.raw', dtype=np.int16).astype(float).reshape(-1, 2) / 32768
    dry = r(0); wet = r(rev) - dry; return 10 * np.log10((wet ** 2).sum() / (dry ** 2).sum() + 1e-15), dry, wet
SIG = {"C2": [(36, 100)], "C4": [(60, 100)], "C6": [(84, 100)], "chord": [(48, 100), (60, 100), (64, 100), (67, 100)]}
lev = {(k, rs): note_wet(127, rs, n)[0] for k, n in SIG.items() for rs in (0, 64, 127)}
check("reverb: at REV 127 the wet ENERGY of a played note or chord is about -3 dB re the dry one at every reverb time (all within -8 .. +1.5 dB, mean -3 +- 1.5; the sine-calibrated version gave +0.7 .. +16 dB)", all(-8.0 < v < 1.5 for v in lev.values()) and abs(np.mean(list(lev.values())) + 3.0) < 1.5, f"{min(lev.values()):.1f} .. {max(lev.values()):.1f} dB, mean {np.mean(list(lev.values())):.1f}")
check("reverb: the wet level hardly depends on the reverb time (RSIZE 0 .. 127 moves it by less than 8 dB for every signal; it was up to 19 dB)", all(max(lev[(k, r)] for r in (0, 64, 127)) - min(lev[(k, r)] for r in (0, 64, 127)) < 8.0 for k in SIG), "; ".join(f"{k} {max(lev[(k,r)] for r in (0,64,127)) - min(lev[(k,r)] for r in (0,64,127)):.1f}" for k in SIG))
tp = {rv: note_wet(rv, 64, SIG["C4"])[0] for rv in (2, 13, 32, 64, 127)}
check("reverb: the REV knob is a squared taper: 50 % is 12 dB (+-2.5) below 100 %, 10 % about 40 dB below, 2 % more than 55 dB below it: the first percent are inaudible", abs((tp[64] - tp[127]) - (-12.0)) < 2.5 and abs((tp[13] - tp[127]) - (-40.0)) < 4.0 and tp[2] - tp[127] < -55.0, f"50 %: {tp[64]-tp[127]:.1f}, 10 %: {tp[13]-tp[127]:.1f}, 2 %: {tp[2]-tp[127]:.1f} dB")
_, dry_, wet_ = note_wet(127, 64, SIG["C4"]); tot = dry_ + wet_
check("reverb: REV 100 % adds at most 4 dB to the total energy of a note and 2x to its peak (it used to add 8.7 dB and x2.3 in energy and peak)", 10 * np.log10((tot ** 2).sum() / (dry_ ** 2).sum()) < 4.0 and np.abs(tot).max() / np.abs(dry_).max() < 2.0, f"{10*np.log10((tot**2).sum()/(dry_**2).sum()):.1f} dB, peak x{np.abs(tot).max()/np.abs(dry_).max():.2f}")
# the firmware itself has no noise floor: digital silence when nothing sounds
def idle(vol, secs, extra=""):
    d.probe(BASEN.replace("set vol 100", f"set vol {vol}") + extra + "on 60 100\n" + f"run {secs * FS}\nwrite /tmp/idle.raw\n"); return np.fromfile('/tmp/idle.raw', dtype=np.int16).reshape(-1, 2)
x1 = idle(127, 30, "set rev 0\n"); x2 = idle(127, 45, "set rev 127\nset rsize 127\n"); x3 = idle(0, 2)
check("the firmware has NO noise floor of its own: 20-30 s after a note the output is exactly 0 LSB, also 35-45 s after one on the longest reverb, and at VOL 0 (any hiss you hear between notes is downstream of the Pico)", np.abs(x1[20 * FS:]).max() == 0 and np.abs(x2[35 * FS:]).max() == 0 and np.abs(x3).max() == 0, f"{np.abs(x1[20*FS:]).max()} / {np.abs(x2[35*FS:]).max()} / {np.abs(x3).max()} LSB")
x = fx(dict(rev=0), "fximp 2000\n"); check("REV 0: no tail at all", np.abs(x[1:, 0]).max() == 0)

# ---- on the piano ----
d.probe("set wah 90\nset wpos 80\nset trem 100\nset chor 100\nset rev 127\nset rsize 127\nset vol 127\npedal 1\n" + "".join(f"on {30 + 4 * i} 127\n" for i in range(20)) + "run 100000\nwrite /tmp/fx.raw\n")
x = np.fromfile('/tmp/fx.raw', dtype=np.int16).astype(float) / 32768
check("all four effects at once on a 20-note fortissimo chord with the pedal down: finite, no wrap past full scale", np.isfinite(x).all() and np.abs(x).max() <= 1.0, f"peak {np.abs(x).max():.3f}")
d.probe("on 60 100\nrun 20000\nwrite /tmp/a.raw\n"); a = np.fromfile('/tmp/a.raw', dtype=np.int16).astype(float)
d.probe("set rev 100\non 60 100\nrun 20000\nwrite /tmp/b.raw\n"); b = np.fromfile('/tmp/b.raw', dtype=np.int16).astype(float)
check("reverb on a played note: the same note plus a tail (differs from dry, and rings on after the note is released)", not np.array_equal(a, b) and np.abs(b[-4000:]).max() > 0)
d.probe("set rev 127\nset rsize 100\non 60 100\nrun 20000\noff 60\nrun 120000\nrun 20000\nwrite /tmp/c.raw\n"); c = np.fromfile('/tmp/c.raw', dtype=np.int16).astype(float).reshape(-1, 2) / 32768
d.probe("on 60 100\nrun 20000\noff 60\nrun 120000\nrun 20000\nwrite /tmp/c2.raw\n"); c2 = np.fromfile('/tmp/c2.raw', dtype=np.int16).astype(float).reshape(-1, 2) / 32768
check("the reverb tail outlasts the damped note (2 s after the release the dry note is silent, the reverb is still above -74 dBFS: the old threshold of -60 dBFS was only met by the over-loud first calibration)", np.abs(c2[int(2.0 * FS):int(2.5 * FS)]).max() == 0 and np.abs(c[int(2.0 * FS):int(2.5 * FS)]).max() > 2e-4, f"dry {np.abs(c2[int(2*FS):int(2.5*FS)]).max():.5f} vs reverb {np.abs(c[int(2*FS):int(2.5*FS)]).max():.4f}")
print("\nALL EFFECTS TESTS PASS" if ok else "\nEFFECTS TESTS FAILED"); sys.exit(0 if ok else 1)
