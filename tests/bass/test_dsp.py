# DSP tests of the bass voice against the REAL engine (ASan + UBSan build).
import sys; sys.path.insert(0, '.')
import numpy as np, dsp_common as d
FS = 44100
ok = True
def check(name, cond, detail=""):
    global ok; ok &= bool(cond); print(("PASS " if cond else "FAIL ") + name + (("  " + detail) if detail else ""))
# a clean test voice: one saw oscillator, filter wide open, no modulation, full sustain
BASE = "".join(f"set {k} {v}\n" for k, v in dict(mix1=127, mix2=0, mix3=0, sub=0, noise=0, wav1=10, oct1=40, cut=127, res=0, drive=0, key=0, env=0, vel=0, acc=0, glide=0, vol=127,
        fa=0, fd=127, fs=127, fr=40, aa=0, ad=127, **{'as': 127}, ar=40, det2=64, det3=64).items())
def audio(script, n=FS):
    d.probe(script + f"run {n}\nwrite /tmp/b.raw\n"); return np.fromfile('/tmp/b.raw', dtype=np.int16).astype(float) / 32768
def f0(x, lo, hi, n=1 << 19):
    s = np.log(np.abs(np.fft.rfft(x * np.hanning(len(x)), n)) + 1e-12); f = np.fft.rfftfreq(n, 1 / FS); m = np.where((f > lo) & (f < hi))[0]; i = m[np.argmax(s[m])]
    a, b, c = s[i - 1], s[i], s[i + 1]; return (i + 0.5 * (a - c) / (a - 2 * b + c)) * FS / n          # parabolic interpolation: ~0.01 bin
def hz(note): return 440 * 2 ** ((note - 69) / 12)
def cents(a, b): return 1200 * np.log2(a / b)
def harm(x, f, k):                                   # amplitude of the sinusoid at EXACTLY k*f (windowed DTFT: no scalloping loss)
    n = np.arange(len(x)); w = np.hanning(len(x)); return abs(np.sum(w * x * np.exp(-2j * np.pi * k * f * n / FS))) * 2 / np.sum(w)
def blockenv(x, b=128): n = len(x) // b; return np.abs(x[:n * b]).reshape(n, b).max(1)
d.build()

# ---- pitch ----
worst = 0
for note in (28, 36, 48, 60, 72, 84):
    x = audio(BASE + f"on {note} 100\n", 2 * FS)[FS // 2:]; e = cents(f0(x, hz(note) * .8, hz(note) * 1.2), hz(note)); worst = max(worst, abs(e))
check("oscillator pitch within 1 cent, E1 .. C6", worst < 1.0, f"worst {worst:.2f} cents")
x = audio(BASE + "on 36 100\nbend 8191\n", 2 * FS)[FS:]; e = cents(f0(x, 70, 90), hz(38))
check("pitch bend +full = +2 semitones", abs(e) < 2, f"{e:+.2f} cents from D2")
x = audio(BASE + "on 36 100\nbend -8192\n", 2 * FS)[FS:]; e = cents(f0(x, 50, 70), hz(34)); check("pitch bend -full = -2 semitones", abs(e) < 2, f"{e:+.2f} cents from A#1")
a = f0(audio(BASE + "on 36 100\n", 2 * FS)[FS:], 50, 80); b = f0(audio(BASE + "set oct1 72\non 36 100\n", 2 * FS)[FS:], 100, 160)
check("OCT knob: +1 octave", abs(cents(b, a) - 1200) < 2, f"{cents(b,a)-1200:+.2f} cents")
a = f0(audio(BASE + "set mix1 0\nset mix2 127\nset det2 127\non 36 100\n", 2 * FS)[FS:], 50, 90)
check("DET2 127 = +50 cents", abs(cents(a, hz(36)) - 49.2) < 1.5, f"{cents(a,hz(36)):+.1f} cents")
a = f0(audio(BASE + "set mix1 0\nset sub 127\non 48 100\n", 2 * FS)[FS:], 50, 90)
check("sub oscillator sounds one octave below osc1", abs(cents(a, hz(36))) < 3, f"{cents(a,hz(36)):+.1f} cents from C2 (note played C3)")

# ---- waveforms ----
# Raw oscillators are measured exactly. Through the voice the LADDER's saturation reshapes a full-level wave (that is the Moog character,
# measured below), so the voice's waveforms are checked at a low input level where the ladder is linear.
def rawharm(wav, note, ks):
    d.probe(f"rawosc {wav} {note} 0 {2*FS} /tmp/o.f32\n"); x = np.fromfile('/tmp/o.f32', dtype=np.float32)[FS:].astype(float); a = [harm(x, hz(note), k) for k in ks]; return [20 * np.log10(v / a[0] + 1e-12) for v in a]
r = rawharm(0, 36, (1, 2, 3)); check("raw saw: harmonics 2, 3 at -6.0 / -9.5 dB (1/n), +-0.3 dB", abs(r[1] + 6.02) < .3 and abs(r[2] + 9.54) < .3, f"{r[1]:.2f} / {r[2]:.2f} dB")
r = rawharm(1, 36, (1, 2, 3, 5)); check("raw square: even harmonics absent (< -60 dB), 3rd -9.5, 5th -14.0 dB", r[1] < -60 and abs(r[2] + 9.54) < .3 and abs(r[3] + 13.98) < .3, f"{r[1]:.0f} / {r[2]:.2f} / {r[3]:.2f} dB")
r = rawharm(2, 36, (1, 2, 3, 5)); check("raw triangle: even harmonics absent, 3rd -19.1, 5th -28.0 dB (1/n^2)", r[1] < -60 and abs(r[2] + 19.08) < .3 and abs(r[3] + 27.96) < .3, f"{r[1]:.0f} / {r[2]:.2f} / {r[3]:.2f} dB")
f = hz(36); LOW = "set mix1 30\nset vol 127\n"; h = lambda x, k: harm(x, f, k)
xs = audio(BASE + LOW + "on 36 100\n", 2 * FS)[FS:]; check("voice saw at low level: 2nd -6.0 dB, 3rd -9.5 dB (ladder linear)", abs(20 * np.log10(h(xs, 2) / h(xs, 1)) + 6.02) < .3 and abs(20 * np.log10(h(xs, 3) / h(xs, 1)) + 9.54) < .3, f"{20*np.log10(h(xs,2)/h(xs,1)):.2f} / {20*np.log10(h(xs,3)/h(xs,1)):.2f} dB")
xt = audio(BASE + LOW + "set wav1 110\non 36 100\n", 2 * FS)[FS:]; check("voice triangle at low level: 3rd -19.1 dB", abs(20 * np.log10(h(xt, 3) / h(xt, 1)) + 19.08) < .3, f"{20*np.log10(h(xt,3)/h(xt,1)):.2f} dB")
xt = audio(BASE + "set wav1 110\non 36 100\n", 2 * FS)[FS:]; sat = 20 * np.log10(h(xt, 3) / h(xt, 1)) + 19.08
check("at full level the ladder's saturation flattens the wave: triangle 3rd harmonic 2..5 dB below ideal (Moog character)", -5 < sat < -2, f"{sat:+.1f} dB re ideal")
def alias_raw(note, wav, naive):
    d.probe(f"rawosc {wav} {note} {naive} {2*FS} /tmp/o.f32\n"); x = np.fromfile('/tmp/o.f32', dtype=np.float32)[FS:].astype(float); fr0 = hz(note); n = len(x)
    s = np.abs(np.fft.rfft(x * np.hanning(n))) ** 2; fr = np.fft.rfftfreq(n, 1 / FS); k = np.round(fr / fr0); hm = (np.abs(fr - k * fr0) < fr0 * 0.04 + 3 * FS / n) & (k >= 1)
    return 10 * np.log10(s[~hm & (fr > 50)].sum() / s[hm].sum())
for note in (60, 72, 84):
    nb, pb = alias_raw(note, 0, 1), alias_raw(note, 0, 0)
    check(f"PolyBLEP saw beats a naive saw by >= 15 dB at note {note}", nb - pb > 15, f"naive {nb:.1f} dB, PolyBLEP {pb:.1f} dB ({nb-pb:.0f} dB better)")
nb, pb = alias_raw(72, 1, 1), alias_raw(72, 1, 0); check("PolyBLEP square beats a naive square by >= 15 dB at C5", nb - pb > 15, f"naive {nb:.1f}, PolyBLEP {pb:.1f} dB")
def alias_voice(note, wav):
    x = audio(BASE + f"set wav1 {wav}\non {note} 100\n", 2 * FS)[FS:]; fr0 = hz(note); n = len(x); s = np.abs(np.fft.rfft(x * np.hanning(n))) ** 2; fr = np.fft.rfftfreq(n, 1 / FS)
    k = np.round(fr / fr0); hm = (np.abs(fr - k * fr0) < fr0 * 0.04 + 3 * FS / n) & (k >= 1); return 10 * np.log10(s[~hm & (fr > 100)].sum() / s[hm].sum())
check("through the voice: non-harmonic energy < -38 dB at C2..C3 (the bass range)", max(alias_voice(36, 10), alias_voice(48, 10)) < -38, f"C2 {alias_voice(36,10):.1f}, C3 {alias_voice(48,10):.1f} dB")
check("... and < -28 dB even at C6", alias_voice(84, 10) < -28, f"{alias_voice(84,10):.1f} dB")

# ---- the ladder: exact mapping ----
def resp(fc, r, f): return float(d.probe(f"filtresp {fc} {r} {f}\n").split()[1])
ratios = {}
for fc in (100, 400, 1600):
    fr = np.geomspace(fc * .2, fc * 1.2, 90); g = np.array([resp(fc, 0.0, q) for q in fr]); ratios[fc] = fr[np.argmax(g < 1 / np.sqrt(2))] / fc
check("ladder -3 dB point = 0.435 x cutoff at low frequencies (4 equal poles)", abs(ratios[100] - 0.435) < 0.03, f"100 Hz: {ratios[100]:.3f}  400 Hz: {ratios[400]:.3f}  1600 Hz: {ratios[1600]:.3f}  (drifts up: the Euler-style stage is not an analog pole)")
check("... and stays within 0.40 .. 0.60 of it up to 1.6 kHz", all(0.40 < v < 0.60 for v in ratios.values()))
sl = 20 * np.log10(resp(200, 0, 1600) / resp(200, 0, 800)); check("ladder slope ~ 24 dB/oct a decade above the cutoff (22..26)", -26 < sl < -22, f"{sl:.1f} dB per octave (800 -> 1600 Hz, fc 200 Hz)")
lo = resp(1000, 0.0, 60); hi = resp(1000, 0.95, 60)
check("resonance steals bass like a Moog (>= 10 dB at r=0.95)", 20 * np.log10(lo / hi) > 10, f"{20*np.log10(lo/hi):.1f} dB")
check("resonance peak at the cutoff: r=0.95 gives > +12 dB at fc vs r=0", 20 * np.log10(resp(1000, 0.95, 1000) / resp(1000, 0.0, 1000)) > 12, f"{20*np.log10(resp(1000,0.95,1000)/resp(1000,0,1000)):.1f} dB")

# ---- self-oscillation in tune ----
OSC = BASE + "set mix1 0\nset noise 14\nset res 127\nset vol 110\n"
worst = 0; rows = []
for cut in (30, 50, 70, 90, 110):
    target = 20 * 2 ** (cut * 8.1 / 127); x = audio(OSC + f"set cut {cut}\non 36 100\n", 3 * FS)[FS:]; e = cents(f0(x, target * .6, target * 1.4), target); worst = max(worst, abs(e)); rows.append((round(target), round(e, 1), round(float(np.abs(x).max()), 2)))
check("RES 127 self-oscillates within 10 cents of the CUT frequency (35 Hz .. 3 kHz)", worst < 10, str(rows))
x = audio(OSC + "set cut 70\non 36 100\n", FS)[FS // 2:]; check("self-oscillation amplitude is a healthy tone, not a squeak", 0.05 < np.abs(x).max() < 0.9, f"peak {np.abs(x).max():.2f}")
dec = audio(BASE + "set mix1 0\nset noise 14\nset res 100\nset vol 110\nset cut 70\non 36 100\nrun 20000\nset noise 0\n", 2 * FS); decs = audio(OSC + "set cut 70\non 36 100\nrun 20000\nset noise 0\n", 2 * FS)
tail = lambda y: np.abs(y[int(1.2 * FS):]).max()
check("RES 100 does NOT self-oscillate (dies away once the noise stops)", tail(dec) < 0.002, f"tail peak {tail(dec):.5f}")
check("RES 127 keeps ringing with no input at all", tail(decs) > 0.02, f"tail peak {tail(decs):.3f}")
p = []
for note in (36, 48, 60):
    x = audio(OSC + f"set cut 40\nset key 127\non {note} 100\n", 3 * FS)[FS:]; p.append(f0(x, 20, 3000))
check("KEY 127: self-oscillation follows the keyboard, 1 octave per octave (+-10 cents)", abs(cents(p[1], p[0]) - 1200) < 10 and abs(cents(p[2], p[1]) - 1200) < 10, f"{p[0]:.1f} {p[1]:.1f} {p[2]:.1f} Hz")
p = [f0(audio(OSC + f"set cut 60\nset key 0\non {n} 100\n", 3 * FS)[FS:], 20, 3000) for n in (36, 60)]
check("KEY 0: cutoff stays put", abs(cents(p[1], p[0])) < 3, f"{p[0]:.1f} / {p[1]:.1f} Hz")
p = [f0(audio(OSC + f"set cut 60\nset key 64\non {n} 100\n", 3 * FS)[FS:], 20, 3000) for n in (36, 60)]
check("KEY 64: half tracking (2 octaves played = 1.008 octaves of cutoff)", abs(cents(p[1], p[0]) - 2400 * 64 / 127) < 15, f"{cents(p[1],p[0]):.0f} cents for 24 semitones (expected {2400*64/127:.0f})")

# ---- filter envelope, accent, velocity, drive ----
def bright(x): s = np.abs(np.fft.rfft(x * np.hanning(len(x)))) ** 2; fr = np.fft.rfftfreq(len(x), 1 / FS); return s[fr > 600].sum() / s.sum()
ENVT = BASE + "set cut 40\nset fd 60\nset fs 0\n"
b0 = bright(audio(ENVT + "set env 0\non 36 100\n", int(.06 * FS))); b1 = bright(audio(ENVT + "set env 100\non 36 100\n", int(.06 * FS)))
check("ENV opens the filter on the attack (brightness up >= 10x)", b1 > 10 * b0, f"{b0:.5f} -> {b1:.5f}")
a1 = bright(audio(ENVT + "set env 100\non 36 100\n", int(.06 * FS))); a2 = bright(audio(ENVT + "set env 100\non 36 100\nrun 20000\n", int(.06 * FS))[:])
late = audio(ENVT + "set env 100\non 36 100\nrun 30000\n", int(.06 * FS))
check("...and the filter closes again as the envelope decays", bright(late) < 0.3 * b1, f"{b1:.5f} -> {bright(late):.5f}")
ACC = ENVT + "set env 70\nset acc 127\nset vel 0\n"
pn = np.abs(audio(ACC + "on 36 100\n", int(.1 * FS))).max(); pa = np.abs(audio(ACC + "on 36 127\n", int(.1 * FS))).max()
bn = bright(audio(ACC + "on 36 100\n", int(.05 * FS))); ba = bright(audio(ACC + "on 36 127\n", int(.05 * FS)))
check("accent (velocity >= 112) is louder and brighter", pa > pn * 1.1 and ba > 1.5 * bn, f"peak x{pa/pn:.2f}, brightness x{ba/bn:.1f}")
VEL = BASE + "set vel 127\n"; q = np.abs(audio(VEL + "on 36 60\n", 8000)).max(); lo_ = np.abs(audio(VEL + "on 36 110\n", 8000)).max()
check("VEL 127: soft keys are much quieter (60 vs 110 = -5 dB or more)", 20 * np.log10(q / lo_) < -4, f"{20*np.log10(q/lo_):.1f} dB")
q = np.abs(audio(BASE + "on 36 60\n", 8000)).max(); lo_ = np.abs(audio(BASE + "on 36 110\n", 8000)).max()
check("VEL 0: velocity does nothing", abs(20 * np.log10(q / lo_)) < 0.3, f"{20*np.log10(q/lo_):.2f} dB")
def pk(script): return float(np.abs(audio(script, 20000)[4000:]).max())
lin = pk(BASE + "set vol 60\nset drive 0\nset mix1 127\non 36 100\n") / pk(BASE + "set vol 60\nset drive 0\nset mix1 50\non 36 100\n")
sat = pk(BASE + "set vol 60\nset drive 127\nset mix1 127\non 36 100\n") / pk(BASE + "set vol 60\nset drive 127\nset mix1 50\non 36 100\n")
check("DRIVE saturates the ladder input (level ratio 127:50 compresses from 6.4x to < 5x)", lin > 6.0 and sat < 0.8 * lin, f"drive 0: x{lin:.2f}   drive 127: x{sat:.2f}")

# ---- envelopes ----
def t_at(env, frac): return (np.argmax(env >= frac * env.max()) + 1) * 128 / FS
AT = BASE + "set ad 127\n"
def rms_env(x, win): return np.sqrt(np.convolve(x ** 2, np.ones(win) / win, 'same'))
for v in (40, 70):
    T = 0.001 * 2000 ** (v / 127); x = audio(AT + f"set aa {v}\non 72 100\n", int(3 * T * FS) + 4000); e = rms_env(x, 84); m = (np.argmax(e >= 0.5 * e[int(2 * T * FS):].max())) / FS
    check(f"amp attack {v}: 50 % point at T/2 (+-20 %)", abs(m - T / 2) < 0.2 * T / 2 + 0.001, f"measured {m*1000:.1f} ms, expected {T/2*1000:.1f} ms")
for v in (50, 80):
    T = 0.005 * 600 ** (v / 127); e = blockenv(audio(BASE + f"set as 0\nset ad {v}\non 36 100\n", int(1.6 * T * FS) + 4000)); pk = e.max(); idx = np.where(e > pk * 0.01)[0]; m = (idx[-1] + 1) * 128 / FS
    check(f"amp decay {v}: -40 dB after T (+-25 %)", abs(m - T) < 0.25 * T, f"measured {m*1000:.0f} ms, expected {T*1000:.0f} ms")
for v in (40, 70):
    T = 0.005 * 600 ** (v / 127); x = audio(BASE + f"set ar {v}\non 36 100\nrun 22050\noff 36\n", int(1.6 * T * FS) + 22050)[22050:]; e = blockenv(x); pk = e.max(); idx = np.where(e > pk * 0.01)[0]; m = (idx[-1] + 1) * 128 / FS
    check(f"amp release {v}: -40 dB after T (+-25 %)", abs(m - T) < 0.25 * T, f"measured {m*1000:.0f} ms, expected {T*1000:.0f} ms")

# ---- glide and legato ----
def pitches(script, steps, per=441):
    out = []
    for i in range(steps): s = d.probe(script + f"skip {per*(i+1)}\nstatus\n"); out.append(float(s.split("pitch=")[1].split()[0]))
    return np.array(out)
G = BASE + "set glide 80\n"; T = 0.01 * 300 ** (80 / 127)
traj = pitches(G + "on 36 100\nskip 3000\non 48 100\n", 40, int(T * FS / 8)); tt = (np.arange(40) + 1) * (T / 8)
tgt = 36 + 12 * (1 - 0.01 ** (tt / T)); check("glide: legato overlap glides exponentially (99 % in the set time)", np.abs(traj - tgt).max() < 0.25, f"max deviation {np.abs(traj-tgt).max():.2f} semitones, T = {T*1000:.0f} ms")
j = pitches(G + "on 36 100\nskip 3000\noff 36\nskip 20\non 48 100\n", 1, 40)
check("glide: a new note after a release jumps straight to the pitch", abs(j[0] - 48) < 0.01, f"{j[0]:.3f}")
check("glide 0 = off, even when notes overlap", abs(pitches(BASE + "on 36 100\nskip 3000\non 48 100\n", 1, 40)[0] - 48) < 0.01)
st = d.probe(G + "set aa 100\non 36 100\nskip 60000\non 48 100\nskip 400\nstatus\n"); al = float(st.split("alevel=")[1].split()[0]); stage = int(st.split("astage=")[1].split()[0])
check("legato note does NOT retrigger the envelope (stays in sustain at full level)", stage == 3 and al > 0.99, f"stage {stage}, level {al:.3f}")
st = d.probe(G + "set aa 100\nset as 80\non 36 100\nskip 60000\noff 36\nskip 40\non 48 100\nskip 100\nstatus\n"); stage = int(st.split("astage=")[1].split()[0])
check("a detached note retriggers the envelope (attack)", stage == 1, f"stage {stage}")

# ---- mono key priority, legato return, pedal ----
def kinds(script): out = d.probe(script + "trace\n"); return [tuple(map(int, l.split()[2:5])) for l in out.split('\n') if l.startswith('T ')]
ev = [tuple(map(int, l.split()[2:4])) for l in d.probe("on 36 100\nskip 100\non 40 100\nskip 100\non 43 100\nskip 100\noff 43\nskip 100\noff 40\nskip 100\noff 36\nskip 100\ntrace\n").split('\n') if l.startswith('T ')]
check("mono: last note wins; key releases walk back down the stack", ev == [(1, 36), (2, 40), (2, 43), (2, 40), (2, 36), (0, 36)], str(ev))
ev = [tuple(map(int, l.split()[2:4])) for l in d.probe("on 36 100\nskip 100\noff 36\nskip 100\non 40 100\nskip 100\n" + "trace\n").split('\n') if l.startswith('T ')]
check("detached notes retrigger (kind 1), the release is a gate-off (kind 0)", ev == [(1, 36), (0, 36), (1, 40)], str(ev))
ev = [tuple(map(int, l.split()[2:4])) for l in d.probe("pedal 1\non 36 100\nskip 100\noff 36\nskip 100\ntrace\n").split('\n') if l.startswith('T ')]
check("sustain pedal holds the note after the key is released", ev == [(1, 36)], str(ev))
ev = [tuple(map(int, l.split()[2:4])) for l in d.probe("pedal 1\non 36 100\nskip 100\noff 36\nskip 100\npedal 0\nskip 100\ntrace\n").split('\n') if l.startswith('T ')]
check("...and releasing the pedal ends it", ev == [(1, 36), (0, 36)], str(ev))

# ---- patch loudness (the same groove through every patch) ----
import calibrate_patches as cp
lv = [cp.level(p, int(d.probe(f"patch {p}\nparam? 14\n").split()[1])) for p in range(4)]
check("Moog Bass, Sub and Acid play the same groove at equal loudness (+-1 dB)", max(lv[i][0] for i in (0, 1, 2)) - min(lv[i][0] for i in (0, 1, 2)) < 1.0, " / ".join(f"{lv[i][0]:.1f}" for i in range(3)) + " dBFS RMS")
check("Pluck is within 4 dB of them (short notes: lower RMS at the same peak)", abs(lv[3][0] - lv[0][0]) < 4.0 and lv[3][1] > 0.5, f"{lv[3][0]:.1f} dBFS RMS, peak {lv[3][1]:.2f}")
check("no patch clips the groove (peak < 0.95)", all(l[1] < 0.95 for l in lv), " / ".join(f"{l[1]:.2f}" for l in lv))

# ---- stability, silence, determinism ----
EXT = "".join(f"set {k} 127\n" for k in ("mix1", "mix2", "mix3", "sub", "noise", "res", "drive", "cut", "env", "vol", "acc", "key")) + "set fs 127\nset as 127\n"
worst = 0; fin = True
for note in (24, 36, 60, 84, 96):
    x = audio(EXT + f"on {note} 127\n", FS); fin &= bool(np.isfinite(x).all()); worst = max(worst, np.abs(x).max())
check("every control at 127: finite, below full scale", fin and worst < 1.0, f"peak {worst:.3f}")
x = audio("on 36 100\noff 36\n", 3 * FS)
check("a released note goes completely silent (exact zeros, voice idle)", np.abs(x[-FS:]).max() == 0 and "active=0" in d.probe("on 36 100\noff 36\nskip %d\nstatus\n" % (3 * FS)), "")
a = audio("patch 2\non 36 100\n", 20000); b = audio("patch 2\non 36 100\n", 20000); check("deterministic (identical runs are bit-identical)", np.array_equal(a, b))
print("\nALL DSP TESTS PASS" if ok else "\nDSP TESTS FAILED"); sys.exit(0 if ok else 1)
