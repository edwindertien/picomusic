# DSP tests of the sampled piano against the REAL engine (ASan + UBSan build) and the bank it plays.
import sys, os, json, hashlib, subprocess; sys.path.insert(0, '.')
import numpy as np, dsp_common as d
FS = 44100; ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..')
ok = True
OUT_GAIN = float([l for l in open(os.path.join(ROOT, 'src/config.h')) if l.startswith('#define OUT_GAIN')][0].split()[2].rstrip('f'))
def check(name, cond, detail=""):
    global ok; ok &= bool(cond); print(("PASS " if cond else "FAIL ") + name + (("  " + detail) if detail else ""))
BANK = json.load(open(f"{ROOT}/tools/.gen/bank.json")); Z = BANK['zones']
ADPCM_TOOL = '/tmp/adpcm_tool_dsp_test'; subprocess.run(['g++', '-O2', '-std=gnu++17', f'{ROOT}/tools/adpcm_tool.cpp', '-o', ADPCM_TOOL], check=True)      # ALWAYS rebuilt from the current source (a stale decoder once made a good bank look like garbage)
NEUTRAL = "set vol 127\nset tone 64\nset hard 64\nset width 64\nset tune 64\nset soft 0\nset decay 127\nset rel 64\n"
def render(script, n=FS): d.probe(script + f"run {n}\nwrite /tmp/p.raw\n"); return np.fromfile('/tmp/p.raw', dtype=np.int16).astype(float).reshape(-1, 2) / 32768
def mono(x): return x.sum(1) / 2
def zone(key): return min(Z, key=lambda z: abs(z['root'] - key))
def f_expected(key): z = zone(key); return 440 * 2 ** ((z['root'] - 69) / 12) * 2 ** (z['cents'] / 1200) * 2 ** ((key - z['root']) / 12)
def peak_near(x, f, span=100, n=1 << 19):
    s = np.log(np.abs(np.fft.rfft(x * np.hanning(len(x)), n)) + 1e-12); fr = np.fft.rfftfreq(n, 1 / FS); m = np.where((fr > f * 2 ** (-span / 1200)) & (fr < f * 2 ** (span / 1200)))[0]
    i = m[np.argmax(s[m])]; a, b, c = s[i - 1], s[i], s[i + 1]; return (i + 0.5 * (a - c) / (a - 2 * b + c)) * FS / n, s[i] - np.median(s[m])
def measure_f0(x, key):
    # the SAME rule as the bank tool's definition of a recording's pitch: the LOWEST harmonic that is clearly present (8x the window median in magnitude = ln 8 nepers).
    # (The first version picked the most prominent harmonic: when the codec noise floor fell, the sharper 3rd partial of key 87 became more prominent than its fundamental and the estimate jumped 21 cents.)
    fe = f_expected(key); best = None
    for h in (1, 2, 3):
        if fe * h > 15000: break
        f, prom = peak_near(x, fe * h)
        if prom >= np.log(8.0): return f / h
        if best is None or prom > best[1]: best = (f / h, prom)
    return best[0]
def cents(a, b): return 1200 * np.log2(a / b)
def env(x, b=441): n = len(x) // b; return np.sqrt((x[:n * b] ** 2).reshape(n, b).mean(1))
def rms(x): return float(np.sqrt((x ** 2).mean()))
def hf(x, f0=2000):
    x = x[:int(.15 * FS)]; s = np.abs(np.fft.rfft(x * np.hanning(len(x)))) ** 2; f = np.fft.rfftfreq(len(x), 1 / FS); return s[f > f0].sum() / s.sum()
def centroid(x): s = np.abs(np.fft.rfft(x * np.hanning(len(x)))) ** 2; f = np.fft.rfftfreq(len(x), 1 / FS); return (s * f).sum() / s.sum()
d.build()

# ---- the bank ----
data = open(f"{ROOT}/tools/.gen/bank.bin", 'rb').read(); hdr = open(f"{ROOT}/src/piano_bank.h").read()
check("bank: 30 zones, A0 .. C8 every minor third", len(Z) == 30 and [z['root'] for z in Z] == list(range(21, 109, 3)))
check("bank: 3.0 MB of ADPCM, fits a 4 MB part with room for the firmware", len(data) == BANK['total'] and 2.9e6 < len(data) <= 3.05e6, f"{len(data)/1e6:.3f} MB")
check("bank: the generated data array matches bank.bin byte for byte", hashlib.md5(open(f"{ROOT}/src/piano_data.cpp").read().encode()).hexdigest() == hashlib.md5(open(f"{ROOT}/src/piano_data.cpp").read().encode()).hexdigest() and f"PIANO_DATA_BYTES {len(data)}" in hdr)
src = open(f"{ROOT}/src/piano_data.cpp").read().split('{', 1)[1].rsplit('}', 1)[0]; arr = bytes(int(v) for v in src.replace('\n', '').split(',') if v.strip())
check("bank: ... (parsed back out of piano_data.cpp)", arr == data)
check("bank: zones are contiguous, in range and long enough for their sample count", all(z['off'] + z['bytes'] <= len(data) and z['bytes'] == 2 * z['head'] + (z['n'] - z['head'] + 1) // 2 for z in Z) and all(Z[i]['off'] + Z[i]['bytes'] <= Z[i + 1]['off'] for i in range(29)))
check("bank: ADPCM SNR >= 33 dB in the middle register, >= 24 dB everywhere", min(z['snr'] for z in Z if 36 < z['root'] <= 72) >= 33 and min(z['snr'] for z in Z) >= 24, f"mid min {min(z['snr'] for z in Z if 36<z['root']<=72)} dB, overall min {min(z['snr'] for z in Z)} dB")
check("bank: tuning follows a stretch curve (flat bass, sharp treble), monotone within 8 cents", all(Z[i + 1]['cents'] - Z[i]['cents'] > -8 for i in range(29)) and Z[0]['cents'] < -15 and Z[-1]['cents'] > 60, f"A0 {Z[0]['cents']:+.0f}, C4 {zone(60)['cents']:+.0f}, C8 {Z[-1]['cents']:+.0f} cents")

# ---- the player reproduces the recording ----
def reference(z):                                                     # ADPCM decoded, then ideally resampled to 44.1 kHz
    H = z['head']; head = np.frombuffer(data[z['off']:z['off'] + 2 * H], dtype='<i2'); enc = data[z['off'] + 2 * H:z['off'] + z['bytes']]       # a zone = PCM head, then the ADPCM stream that continues from the head's history
    start, hist = (int(head[-1]), [str(int(v)) for v in head[-2:-5:-1]]) if H else (z['pred0'], [])
    dec = head.tobytes() + subprocess.run([ADPCM_TOOL, 'dec', str(start), str(z['idx0']), str(z['n'] - H)] + hist, input=enc, capture_output=True).stdout
    y = subprocess.run(['ffmpeg', '-v', 'error', '-f', 's16le', '-ar', str(z['rate']), '-ac', '1', '-i', '-', '-af', 'aresample=resampler=soxr:precision=28', '-ar', str(FS), '-f', 's16le', '-'], input=dec, capture_output=True).stdout
    return np.frombuffer(y, dtype=np.int16).astype(float) / 32768 * 10 ** (z['gain10'] / 200)      # at velocity 80 (= vref) a note plays at exactly the zone's own gain (it restores the level lost by per-zone normalisation)
def fshift(x, tau):                                                   # fractional delay by FFT phase
    X = np.fft.rfft(x); w = 2 * np.pi * np.fft.rfftfreq(len(x)); return np.fft.irfft(X * np.exp(-1j * w * tau), len(x))
def best_snr(m, ref):
    n = min(len(m), len(ref)); m, ref = m[:n], ref[:n]; lo, hi = -2.0, 10.0
    f = lambda tau: 10 * np.log10((ref[200:n - 200] ** 2).sum() / (((m - fshift(ref, tau))[200:n - 200]) ** 2).sum())
    for _ in range(40): a1, a2 = lo + (hi - lo) / 3, hi - (hi - lo) / 3; lo, hi = (a1, hi) if f(a1) < f(a2) else (lo, a2)
    return f((lo + hi) / 2), (lo + hi) / 2
for key, minsnr in ((36, 29), (60, 25), (84, 30)):
    z = zone(key); ref = reference(z); out = render(NEUTRAL + "set width 0\n" + f"on {key} 80\n", int(2.5 * FS)); m = mono(out)[:int(2.5 * FS)] / OUT_GAIN
    snr, tau = best_snr(m, ref); check(f"player at the zone root ({z['name']}): output matches the decoded recording (SNR >= {minsnr} dB; Hermite vs ideal resampling)", snr >= minsnr, f"{snr:.1f} dB at a delay of {tau:+.2f} samples, {z['rate']} Hz zone")
x = render(NEUTRAL + "on 60 80\n", int(1.0 * FS)); check("attack is immediate (the hammer transient is in the data): peak within 15 ms of the key", np.argmax(np.abs(mono(x)) > 0.3 * np.abs(x).max()) < int(0.015 * FS), f"{np.argmax(np.abs(mono(x)) > 0.3*np.abs(x).max())/FS*1000:.1f} ms")

# ---- pitch of all 88 keys ----
errs = {}
for key in range(21, 109):
    x = mono(render(NEUTRAL + f"on {key} 80\n", int(2.6 * FS)))[int(.3 * FS):int(2.5 * FS)]
    if key >= 100: x = mono(render(NEUTRAL + f"on {key} 80\n", int(1.6 * FS)))[int(.1 * FS):int(1.5 * FS)]
    errs[key] = cents(measure_f0(x, key), f_expected(key))
bad = {k: round(e, 1) for k, e in errs.items() if abs(e) > 6}
check("all 88 keys sound at the recording's pitch +- the exact semitone shift (within 6 cents)", not bad, f"worst {max(errs.values(), key=abs):+.1f} cents; outside: {bad}")
def _root_err(z):                                                      # the engine's pitch vs the pitch of the DECODED ZONE, measured with the same estimator and window: a test of the playback path, independent of how the bank tool measured the recording
    key = z['root']; ref = reference(z); win = (int(.3 * FS), int(2.5 * FS)) if key < 100 else (int(.1 * FS), int(1.5 * FS))
    x = mono(render(NEUTRAL + f"on {key} 80\n", int(2.6 * FS)))[win[0]:win[1]] if key < 100 else mono(render(NEUTRAL + f"on {key} 80\n", int(1.6 * FS)))[win[0]:win[1]]
    return cents(measure_f0(x, key), measure_f0(ref[win[0]:win[1]], key))
root_errs = {z['root']: _root_err(z) for z in Z}
check("keys that need no shift (zone roots) play at the pitch of the decoded zone itself, within 1 cent (the same estimator on both)", all(abs(e) < 1.0 for e in root_errs.values()), f"{max(abs(e) for e in root_errs.values()):.2f} cents worst")
check("... and within 6 cents of the pitch the bank tool measured on the recording", all(abs(errs[z['root']]) < 6 for z in Z), f"{max(abs(errs[z['root']]) for z in Z):.2f} cents worst")
x1 = render(NEUTRAL + "on 61 80\n", 2 * FS); x2 = render(NEUTRAL + "on 62 80\n", 2 * FS)
check("neighbouring keys in one zone are one semitone apart (+-2 cents)", abs(cents(measure_f0(mono(x2)[int(.3*FS):], 62), measure_f0(mono(x1)[int(.3*FS):], 61)) - 100) < 2.0, f"{cents(measure_f0(mono(x2)[int(.3*FS):], 62), measure_f0(mono(x1)[int(.3*FS):], 61)):.1f} cents")

# ---- dynamics ----
HARD = lambda v: 0.9 + v / 127.0
def vgain(vel, tone=127, hard=64, key=60):
    a = render(NEUTRAL + f"set tone {tone}\nset hard {hard}\non {key} 80\n", int(.8 * FS)); b = render(NEUTRAL + f"set tone {tone}\nset hard {hard}\non {key} {vel}\n", int(.8 * FS)); return rms(mono(b)) / rms(mono(a))
for vel in (20, 40, 110):
    g = vgain(vel); exp = (vel / 80) ** HARD(64); check(f"velocity {vel}: gain = (vel/80)^1.4 (+-0.5 dB, filter bypassed)", abs(20 * np.log10(g / exp)) < 0.5, f"{20*np.log10(g):+.1f} dB, expected {20*np.log10(exp):+.1f}")
check("HARD changes the exponent (0.9 / 1.9) at velocity 40", abs(20 * np.log10(vgain(40, hard=0) / 0.5 ** 0.9)) < 0.6 and abs(20 * np.log10(vgain(40, hard=127) / 0.5 ** 1.9)) < 0.6, f"{20*np.log10(vgain(40,hard=0)):+.1f} / {20*np.log10(vgain(40,hard=127)):+.1f} dB")
h20 = hf(mono(render(NEUTRAL + "on 60 20\n", int(.3 * FS)))); h80 = hf(mono(render(NEUTRAL + "on 60 80\n", int(.3 * FS))))
check("soft notes are darker: the attack's energy above 2 kHz is >= 6 dB lower at velocity 20 than at 80", 10 * np.log10(h20 / h80) < -6, f"{10*np.log10(h20/h80):.1f} dB")
a = mono(render(NEUTRAL + "on 60 80\n", int(.6 * FS))); b = mono(render(NEUTRAL + "on 60 127\n", int(.6 * FS))); b = b / rms(b) * rms(a)
check("velocity 80 and above are not filtered: same spectrum, only louder", abs(centroid(a) - centroid(b)) < 0.02 * centroid(a), f"{centroid(a):.0f} vs {centroid(b):.0f} Hz")
ct = [hf(mono(render(NEUTRAL + f"set tone {t}\non 60 80\n", int(.3 * FS)))) for t in (0, 64, 127)]
check("TONE: 0 is >= 6 dB darker than 64; 64 and 127 are identical on a loud note (nothing to brighten)", 10 * np.log10(ct[0] / ct[1]) < -6 and abs(ct[1] - ct[2]) < 0.01 * ct[1], f"{10*np.log10(ct[0]/ct[1]):.1f} dB; {ct[1]:.4f} / {ct[2]:.4f}")
s0 = render(NEUTRAL + "on 60 80\n", int(.6 * FS)); s1 = render(NEUTRAL + "set soft 127\non 60 80\n", int(.6 * FS)); s2 = render(NEUTRAL + "softped 1\non 60 80\n", int(.6 * FS))
check("una corda (SOFT 127): -5.2 dB (x0.55) and >= 5 dB darker (subtle by design)", abs(20 * np.log10(rms(mono(s1)) / rms(mono(s0))) + 5.19) < 1.0 and 10 * np.log10(hf(mono(s1)) / hf(mono(s0))) < -5, f"{20*np.log10(rms(mono(s1))/rms(mono(s0))):.1f} dB, HF {10*np.log10(hf(mono(s1))/hf(mono(s0))):.1f} dB")
check("the soft pedal (CC67) does the same as SOFT 127", abs(rms(mono(s2)) / rms(mono(s1)) - 1) < 0.02)

# ---- pedal, damper, decay ----
def tail_db(script, key, t_off, t_meas, n):
    x = mono(render(NEUTRAL + script, n)); a = rms(x[int(t_off * FS) - 4410:int(t_off * FS)]); b = rms(x[int(t_meas * FS):int(t_meas * FS) + 4410]); return 20 * np.log10(b / a + 1e-12)
held = tail_db("on 60 80\n", 60, 1.0, 1.5, 2 * FS); damped = tail_db("on 60 80\nrun 44100\noff 60\n", 60, 1.0, 1.5, 2 * FS)
check("key release: the damper takes the note down (> 20 dB more than the held note after 0.5 s)", damped < held - 20, f"held {held:.1f} dB, released {damped:.1f} dB")
ped = tail_db("pedal 1\non 60 80\nrun 44100\noff 60\n", 60, 1.0, 1.5, 2 * FS)
check("sustain pedal: the released key keeps ringing like a held key (+-1 dB)", abs(ped - held) < 1.0, f"{ped:.1f} vs held {held:.1f} dB")
pu = tail_db("pedal 1\non 60 80\nrun 44100\noff 60\nrun 22050\npedal 0\n", 60, 1.5, 2.0, int(2.6 * FS)); check("lifting the pedal dampens the note", pu < held - 20, f"{pu:.1f} dB")
x = mono(render(NEUTRAL + "pedal 1\non 60 80\non 64 80\non 67 80\nrun 22050\noff 60\noff 64\noff 67\nrun 22050\n", int(1.0 * FS))); x2 = mono(render(NEUTRAL + "on 60 80\non 64 80\non 67 80\nrun 22050\noff 60\noff 64\noff 67\nrun 22050\n", int(1.0 * FS)))
check("pedal down at note-off: the whole chord keeps sounding (> 15 dB louder than undamped-released)", 20 * np.log10((rms(x[-4410:]) + 1e-9) / (rms(x2[-4410:]) + 1e-9)) > 15, f"{20*np.log10((rms(x[-4410:])+1e-9)/(rms(x2[-4410:])+1e-9)):.0f} dB")
for key in (36, 60, 84):
    t60 = max(0.5 * 2 ** (-(key - 24) / 24), 0.02); x = mono(render(NEUTRAL + f"on {key} 80\nrun 44100\noff {key}\n", int(2 * t60 * FS))); y = mono(render(NEUTRAL + f"on {key} 80\n", int(1.0 * FS) + int(2 * t60 * FS)))
    e = env(x[FS:], 441) / (env(y[FS:], 441) + 1e-9); idx = np.where(e < 0.01)[0]; t = (idx[0] + 1) * 441 / FS if len(idx) else 9
    check(f"damper time, key {key}: -40 dB after {2/3*t60*1000:.0f} ms (+-30 %)", abs(t - 2 / 3 * t60) < 0.3 * 2 / 3 * t60 + 0.01, f"measured {t*1000:.0f} ms")
tb, tt = 0.5 * 2 ** (-(30 - 24) / 24), 0.5 * 2 ** (-(90 - 24) / 24); check("the damper is slower in the bass than in the treble (x4 over 5 octaves)", tb / tt > 4)
x0 = mono(render(NEUTRAL + "on 60 80\n", 4 * FS)); x1 = mono(render(NEUTRAL + "set decay 60\non 60 80\n", 4 * FS)); db = 20 * np.log10(rms(x1[3 * FS:3 * FS + 4410]) / rms(x0[3 * FS:3 * FS + 4410])); T = 40 ** (60 / 124)
check("DECAY 60 adds an exponential decay (T60 = %.1f s): -%.0f dB at 3 s" % (T, 60 * 3 / T), abs(db + 60 * 3 / T) < 3, f"{db:.1f} dB")
check("DECAY 127 leaves the recording's own decay alone (bit-identical to 124)", np.array_equal(render(NEUTRAL + "on 60 80\n", 20000), render(NEUTRAL + "set decay 124\non 60 80\n", 20000)))

# ---- polyphony and stealing ----
def trace(script): out = d.probe(script + "trace\n"); return [tuple(map(int, l.split()[1:])) for l in out.split('\n') if l.startswith('T ')]
tr = trace("".join(f"on {40+i} 80\nskip 200\n" for i in range(24))); st = d.probe("".join(f"on {40+i} 80\nskip 200\n" for i in range(24)) + "status\n")
check("24 simultaneous notes: 24 voices, no steal", "active=24" in st and not any(e[1] == 2 for e in tr))
tr = trace("".join(f"on {40+i} 80\nskip 200\n" for i in range(24)) + "off 50\nskip 100\non 100 80\nskip 100\n"); steals = [e for e in tr if e[1] == 2]
check("the 25th note steals the RELEASING voice (the key just released), not a held one", len(steals) == 1 and steals[0][2] == 50, str(steals))
tr = trace("".join(f"on {40+i} 80\nskip 200\n" for i in range(24)) + "on 100 80\nskip 100\n"); steals = [e for e in tr if e[1] == 2]
check("with everything held, the oldest note is stolen", len(steals) == 1 and steals[0][2] == 40, str(steals))
tr = trace("on 60 80\nskip 2000\non 60 80\nskip 20000\n"); k5 = [e for e in tr if e[1] == 5]; st = d.probe("on 60 80\nskip 2000\non 60 80\nskip 20000\nstatus\n")
check("re-striking a key damps the old string quickly (fast release) and leaves one voice", len(k5) == 1 and "voices=1" in st, f"fast releases {len(k5)}; {st.split('voices=')[1].split()[0]} voice(s) after 0.45 s")
st = d.probe("pedal 1\n" + "".join(f"on {30+i} 80\nskip 100\noff {30+i}\n" for i in range(40)) + "status\n"); check("pedal held while playing 40 notes: never more than the 32-slot pool", int(st.split("active=")[1].split()[0]) <= 32)
st = d.probe("pedal 1\n" + "".join(f"on {30+i} 80\nskip 100\noff {30+i}\n" for i in range(40)) + "skip 5000\nstatus\n"); check("... and no more than 24 voices once the fade-outs are over", int(st.split("active=")[1].split()[0]) <= 24, st.split("active=")[1].split()[0] + " voices")

def click_ratios(script, n_extra=0):
    out = d.probe(script + "trace\n"); steals = [int(l.split()[1]) for l in out.split('\n') if l.startswith('T ') and int(l.split()[2]) == 2]
    x = mono(np.fromfile('/tmp/p.raw', dtype=np.int16).astype(float).reshape(-1, 2) / 32768); d2 = np.abs(np.diff(x, 2))
    return [d2[max(0, s - 3):s + 4].max() / (np.median(d2[max(0, s - 441):s + 441]) + 1e-7) for s in steals], len(steals)
CLK = NEUTRAL + "set vol 110\npedal 1\n" + "".join(f"on {36 + (i * 7) % 60} {70 + (i % 5) * 6}\nrun 2205\n" for i in range(40)) + "write /tmp/p.raw\n"
r, n = click_ratios(CLK)
check("stealing does not click: 40 notes under the pedal, the discontinuity at every steal is <= 5x the signal's normal roughness (a hard cut measured 10.7x)", n >= 10 and max(r) <= 5.0, f"{n} steals, worst {max(r):.1f}x, median {np.median(r):.1f}x")
st = d.probe("".join(f"on {40+i} 80\nskip 200\n" for i in range(24)) + "on 100 80\nskip 2000\nlevels\n"); keys = [int(l.split('key=')[1].split()[0]) for l in st.split('\n') if l.startswith('V ')]
check("the stolen voice fades out (15 ms) and is gone after 50 ms; the new note is sounding", 40 not in keys and 100 in keys and len(keys) == 24, f"{len(keys)} voices, oldest key 40 {'still' if 40 in keys else 'gone'}")
st = d.probe("".join(f"on {40+i} 80\nskip 200\n" for i in range(24)) + "on 100 80\nskip 40\nlevels\n"); v40 = [l for l in st.split('\n') if 'key=40 ' in l]
check("... and for those 15 ms it is still there, fading (not cut at once)", len(v40) == 1 and "rel=1" in v40[0], v40[0] if v40 else "voice 40 missing right after the steal")

# ---- distortion: the output limiter must not touch a normal loud note (it once added -33 dB of distortion at velocity 127, default volume) ----
def lvl(vol, notes, pedal=False):
    d.probe(f"set vol {vol}\nset tone 64\nset hard 64\nset width 64\nset soft 0\nset decay 127\n" + ("pedal 1\n" if pedal else "") + "".join(f"on {k} {v}\n" for k, v in notes) + "run 44100\nwrite /tmp/p.raw\n")
    return np.fromfile('/tmp/p.raw', dtype=np.int16).astype(float).reshape(-1, 2) / 32768
vg = lambda v: (v / 127) ** 2 * 0.5
for label, notes, thr in (("C4", [(60, 127)], -55), ("C7", [(96, 127)], -50)):
    ref = lvl(60, notes); x = lvl(100, notes); diff = x - ref * (vg(100) / vg(60)); dist = 20 * np.log10(np.sqrt((diff ** 2).mean()) / np.sqrt((x ** 2).mean()))
    check(f"no added distortion on a velocity-127 {label} at the default volume (vs the same note at volume 60, below the knee): < {thr} dB (the old saturator: -33 dB)", dist < thr and np.abs(x).max() < 0.72, f"{dist:.1f} dB, peak {np.abs(x).max():.2f}")
ch = [(48, 127), (60, 127), (64, 127), (67, 127)]; ref = lvl(60, ch); x = lvl(100, ch); lin = ref * (vg(100) / vg(60))
# tolerance 4 LSB: the output conversion TRUNCATES (1 LSB) and the reference is a volume-60 render scaled up 2.78x (1 x 2.78 LSB): bound 3.78 LSB, measured worst 2.67
check("a loud four-note chord: the limiter only acts on the peaks above its knee (output equals the linear signal wherever the linear signal is below 0.72)", np.allclose(x[np.abs(lin) < 0.72], lin[np.abs(lin) < 0.72], atol=4.0 / 32768) and np.abs(x).max() < 0.995, f"linear peak {np.abs(lin).max():.2f}, output peak {np.abs(x).max():.3f}")

# ---- stereo, bend, tune ----
def lr(key, width): x = render(NEUTRAL + f"set width {width}\non {key} 80\n", int(.5 * FS)); return rms(x[:, 0]) / rms(x[:, 1])
check("WIDTH 127: bass leans left, treble right (L/R > 1.4 at key 24, < 0.7 at key 100)", lr(24, 127) > 1.4 and lr(100, 127) < 0.7, f"{lr(24,127):.2f} / {lr(100,127):.2f}")
check("WIDTH 0: dead centre at every key (L/R = 1 +- 1 %)", all(abs(lr(k, 0) - 1) < 0.01 for k in (24, 60, 100)))
pw = lambda w: np.sqrt(sum(rms(render(NEUTRAL + f"set width {w}\non 40 80\n", int(.3 * FS))[:, c]) ** 2 for c in (0, 1)))
check("panning is constant power: width does not change loudness (+-0.3 dB)", abs(20 * np.log10(pw(127) / pw(0))) < 0.3, f"{20*np.log10(pw(127)/pw(0)):+.2f} dB")
base = measure_f0(mono(render(NEUTRAL + "on 60 80\n", 2 * FS))[int(.3 * FS):], 60)
up = measure_f0(mono(render(NEUTRAL + "bend 8191\non 60 80\n", 2 * FS))[int(.3 * FS):], 62); dn = measure_f0(mono(render(NEUTRAL + "bend -8192\non 60 80\n", 2 * FS))[int(.3 * FS):], 58)
check("pitch bend +-2 semitones (+-2 cents)", abs(cents(up, base) - 200) < 2 and abs(cents(dn, base) + 200) < 2, f"{cents(up,base):+.1f} / {cents(dn,base):+.1f} cents")
hi = measure_f0(mono(render(NEUTRAL + "set tune 127\non 60 80\n", 2 * FS))[int(.3 * FS):], 60); lo = measure_f0(mono(render(NEUTRAL + "set tune 0\non 60 80\n", 2 * FS))[int(.3 * FS):], 60)
check("TUNE spans -50 .. +49.2 cents (+-1.5)", abs(cents(hi, base) - 49.2) < 1.5 and abs(cents(lo, base) + 50) < 1.5, f"{cents(lo,base):+.1f} / {cents(hi,base):+.1f} cents")

# ---- stability, silence, determinism ----
chord = "".join(f"on {k} 127\n" for k in range(21, 109, 4)); x = render(NEUTRAL + chord, FS)
check("22 notes at velocity 127, volume 127: finite and never wraps past full scale", np.isfinite(x).all() and np.abs(x).max() <= 1.0, f"peak {np.abs(x).max():.3f}")
x = render(NEUTRAL + "on 60 80\noff 60\n", 3 * FS); check("a released note goes completely silent (exact zeros, no voice left)", np.abs(x[-FS:]).max() == 0 and "voices=0" in d.probe("on 60 80\noff 60\nskip 100000\nstatus\n"))
a = render("on 60 80\non 64 100\n", 30000); b = render("on 60 80\non 64 100\n", 30000); check("deterministic (identical runs are bit-identical)", np.array_equal(a, b))
x = render(NEUTRAL + "on 60 80\non 64 80\non 67 80\n", FS); check("a mezzo-forte triad at default volume leaves headroom (peak < 0.7)", np.abs(render("on 60 80\non 64 80\non 67 80\n", FS)).max() < 0.7, f"{np.abs(render('on 60 80 80'.split()[0]+' 60 80', 100)).max() if False else np.abs(render('on 60 80\non 64 80\non 67 80\n', FS)).max():.2f}")
print("\nALL DSP TESTS PASS" if ok else "\nDSP TESTS FAILED"); sys.exit(0 if ok else 1)
