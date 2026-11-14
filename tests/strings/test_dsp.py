"""DSP tests for the string machine: drives the REAL engine through host_test/probe.cpp and measures the result."""
import sys, os, numpy as np
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from dsp_common import *
build(); fails = 0
def check(name, cond, detail=''):
    global fails
    if not cond: fails += 1
    print(f"  [{'ok' if cond else 'FAIL'}] {name} {detail}")
BASE = "patch 0\nset bass 0\nset cello 0\nset viola 0\nset violin 0\nset pizz 0\nset emix 0\nset tone 127\nset atk 0\nset det 0\nset vib 0\nset vol 127\n"

print("== PolyBLEP saw vs naive saw: inharmonic (alias) energy across the range ==")
N = 1 << 16
def alias_db(f0, naive):
    probe(f"rawsaw {f0} {N} {int(naive)} /tmp/s.f32\n"); x = np.fromfile('/tmp/s.f32', dtype=np.float32).astype(float)
    X = np.abs(np.fft.rfft(x * np.hanning(N))) ** 2; fr = np.fft.rfftfreq(N, 1 / FS)
    k = np.round(fr / f0); harm = np.abs(fr - k * f0) < 3 * FS / N * 2          # bins close to a harmonic of f0
    return 10 * np.log10(X[~harm].sum() / X[harm].sum())
worst_gain = 99
for f0, nm in ((261.63, 'C4'), (523.25, 'C5'), (1046.5, 'C6'), (2093.0, 'C7'), (4186.0, 'C8')):
    an, ab = alias_db(f0, True), alias_db(f0, False); worst_gain = min(worst_gain, an - ab)
    print(f"      {nm}: naive {an:6.1f} dB   PolyBLEP {ab:6.1f} dB   ({an - ab:4.1f} dB better)")
check("PolyBLEP is >= 15 dB cleaner than a naive saw at every pitch from C4 to C8", worst_gain >= 15)
check("PolyBLEP alias energy below -25 dB even at C8", ab < -25)

print("\n== registration filters: shape at their design frequencies ==")
def resp(n, hz): return float([l for l in probe(f"resp {n} {hz}\n").split('\n') if l.startswith('RESP')][0].split()[3])
check("bass   LP: ~unity at 60 Hz, -3 dB near 260 Hz, strong cut at 2 kHz", resp('bass', 60) > 0.95 and 0.6 < resp('bass', 260) < 0.8 and resp('bass', 2000) < 0.03, f"{resp('bass',60):.2f} / {resp('bass',260):.2f} / {resp('bass',2000):.3f}")
check("cello  LP: resonant peak (Q=1) at 720 Hz, cut at 4 kHz", 0.9 < resp('cello', 720) < 1.1 and resp('cello', 4000) < 0.05, f"{resp('cello',720):.2f} / {resp('cello',4000):.3f}")
for n, fc in (('viola', 1100), ('violin', 2400), ('pizz', 1600)):
    peak = max(resp(n, f) for f in np.geomspace(100, 8000, 40)); fpk = max(np.geomspace(100, 8000, 40), key=lambda f: resp(n, f))
    check(f"{n:6} BP: unity peak near {fc} Hz", 0.9 < resp(n, fc) < 1.05 and abs(fpk / fc - 1) < 0.15, f"gain at fc {resp(n,fc):.2f}, peak at {fpk:.0f} Hz")

print("\n== loudness balance: every registration equally loud at middle C (trims) ==")
rms = {}
for r in ('bass', 'cello', 'viola', 'violin'): rms[r] = sustained_rms(BASE + f"set {r} 127\n", 60)
rms['pizz'] = sustained_rms(BASE + "set pizz 127\nset pdec 127\n", 60, skip=2000, run=8000)
db = {k: 20 * np.log10(v / rms['cello']) for k, v in rms.items()}
print("      " + '  '.join(f"{k} {v:+.2f} dB" for k, v in db.items()))
check("all within 1 dB of each other at middle C", max(abs(v) for v in db.values()) < 1.0)

print("\n== ensemble ==")
probe(BASE + "set viola 100\nset cello 100\nset emix 0\non 55\non 62\nskip 30000\nrun 22050\nwrite /tmp/e0.raw\n"); e0 = read_stereo('/tmp/e0.raw')
check("mix 0: left == right exactly", np.array_equal(e0[:, 0], e0[:, 1]))
probe(BASE + "set viola 100\nset cello 100\nset emix 127\nset edepth 127\nset eshim 127\non 55\non 62\nskip 30000\nrun 88200\nwrite /tmp/e1.raw\n"); e1 = read_stereo('/tmp/e1.raw')
c = np.corrcoef(e1[:, 0], e1[:, 1])[0, 1]; lvl = 20 * np.log10(np.sqrt(np.mean(e1 ** 2)) / np.sqrt(np.mean(e0 ** 2)))
print(f"      full ensemble: L/R correlation {c:.3f}, level vs dry {lvl:+.2f} dB")
check("full ensemble decorrelates L and R", c < 0.97)
check("ensemble keeps the level within 2 dB", abs(lvl) < 2.0)
# tap delays: impulse train through the Ensemble alone; three taps 120 degrees apart => their LFO offsets sum to zero
n = 44100 * 12; probe(f"ensprobe 1 0 1 60 {n} /tmp/ens.f32\n"); y = np.fromfile('/tmp/ens.f32', dtype=np.float32).reshape(-1, 2).astype(float)
d0, d1, d2 = [], [], []
for k in range(0, n // 1000 - 2):
    seg = y[k * 1000 + 60: k * 1000 + 420]
    pl = np.argsort(seg[:, 0])[::-1]; a = pl[0]; b = [i for i in pl if abs(i - a) > 6][0]; r = np.argmax(seg[:, 1])
    d0.append(a + 60); d1.append(b + 60); d2.append(r + 60)
d0, d1, d2 = np.array(d0), np.array(d1), np.array(d2); D0 = ENS_D0 = 5.0e-3 * FS
print(f"      tap delays (samples): t0 {d0.min()}..{d0.max()}  t1 {d1.min()}..{d1.max()}  t2 {d2.min()}..{d2.max()}   (centre {D0:.0f}, slow sweep +-{2.4e-3*FS:.0f})")
check("taps sweep about the centre delay by the slow-LFO depth", abs(d0.mean() - D0) < 15 and (d0.max() - d0.min()) > 1.6 * 2.4e-3 * FS)
# t0 and t1 are the two L peaks: identify which is which by LFO phase relation (sum of deviations ~ 0 when ordered by phase)
allt = np.vstack([d0, d1, d2]); dev = allt - D0
check("no net delay drift: the three tap offsets cancel (120 degree spacing)", abs(np.mean(np.sort(dev, axis=0).sum(axis=0))) < 12, f"mean sum {np.mean(dev.sum(axis=0)):+.1f} samples")

print("\n== envelopes (mix 0, static filters) ==")
def envelope(script, total):
    probe(script); d = read_stereo('/tmp/env.raw')[:, 0]; w = 441
    return np.array([np.sqrt(np.mean(d[i:i + w] ** 2)) for i in range(0, len(d) - w, w)]), w / FS
env, dt = envelope(BASE + "set cello 127\nset atk 100\non 60\nrun 88200\nwrite /tmp/env.raw\n", 2)
final = env[-20:].mean(); t50 = np.argmax(env > 0.5 * final) * dt; exp_t = 0.707 * attackSeconds if False else None
import math
T = 0.004 * 600 ** (100 / 127); texp = 0.707 * T
print(f"      attack knob 100 = {T:.3f} s linear; amplitude (level squared) reaches 50% at {texp:.3f} s -> measured {t50:.3f} s")
check("attack timing within 15 %", abs(t50 / texp - 1) < 0.15)
env, dt = envelope(BASE + "set cello 127\nset atk 0\nset rel 90\non 60\nskip 22050\nrun 4410\noff 60\nrun 88200\nwrite /tmp/env.raw\n", 2)
i_off = 4410 // 441; tail = env[i_off:]; hold = env[:i_off].mean(); t50 = np.argmax(tail < 0.5 * hold) * dt
T = 0.02 * 300 ** (90 / 127); texp = (1 - 0.707) * T
print(f"      release knob 90 = {T:.3f} s; amplitude falls to 50% at {texp:.3f} s -> measured {t50:.3f} s; silent after {np.argmax(tail < 1) * dt:.2f} s")
check("release timing within 15 %", abs(t50 / texp - 1) < 0.15)
check("release reaches silence on time", np.argmax(tail < 1) * dt < T + 0.15)
env, dt = envelope(BASE + "set pizz 127\nset pdec 100\non 60\nrun 44100\nwrite /tmp/env.raw\n", 1)
tau = 0.05 * 20 ** (100 / 127); k0 = int(0.03 / dt); k1 = k0 + int(tau / dt)
print(f"      pizz decay knob 100: tau {tau:.3f} s; amplitude after one tau = {env[k1]/env[k0]:.3f} of the start (ideal 0.368)")
check("pizzicato decays exponentially with the set tau (+-20 %)", abs(env[k1] / env[k0] / 0.368 - 1) < 0.20)

print("\n== headroom and robustness ==")
def pk(script): probe(script + "write /tmp/h.raw\n"); d = read_stereo('/tmp/h.raw'); return abs(d).max() / 32767, d
chord6 = ''.join(f"on {n}\n" for n in (48, 55, 60, 64, 67, 72)); chord10 = ''.join(f"on {n}\n" for n in (36, 43, 48, 52, 55, 60, 64, 67, 72, 76))
for p, name in enumerate(("Ensemble", "Cello", "Violins")):
    a, _ = pk(f"patch {p}\n{chord6}skip 88200\nrun 44100\n"); b, _ = pk(f"patch {p}\n{chord10}skip 88200\nrun 44100\n")
    print(f"      {name:8} peak: 6 notes {a:.2f}, 10 notes {b:.2f}")
    check(f"{name}: 6-note chord leaves headroom (peak 0.15 .. 0.6 FS)", 0.15 < a < 0.6)
    check(f"{name}: 10-note chord below 0.85 FS", b < 0.85)
worst = "patch 0\nset bass 127\nset cello 127\nset viola 127\nset violin 127\nset pizz 127\nset vol 127\nset tone 127\nset det 127\nset vib 127\nset atk 0\n" + ''.join(f"on {n}\n" for n in range(24, 120, 4)) + "pedal 1\nskip 44100\nrun 44100\n"
a, d = pk(worst)
check("everything at maximum, 24 notes: finite and within 16 bit", np.isfinite(d).all() and a <= 1.0, f"peak {a:.2f} FS")
print(f"\nDSP TESTS: {'PASS' if not fails else 'FAIL'} ({fails} failures)"); sys.exit(1 if fails else 0)
