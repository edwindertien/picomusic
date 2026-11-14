# DSP tests of the drum voices, run against the REAL engine (ASan + UBSan build).
import sys; sys.path.insert(0, '.')
import numpy as np, dsp_common as d
from drum_names import NAMES
FS = 44100
BD, SD, LT, MT, HT, RS, CP, CB, CH, OH, CY, CL, MA, LC, MC, HC = range(16)
ok = True
def check(name, cond, detail=""):
    global ok; ok &= bool(cond); print(("PASS " if cond else "FAIL ") + name + (("  " + detail) if detail else ""))
def render(script, n=FS):
    d.probe(script + f"run {n}\nwrite /tmp/t.raw\n"); x = d.read_stereo('/tmp/t.raw') / 32768.0; return x
def mono(i, n=FS, vel=127, pre=""): x = render(pre + f"hit {i} {vel}\n", n); return (x[:, 0] + x[:, 1]) / 2
def blockenv(x, b=256): n = len(x) // b; return np.abs(x[:n*b]).reshape(n, b).max(1)
def t_to(x, db, b=256):
    e = blockenv(x, b); idx = np.where(e > e.max() * 10 ** (db / 20))[0]; return (idx[-1] + 1) * b / FS if len(idx) else 0
def peakfreq(x, lo=20, hi=6000, n=1 << 17):
    s = np.abs(np.fft.rfft(x * np.hanning(len(x)), n)); f = np.fft.rfftfreq(n, 1 / FS); m = (f > lo) & (f < hi); return f[m][np.argmax(s[m])]
def bandrms(x, lo, hi):
    s = np.abs(np.fft.rfft(x * np.hanning(len(x)))) ** 2; f = np.fft.rfftfreq(len(x), 1 / FS); return np.sqrt(s[(f >= lo) & (f < hi)].sum())
def centroid(x):
    s = np.abs(np.fft.rfft(x * np.hanning(len(x)))) ** 2; f = np.fft.rfftfreq(len(x), 1 / FS); return (s * f).sum() / s.sum()
d.build()

# ---- pitch and decay ----
x = mono(BD); f_end = peakfreq(x[int(.15*FS):int(.40*FS)], 20, 200)
check("BD final pitch 56 Hz (tune 64)", abs(f_end - 56) < 3, f"{f_end:.1f} Hz")
zc = np.where((x[:-1] <= 0) & (x[1:] > 0))[0]; f_start = FS / (zc[1] - zc[0]) if len(zc) > 1 else 0     # length of the first full cycle
check("BD pitch sweeps down (first cycle >= 1.8x final pitch)", f_start > 1.8 * f_end, f"first cycle {f_start:.0f} Hz, final {f_end:.0f} Hz")
tau = 0.06 * 12 ** (45 / 127); pred = tau * np.log(100)           # -40 dB of an exponential
check("BD -40 dB time matches the decay law", abs(t_to(x, -40) - pred) < 0.25 * pred, f"measured {t_to(x,-40):.3f} s, predicted {pred:.3f} s")
lo = peakfreq(mono(BD, pre="param 0 1 0\n")[int(.15*FS):int(.4*FS)], 20, 200); hi = peakfreq(mono(BD, pre="param 0 1 127\n")[int(.15*FS):int(.4*FS)], 20, 200)
check("BD tune spans 0.71x..1.41x", abs(lo / 56 - 0.71) < 0.05 and abs(hi / 56 - 1.41) < 0.07, f"{lo:.1f} .. {hi:.1f} Hz")
short = t_to(mono(BD, pre="param 0 2 0\n"), -40); long_ = t_to(mono(BD, pre="param 0 2 127\n", n=6*FS), -40)
check("BD decay knob spans ~0.28 s .. 3.3 s", 0.2 < short < 0.45 and 2.5 < long_ < 4.5, f"{short:.2f} .. {long_:.2f} s")
for i, base in ((LT, 88), (MT, 130), (HT, 190), (LC, 205), (MC, 273), (HC, 365), (CL, 2500)):
    f = peakfreq(mono(i)[int(.06*FS):int(.12*FS)] if i != CL else mono(i)[:int(.02*FS)], 50, 4000)
    check(f"{NAMES[i]} pitch {base} Hz", abs(f / base - 1) < 0.04, f"{f:.0f} Hz")
check("CL is much shorter than LT (-40 dB)", t_to(mono(CL), -40) < 0.12 and t_to(mono(LT), -40) > 0.4, f"CL {t_to(mono(CL),-40):.3f}  LT {t_to(mono(LT),-40):.3f}")

# ---- snare ----
x = mono(SD); fs_ = peakfreq(x[:int(.1*FS)], 100, 400)
check("SD tone at 185 Hz", abs(fs_ / 185 - 1) < 0.08, f"{fs_:.0f} Hz")
n0 = bandrms(mono(SD, pre="param 1 3 0\n")[:int(.3*FS)], 3000, 10000) / bandrms(mono(SD, pre="param 1 3 0\n")[:int(.3*FS)], 100, 400)
n1 = bandrms(mono(SD, pre="param 1 3 127\n")[:int(.3*FS)], 3000, 10000) / bandrms(mono(SD, pre="param 1 3 127\n")[:int(.3*FS)], 100, 400)
check("SD tone knob trades tone for noise (>= 10 dB)", 20 * np.log10(n1 / n0) > 10, f"{20*np.log10(n1/n0):.1f} dB")

# ---- metal ----
ch, oh, cy = mono(CH), mono(OH), mono(CY, n=3 * FS)
check("CH is bright (centroid 6..11 kHz)", 6000 < centroid(ch[:int(.1*FS)]) < 11000, f"{centroid(ch[:int(.1*FS)]):.0f} Hz")
check("CY centroid 5..10 kHz", 5000 < centroid(cy[:int(.5*FS)]) < 10000, f"{centroid(cy[:int(.5*FS)]):.0f} Hz")
check("OH rings >= 4x longer than CH (-30 dB)", t_to(oh, -30) >= 4 * t_to(ch, -30), f"CH {t_to(ch,-30):.3f}  OH {t_to(oh,-30):.3f}")
check("CY rings longer than OH", t_to(cy, -30) > 1.5 * t_to(oh, -30), f"{t_to(cy,-30):.2f} vs {t_to(oh,-30):.2f}")
free = render("hit 9 127\nrun 2205\nparam 8 0 0\nhit 8 127\n", FS)[:, 0]
open_ = render("hit 9 127\nrun 2205\n", FS)[:, 0]
tail = lambda y: np.sqrt(np.mean(y[int(.20*FS) + 2205:int(.40*FS) + 2205] ** 2)) + 1e-12
check("CH chokes OH (tail >= 30 dB down)", 20 * np.log10(tail(free) / tail(open_)) < -30, f"{20*np.log10(tail(free)/tail(open_)):.1f} dB")
a_ = render("hit 8 127\nrun 2205\n", FS)[:, 0]; b_ = render("hit 8 127\nrun 2205\nparam 9 0 0\nhit 9 127\n", FS)[:, 0]
check("OH does NOT choke CH (bit-identical CH tail)", np.array_equal(a_, b_))

# ---- clap, cowbell, maracas, rim ----
hits = 60; xs = render("".join(f"hit 6 127\nrun 22050\n" for _ in range(hits)), 0)[:, 0]
env = np.abs(xs.reshape(hits, 22050)).mean(0)[:int(.05*FS)]; env = np.convolve(env, np.ones(44) / 44, 'same')   # mean rectified envelope over 60 hits, 1 ms smoothing
pk = [i for i in range(100, len(env) - 100) if env[i] == env[i-100:i+100].max() and env[i] > 0.25 * env.max()]
check("CP has 4 bursts ~11 ms apart", len(pk) >= 3 and all(8.5 < (b - a) / FS * 1000 < 14 for a, b in zip(pk, pk[1:])), f"peaks at {[round(p/FS*1000,1) for p in pk]} ms")
xc = mono(CB)[:int(.4*FS)]; b = lambda hz: bandrms(xc, hz - 20, hz + 20)
check("CB shows 540 and 800 Hz partials", 20 * np.log10(b(540) / b(650)) > 6 and 20 * np.log10(b(800) / b(650)) > 6, f"{20*np.log10(b(540)/b(650)):.1f} / {20*np.log10(b(800)/b(650)):.1f} dB over the gap")
xm = mono(MA, n=int(.2*FS)); e = blockenv(xm, 44)
check("MA has an attack (peak after 1..8 ms, not at 0)", 1.0 < np.argmax(e) * 44 / FS * 1000 < 8.0, f"peak at {np.argmax(e)*44/FS*1000:.1f} ms")
check("MA is bright but band-limited (centroid 4..10 kHz)", 4000 < centroid(xm) < 10000, f"{centroid(xm):.0f} Hz")
xr = mono(RS)[:int(.03*FS)]
check("RS has partials near 470 and 1700 Hz", bandrms(xr, 1600, 1800) > 2 * bandrms(xr, 1000, 1200) and bandrms(xr, 400, 540) > 1.5 * bandrms(xr, 700, 900), "")

# ---- velocity, level, pan ----
bad = []
for i in range(16):
    pre = f"param {i} 0 40\n"; p127 = np.abs(mono(i, n=8820, vel=127, pre=pre)).max(); p64 = np.abs(mono(i, n=8820, vel=64, pre=pre)).max()   # LEVEL 40: far below the output limiter's knee
    if abs(p64 / p127 - 64 / 127) > 0.08: bad.append((NAMES[i], round(p64 / p127, 3)))
check("velocity scales amplitude linearly (all 16)", not bad, str(bad))
a40 = np.abs(mono(BD, n=8820, pre="param 0 0 40\n")).max(); a20 = np.abs(mono(BD, n=8820, pre="param 0 0 20\n")).max()
check("LEVEL is square-law (40 vs 20 = +12 dB)", abs(20 * np.log10(a40 / a20) - 12) < 0.5, f"{20*np.log10(a40/a20):.1f} dB")
pw = []
for pan in (0, 32, 64, 96, 127):
    x = render(f"param 0 0 40\nparam 0 4 {pan}\nhit 0 127\n", 8820); pw.append((pan, np.sqrt(x[:, 0] ** 2 + x[:, 1] ** 2).max(), np.abs(x[:, 0]).max(), np.abs(x[:, 1]).max()))
check("pan 0 is hard left, 127 is hard right", pw[0][3] < 0.01 * pw[0][2] and pw[4][2] < 0.01 * pw[4][3], f"R/L at 0 = {pw[0][3]/pw[0][2]:.4f}, L/R at 127 = {pw[4][2]/pw[4][3]:.4f}")
check("pan law is constant power (+-0.1 dB)", max(p[1] for p in pw) / min(p[1] for p in pw) < 1.012, f"{[round(p[1],3) for p in pw]}")
check("pan 64 is centred", abs(pw[2][2] - pw[2][3]) < 0.02 * pw[2][2])

# ---- whole-kit behaviour ----
x = render("".join(f"hit {i} 127\n" for i in range(16)), FS)
check("all 16 at full velocity: finite and below full scale", np.isfinite(x).all() and np.abs(x).max() < 0.999, f"peak {np.abs(x).max():.3f}")
x1 = render("hit 0 127\n", 4410)[:, 0]; x2 = render("hit 0 127\n", 4410)[:, 0]
check("deterministic (identical runs are bit-identical)", np.array_equal(x1, x2))
check("pad hit sounds within one control tick (16 samples)", np.argmax(np.abs(render("hit 0 127\n", 400)[:, 0]) > 0) <= 32)
out = d.probe("hit 8 127\nrun 20000\nhit 8 100\nrun 20000\npeek 8\n")
x = render("hit 9 127\nhit 10 127\nhit 12 127\nhit 6 127\n", 6 * FS)
tail = np.abs(x[-FS:]).max(); st = d.probe("hit 9 127\nhit 10 127\nhit 12 127\nhit 6 127\nrun %d\npeek 9\npeek 10\npeek 12\npeek 6\n" % (6 * FS))
check("voices release (live=0) after their tails", st.count("live=0") == 4 and tail == 0, st.replace("\n", " ").strip())
check("rapid re-trigger does not stick", "live=1" not in d.probe("hit 3 100\nrun 100\nhit 3 100\nrun 100\nhit 3 100\nrun %d\npeek 3\n" % (3 * FS)))
print("\nALL DSP TESTS PASS" if ok else "\nDSP TESTS FAILED"); sys.exit(0 if ok else 1)
