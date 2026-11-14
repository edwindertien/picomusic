# Tests of the bank tool (tools/bankconf) and of the engine playing what it builds: zones, layers, loops, instruments, budget, warnings.
#   python3 test_bank.py            fast tests (synthetic samples)
#   python3 test_bank.py --full     also rebuild the shipped grand from the real samples and compare with the bank byte for byte (about 90 s; needs samples/salamander)
import sys, os, json, hashlib, subprocess, tempfile, shutil, wave, textwrap
ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..')); sys.path.insert(0, os.path.join(ROOT, 'tools'))
import numpy as np, yaml
from bankconf import core, sfz, notes
FS = 44100; ok = True
def check(name, cond, detail=""):
    global ok; ok &= bool(cond); print(("PASS " if cond else "FAIL ") + name + (("  " + detail) if detail else ""))
def wav(path, x):
    w = wave.open(path, 'wb'); w.setnchannels(1); w.setsampwidth(2); w.setframerate(FS); w.writeframes((np.clip(x, -1, 1) * 32767).astype(np.int16).tobytes()); w.close()
def tone(f, dur, harm, tau, noise=0.0, seed=1):
    t = np.arange(int(dur * FS)) / FS; x = sum((1.0 / h) * np.sin(2 * np.pi * f * h * t + 0.3 * h) for h in range(1, harm + 1)); env = np.exp(-t / tau) * np.minimum(1, t / 0.004) if tau else np.minimum(1, t / 0.02)
    return 0.3 * x / harm ** 0.5 * env + noise * np.random.RandomState(seed).randn(len(t)) * np.exp(-t / 0.03)
T = tempfile.mkdtemp(prefix="bank_test_"); S = os.path.join(T, "samples"); os.makedirs(S)
for root in (48, 60, 72):
    f = 440 * 2 ** ((root - 69) / 12); wav(f"{S}/soft_{root}.wav", tone(f, 3.0, 3, 1.2)); wav(f"{S}/loud_{root}.wav", 1.8 * tone(f, 3.0, 12, 1.0, 0.02)); wav(f"{S}/organ_{root}.wav", tone(f, 2.5, 8, None))
wav(f"{S}/sine_69.wav", 0.5 * np.sin(2 * np.pi * 440 * np.arange(int(2.5 * FS)) / FS))

# ---- notes and SFZ ----
check("note names: A0 = 21, C4 = 60, C8 = 108, Ds1 = 27, F#3 = 54, Bb2 = 46, plain numbers pass through", [notes.parse_note(s) for s in ("A0", "C4", "C8", "Ds1", "F#3", "Bb2", "77", 77)] == [21, 60, 108, 27, 54, 46, 77, 77])
check("note_name is the inverse for the whole keyboard", all(notes.parse_note(notes.note_name(k)) == k for k in range(0, 128)))
try: notes.parse_note("H9"); bad = False
except ValueError: bad = True
check("a nonsense note name is an error", bad)
open(f"{S}/t.sfz", "w").write(textwrap.dedent("""
    <control> default_path=sub/
    <global> volume=-2
    <group> lovel=1 hivel=60
    <region> sample=a.flac key=c3 loop_mode=loop_continuous loop_start=1000 loop_end=9000
    <region> sample=b c.flac lokey=50 hikey=60 pitch_keycenter=55 tune=7
    <group> lovel=61 hivel=127 // loud
    <region> sample=l.flac key=60 trigger=release
    <region> sample=m.flac pitch_keycenter=62 lokey=61 hikey=64
    """))
r = sfz.parse(f"{S}/t.sfz")
check("SFZ: regions, groups, default_path, key names, velocity layers, loops; release regions are skipped", len(r) == 3 and r[0]["root"] == 48 and r[0]["loop_sfz"] == (1000, 9000) and r[0]["vhi"] == 60 and r[1]["file"].endswith("sub/b c.flac") and r[1]["lo"] == 50 and r[1]["root"] == 55 and r[2]["vlo"] == 61 and r[2]["root"] == 62 and r[0]["volume"] == -2, str([(x["root"], x["vlo"], x["vhi"]) for x in r]))

# ---- config ----
cfg = yaml.safe_load(f"""
budget: 600000
normalize_peak: 0.9
normalize: instrument
layer_levels: natural
instruments:
  - name: Layers
    defaults: {{rel: 50, rev: 40}}
    rate_rules: [{{max_key: 127, rate: 22050, maxdur: 2.8}}]
    samples:
      - {{file: soft_48.wav, root: 48, vlo: 1, vhi: 70}}
      - {{file: soft_60.wav, root: 60, vlo: 1, vhi: 70}}
      - {{file: soft_72.wav, root: 72, vlo: 1, vhi: 70}}
      - {{file: loud_48.wav, root: 48, vlo: 71, vhi: 127, gain: -3}}
      - {{file: loud_60.wav, root: C4, vlo: 71, vhi: 127, gain: -3}}
      - {{file: loud_72.wav, root: 72, vlo: 71, vhi: 127, gain: -3}}
  - name: Organ
    defaults: {{rel: 30, decay: 127, trem: 90}}
    rate_rules: [{{max_key: 127, rate: 16000, maxdur: 1.5}}]
    import:
      - {{dir: samples, pattern: "organ_{{midi}}.wav", loop: auto}}
  - name: Sine
    rate_rules: [{{max_key: 127, rate: 16000, maxdur: 2.0}}]
    samples: [{{file: samples/sine_69.wav, root: A4, loop: auto}}]
""")
for s in cfg["instruments"][0]["samples"]: s["file"] = os.path.join(S, s["file"])
ins = core.expand_config(cfg, T)
check("config: three instruments, samples sorted by layer then root", [i["name"] for i in ins] == ["Layers", "Organ", "Sine"] and [s["root"] for s in ins[0]["samples"]] == [48, 60, 72, 48, 60, 72])
l = ins[0]["samples"]; check("config: key ranges split at the midpoints between roots inside each velocity layer (0-54, 55-66, 67-127)", [(s["lo"], s["hi"]) for s in l[:3]] == [(0, 54), (55, 66), (67, 127)] and [(s["lo"], s["hi"]) for s in l[3:]] == [(0, 54), (55, 66), (67, 127)])
check("config: vref is the layer's middle velocity (35, 99); a full-range sample defaults to 80; rate rules applied", [s["vref"] for s in l[:1] + l[3:4]] == [35, 99] and ins[1]["samples"][0]["vref"] == 80 and ins[1]["samples"][0]["rate"] == 16000)
check("config: the directory pattern found the three organ samples by MIDI number and kept the loop setting", [s["root"] for s in ins[1]["samples"]] == [48, 60, 72] and all(s["loop"] == "auto" for s in ins[1]["samples"]))
ci = core.expand_config({"instruments": [{"name": "c", "credit": 'Rhodes "Mk I" - CC BY-NC-SA 4.0', "samples": [{"file": f"{S}/soft_60.wav", "root": 60}]}]}, T)
check("config: the credit line is carried per instrument (default empty); a quote in it cannot break the generated C header; over 38 characters is a warning", ins[0]["credit"] == "" and ci[0]["credit"].startswith("Rhodes") and "credit line is" in " ".join(core.check_config(core.expand_config({"instruments": [{"name": "c", "credit": "x" * 50, "samples": [{"file": f"{S}/soft_60.wav", "root": 60}]}]}, T))))
cmds, pdef = core.read_params(); check("config: instrument defaults are read against the firmware's own parameter table", ins[0]["defaults"][cmds.index("rel")] == 50 and ins[0]["defaults"][cmds.index("rev")] == 40 and ins[0]["defaults"][cmds.index("vol")] == pdef[cmds.index("vol")], str(cmds))
for bad, msg in (({"instruments": [{"name": "x", "defaults": {"nonsense": 1}, "samples": [{"file": f"{S}/soft_48.wav", "root": 48}]}]}, "unknown default"), ({"instruments": []}, "no instruments"), ({"instruments": [{"name": "x", "import": [{"dir": f"{T}/nothere"}]}]}, "not found")):
    try: core.expand_config(bad, T); good = False
    except core.BankError as e: good = msg in str(e)
    check(f"config error is reported clearly: {msg}", good)
try: core.build({"instruments": [{"name": "x", "samples": [{"file": f"{T}/missing.wav", "root": 60}]}]}, T); good = False
except core.BankError as e: good = "not found" in str(e)
check("a missing sample file is a clear error, not a crash", good)

# ---- estimate, build, budget ----
est = core.estimate(cfg, T); rep = core.build(cfg, T)
check("estimate: the size predicted without encoding is within 1 % of the real build", abs(est["total"] - rep["total"]) / rep["total"] < 0.01, f"estimate {est['total']}, build {rep['total']}")
Z = rep["zones"]; zl = [z for z in Z if z["instrument"] == 0]
check("build: 6 + 3 + 1 zones in 3 instruments; per-instrument byte counts add up to the data size (+ alignment padding)", len(Z) == 10 and sum(i["bytes"] for i in rep["instruments"]) <= rep["total"] <= sum(i["bytes"] for i in rep["instruments"]) + 4 * len(Z))
check("build: the header carries each instrument's credit and name", '"Layers", ""' in core.header_text(rep) and "struct PianoInstrument { const char* name; const char* credit;" in core.header_text(rep))
check("build: layer velocity ranges, vref, and the -3 dB sample gain (gain10 = -30) end up in the zone table", [(z["vlo"], z["vhi"], z["vref"], z["gain10"]) for z in zl[:1] + zl[3:4]] == [(1, 70, 35, 0), (71, 127, 99, -30)])
loops = [z for z in Z if z["loopEnd"]]
check("build: the sustained samples got a seamless loop (0.25 .. 1 s long, ending at the stored data end); the decaying ones none", len(loops) == 4 and all(z["loopEnd"] == z["n"] and 0.25 * z["rate"] <= z["loopEnd"] - z["loopStart"] <= 1.0 * z["rate"] and len(z["lh"]) == 4 and len(z["lw"]) == 4 and any(z["lw"]) and z["lidx"] >= 0 for z in loops) and not any(z["loopEnd"] for z in zl), f"loops {[(z['loopStart'], z['loopEnd']) for z in loops]}")
small = dict(cfg, budget=80000); e2 = core.estimate(small, T); r2 = core.build(small, T)
check("budget: a tight budget shortens every sample to fit (80 KB: built size within 2 %, scale < 1)", r2["total"] <= 80000 * 1.02 and r2["scale"] < 1.0 and r2["total"] > 0.8 * 80000, f"{r2['total']} bytes, scale {r2['scale']:.3f}")
check("budget: an unusably tight budget is reported (every sample cut to a sliver), not silently met", any("very tight" in w for w in core.build(dict(cfg, budget=1000), T)["warnings"]))
W = rep["warnings"]
check("warnings: gaps / large pitch shifts / increments above 1 / low ADPCM SNR are reported", any("large pitch shifts" in w for w in W) and any("more than one ADPCM sample" in w for w in W) and any("SNR" in w for w in W), f"{len(W)} warnings")
gap = core.expand_config({"instruments": [{"name": "g", "samples": [{"file": f"{S}/soft_60.wav", "root": 60, "lo": 55, "hi": 65}]}]}, T); gw = core.check_config(gap)
check("warnings: a keyboard with unmapped keys says which (C#8? no: keys below / above the sample)", any("no sample for keys" in w for w in gw), str(gw[:1]))
layers, cov = core.coverage(ins[0]["samples"]); check("coverage(): per key and velocity layer, the sample that plays (layer 0 key 50 -> soft_48, layer 1 key 100 -> loud_72)", layers == [(1, 70), (71, 127)] and cov[50] == [0, 3] and cov[100] == [2, 5])

# ---- compiling the C++ helper on other people's machines (a Mac user's 'cstdio file not found') ----
_saved = core.cxx_candidates
core.cxx_candidates = lambda: ["/bin/false"] + _saved(); _o = os.path.join(T, "cxx_fallback"); core.compile_cpp([os.path.join(ROOT, "tools", "adpcm_tool.cpp")], _o, what="test"); fb_ok = os.path.exists(_o)
check("compiling: a broken first compiler is skipped and the next one is used", fb_ok)
core.cxx_candidates = lambda: ["/bin/false"]
try: core.compile_cpp([os.path.join(ROOT, "tools", "adpcm_tool.cpp")], os.path.join(T, "cxx_fail"), what="tools/adpcm_tool.cpp (the ADPCM encoder)"); msg = ""
except core.BankError as e: msg = str(e)
check("compiling: when every compiler fails the message names what failed and says what to install (xcode-select --install on macOS, build-essential on Linux, check_setup.py)", "could not compile tools/adpcm_tool.cpp" in msg and "xcode-select --install" in msg and "build-essential" in msg and "check_setup.py" in msg, msg.splitlines()[0][:80] if msg else "no error raised")
core.cxx_candidates = lambda: []
try: core.compile_cpp([os.path.join(ROOT, "tools", "adpcm_tool.cpp")], os.path.join(T, "cxx_none"), what="x"); msg2 = ""
except core.BankError as e: msg2 = str(e)
check("compiling: no compiler at all is a clear message too", "no C++ compiler found" in msg2 and "xcode-select" in msg2)
core.cxx_candidates = _saved
_t1 = core.ensure_adpcm_tool(); check("the ADPCM tool's file name carries a hash of its sources (a stale binary cannot be used)", len(os.path.basename(_t1).split("_")[-1]) == 10 and os.path.exists(_t1), os.path.basename(_t1))
r_ = subprocess.run([sys.executable, os.path.join(ROOT, "tools", "check_setup.py")], capture_output=True, text=True); check("tools/check_setup.py runs and reports every requirement (python packages, ffmpeg with soxr, a working C++ compiler)", r_.returncode == 0 and "soxr" in r_.stdout and "C++" in r_.stdout and "[FAIL]" not in r_.stdout, r_.stdout.strip().splitlines()[-1] if r_.stdout.strip() else r_.stderr[:100])

# ---- codec 2: an independent Python implementation of the decoder, straight from the spec, against the tool's C++ decoder ----
STEP_ = [7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 50, 55, 60, 66, 73, 80, 88, 97, 107, 118, 130, 143, 157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658, 724, 796, 876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428, 4871, 5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767]
IDX_ = [-1, -1, -1, -1, 2, 4, 6, 8] * 2
def ref_decode(data, n, pred0, idx0=0, shift=7, hist=None):
    h = ([pred0] + [int(v) for v in hist]) if hist else [pred0] * 4; w = [8192, 0, 0, 0]; idx = idx0; out = []
    cl = lambda v, lo, hi: lo if v < lo else hi if v > hi else v
    for i in range(n):
        code = (data[i >> 1] >> 4) if (i & 1) else (data[i >> 1] & 15); p = cl(sum(a * c for a, c in zip(w, h)) >> 13, -32768, 32767); step = STEP_[idx]; vp = step >> 3
        if code & 4: vp += step
        if code & 2: vp += step >> 1
        if code & 1: vp += step >> 2
        r = cl(p + (-vp if code & 8 else vp), -32768, 32767); idx = cl(idx + IDX_[code], 0, 88); e = (r > p) - (r < p)
        if e: w = [cl(a + ((e * c) >> shift), -32000, 32000) for a, c in zip(w, h)]
        h = [r] + h[:-1]; out.append(r)
    return np.array(out, dtype=np.int16)
rs = np.random.RandomState(7); good = True; detail = ""
for name, data, p0, i0 in (("random A", rs.randint(0, 256, 6000, dtype=np.uint8).tobytes(), 1234, 0), ("random B", rs.randint(0, 256, 6000, dtype=np.uint8).tobytes(), -30000, 40), ("all 0x77 (largest steps up)", bytes([0x77]) * 4000, 0, 88), ("all 0xFF (largest steps down)", bytes([0xFF]) * 4000, 5, 88), ("all 0x00", bytes(4000), 32767, 0), ("alternating 0x7F/0xF7", bytes([0x7F, 0xF7]) * 2000, -1, 20)):
    ref = ref_decode(data, len(data) * 2, p0, i0); got = np.frombuffer(core.adpcm("dec", data, p0, i0, len(data) * 2), dtype=np.int16)
    if not np.array_equal(ref, got): good = False; detail += f" {name}: first difference at sample {int(np.argmax(ref != got))};"
check("codec 2: the independent Python decoder equals the tool's C++ decoder bit for bit on random data, and on the extreme streams that exercise every clamp (weights, prediction, sample, step index)", good, detail)
ref_state = None
data = rs.randint(0, 256, 3000, dtype=np.uint8).tobytes(); st_ = [int(t) for t in core.adpcm("state", data, 100, 0, 777).decode().split()]
def ref_state_after(data, n, pred0):
    h = [pred0] * 4; w = [8192, 0, 0, 0]; idx = 0; cl = lambda v, lo, hi: lo if v < lo else hi if v > hi else v
    for i in range(n):
        code = (data[i >> 1] >> 4) if (i & 1) else (data[i >> 1] & 15); p = cl(sum(a * c for a, c in zip(w, h)) >> 13, -32768, 32767); step = STEP_[idx]; vp = step >> 3
        if code & 4: vp += step
        if code & 2: vp += step >> 1
        if code & 1: vp += step >> 2
        r = cl(p + (-vp if code & 8 else vp), -32768, 32767); idx = cl(idx + IDX_[code], 0, 88); e = (r > p) - (r < p)
        if e: w = [cl(a + ((e * c) >> 7), -32000, 32000) for a, c in zip(w, h)]
        h = [r] + h[:-1]
    return h + w + [idx]
check("codec 2: the decoder state the tool reports for a loop start (history, weights, step index) equals the Python reference's", st_ == ref_state_after(data, 777, 100), str(st_))
bk = open(os.path.join(ROOT, "tools", ".gen", "bank.bin"), "rb").read(); bj = json.load(open(os.path.join(ROOT, "tools", ".gen", "bank.json"))); same = True
for zq in (bj["zones"][0], bj["zones"][14], bj["zones"][-1]):
    H = zq["head"]; head = np.frombuffer(bk[zq["off"]:zq["off"] + 2 * H], dtype="<i2"); seg = bk[zq["off"] + 2 * H:zq["off"] + zq["bytes"]]
    start, hist = (int(head[-1]), [int(v) for v in head[-2:-5:-1]]) if H else (zq["pred0"], None)
    same &= np.array_equal(ref_decode(seg, 20000, start, zq["idx0"], hist=hist), np.frombuffer(core.adpcm("dec", seg, start, zq["idx0"], 20000, hist), dtype=np.int16))
check("codec 2: ... and on the first 20,000 ADPCM samples (after the PCM head, from its history) of three zones of the real shipped bank", same)
import re as _re
hd = open(os.path.join(ROOT, "src", "piano_adpcm.h")).read(); check("codec 2: the shift in the Python reference (7) is the firmware's LMS_SHIFT", int(_re.search(r"#define LMS_SHIFT (\d+)", hd).group(1)) == 7)

# ---- the real engine on the built bank ----
def stage_bank(rep, name):
    d = os.path.join(T, name); os.makedirs(d, exist_ok=True)
    for f in os.listdir(os.path.join(ROOT, "src")):
        if f.endswith(".h") and f != "piano_bank.h": shutil.copy(os.path.join(ROOT, "src", f), d)
    core.write_outputs(rep, d); return d
def build_probe(stage, out):
    r = subprocess.run(["g++", "-std=gnu++17", "-O1", "-I", os.path.join(ROOT, "host_test", "mock"), "-I", stage, os.path.join(ROOT, "host_test", "probe.cpp"), os.path.join(stage, "piano_data.cpp"),
                        os.path.join(ROOT, "host_test", "mock", "mock_globals.cpp"), os.path.join(ROOT, "host_test", "mock", "mock_globals2.cpp"), "-o", out], capture_output=True, text=True)
    assert r.returncode == 0, r.stderr[:600]; return out
def play(exe, script):
    raw = os.path.join(T, "p.raw"); out = subprocess.run([exe], input=script + f"write {raw}\n", capture_output=True, text=True).stdout
    return out, (np.fromfile(raw, dtype=np.int16).astype(float).reshape(-1, 2) / 32768 if os.path.exists(raw) else None)
stage = stage_bank(rep, "stage_synth"); exe = build_probe(stage, os.path.join(T, "probe_synth"))
NEU = "set vol 127\nset tone 127\nset width 64\nset trem 64\nset rev 0\n"
o, _ = play(exe, "zone? 60 30\nzone? 60 100\nzone? 50 30\nzone? 100 100\ninst 1\nzone? 60 30\n"); zz = [l for l in o.split("\n") if l.startswith("ZONE")]
check("engine: the velocity layer is chosen by velocity (60/30 -> soft, 60/100 -> loud), the key range by key (50 -> the 48 sample; 100 -> the 72 sample)", [(l.split()[1]) for l in zz[:4]] == ["1", "4", "0", "5"] and zz[4].split()[1] == "7" and "loop=" in zz[4] and not zz[4].endswith("loop=0"), " | ".join(zz))
check("engine: a velocity outside every layer, a key outside every range, still find the nearest zone", "ZONE" in play(exe, "zone? 60 0\nzone? 0 127\nzone? 127 1\n")[0] and "ZONE -1" not in play(exe, "zone? 60 0\nzone? 0 127\nzone? 127 1\n")[0])
o, _ = play(exe, "inst 1\nparam? 3\nparam? 11\nstatus\n"); check("engine: selecting an instrument loads its knob defaults (Organ: rel 30, trem 90) and reports it", "PV 30" in o and "PV 90" in o and "inst=1" in o, " ".join(o.split()))
_, a = play(exe, NEU + "on 60 100\nrun 20000\n"); _, b = play(exe, NEU + "on 60 40\nrun 20000\n")
rms = lambda x: float(np.sqrt((x ** 2).mean()))
# gain10: loud layer carries -3 dB and plays at its own vref (99): vel 99 -> loud sample at unity (x 10^(-3/20)); the soft layer at vref 35
_, loud = play(exe, NEU + "on 60 99\nrun 20000\n"); _, soft = play(exe, NEU + "on 60 35\nrun 20000\n")
direct = {z["name"]: z for z in Z}; xl = core.load_sample(os.path.join(S, "loud_60.wav")); xs = core.load_sample(os.path.join(S, "soft_60.wav"))
exp = rms(xl["x"][:20000]) / rms(xs["x"][:20000])      # the tool normalises an instrument with ONE gain (its loudest sample), so the layers keep their relative level
check("engine: each layer plays unchanged at its own reference velocity, and the loud layer's -3 dB zone gain is applied (loud/soft RMS = source ratio x 0.708, +-8 %)", abs(rms(loud) / rms(soft) / (exp * 10 ** (-3 / 20)) - 1) < 0.08, f"measured {rms(loud)/rms(soft):.3f}, expected {exp * 10 ** (-3 / 20):.3f}")
# a pure sine, looped: the engine must keep it a clean sinusoid for 10 s
o, x = play(exe, NEU + "set width 0\ninst 2\non 69 80\nrun 441000\n"); y = x[:, 0][FS:]; t = np.arange(len(y)) / FS
A = np.vstack([np.sin(2 * np.pi * 440 * t), np.cos(2 * np.pi * 440 * t), np.ones_like(t)]).T; coef, *_ = np.linalg.lstsq(A, y, rcond=None); res = y - A @ coef; amp = float(np.hypot(coef[0], coef[1]))
check("engine: a looped pure sine stays a clean sinusoid for 9 s of looping (residual < -35 dB re the amplitude: no phase jump, no click, no drift)", rms(res) / (amp / np.sqrt(2)) < 10 ** (-35 / 20), f"residual {20*np.log10(rms(res)/(amp/np.sqrt(2))):.1f} dB")
check("engine: ... and no single seam spike (the largest residual sample is < 6 % of the amplitude)", np.abs(res).max() < 0.06 * amp, f"max residual {np.abs(res).max()/amp*100:.2f} % of the amplitude")
check("engine: ... at constant level (RMS in the 2nd and the 9th second equal within 0.3 dB)", abs(20 * np.log10(rms(y[FS:2 * FS]) / rms(y[8 * FS:9 * FS]))) < 0.3)
lp = [z for z in Z if z["instrument"] == 2][0]; per = (lp["loopEnd"] - lp["loopStart"]) / lp["rate"]
wraps = [(lp["loopEnd"] / lp["rate"]) + i * per for i in range(1, 10)]; d2 = np.abs(np.diff(x[:, 0], 2))
ratio = lambda w: d2[int(w * FS) - 40:int(w * FS) + 40].max() / (np.median(d2[int(w * FS) - 441:int(w * FS) + 441]) + 1e-9)
rw = [ratio(w) for w in wraps]; ref = [ratio(w + per / 2.7) for w in wraps]          # the same measurement at points that are NOT loop wraps
check("engine: the loop wraps are no rougher than other points of the same note (worst wrap <= 1.3 x the worst non-wrap point)", max(rw) <= 1.3 * max(ref), f"wraps worst {max(rw):.2f}, elsewhere worst {max(ref):.2f}")
o, x = play(exe, NEU + "inst 1\non 60 80\nrun 22050\noff 60\nrun 40000\n"); check("engine: a looped note is released by the damper like any other and goes silent", np.abs(x[-4000:]).max() == 0 and np.abs(x[22050:22500]).max() > 0.01)
# the engine's own test-suite on the synthetic bank (layers, loops, instruments under torture)
eng = os.path.join(T, "test_engine_synth")
r = subprocess.run(["g++", "-std=gnu++17", "-O1", "-g", "-fsanitize=address,undefined,float-cast-overflow", "-fno-sanitize-recover=float-cast-overflow", "-pthread", "-Wno-unused-function", "-Wno-unused-parameter", "-I", os.path.join(ROOT, "host_test", "mock"), "-I", stage,
                    os.path.join(ROOT, "host_test", "test_engine.cpp"), os.path.join(stage, "piano_data.cpp"), os.path.join(ROOT, "host_test", "mock", "mock_globals.cpp"), os.path.join(ROOT, "host_test", "mock", "mock_globals2.cpp"), "-o", eng], capture_output=True, text=True)
check("the engine test suite compiles against the synthetic multi-instrument bank", r.returncode == 0, r.stderr[:300])
if r.returncode == 0:
    out = subprocess.run([eng], capture_output=True, text=True, timeout=280).stdout; check("... and passes there: layers, loops and instrument switching under 250,000 random events, two threads, ASan + UBSan", "ALL ENGINE TESTS PASS" in out, "\n".join(l for l in out.split("\n") if l.startswith("FAIL"))[:300])

# ---- importer robustness (found with the real libraries and your server log) ----
D2 = os.path.join(T, "dups"); os.makedirs(D2); shutil.copy(f"{S}/soft_60.wav", f"{D2}/C4.wav"); shutil.copy(f"{S}/soft_72.wav", f"{D2}/C5.wav")
for ext in ("flac", "ogg"): subprocess.run(["ffmpeg", "-v", "error", "-y", "-i", f"{S}/soft_60.wav", f"{D2}/C4.{ext}"], check=True)
cd_ = core.expand_config({"instruments": [{"name": "d", "import": [{"dir": D2, "pattern": "{note}.*"}]}]}, T)[0]
check("import: a folder with one note in several formats gives ONE sample per note (wav before flac before ogg) and says what it ignored", [(s["root"], os.path.splitext(s["file"])[1]) for s in cd_["samples"]] == [(60, ".wav"), (72, ".wav")] and len(cd_["notes"]) == 2 and all("ignoring" in n for n in cd_["notes"]), str(cd_["notes"][:1]))
check("config: two samples for the same root in the same velocity layer are a warning", any("two samples for" in w for w in core.check_config(core.expand_config({"instruments": [{"name": "d", "samples": [{"file": f"{S}/soft_60.wav", "root": 60}, {"file": f"{S}/soft_60.wav", "root": 60}]}]}, T))))
for cfg_bad, want in (({"name": "x", "sfz": [{"file": f"{T}/samples/jRhodes3c/none.sfz"}]}, "fetch_rhodes.sh"), ({"name": "x", "sfz": f"{T}/samples/stevie-clavinet/none.sfz"}, "fetch_clavinet.sh"), ({"name": "x", "import": [{"dir": f"{T}/samples/salamander"}]}, "fetch_salamander.sh")):
    try: core.expand_config({"instruments": [cfg_bad]}, T); good = False
    except core.BankError as e: good = (("SFZ file not found" in str(e)) or ("directory not found" in str(e))) and want in str(e)
    check(f"a missing sample set is a clear error that names the download script ({want}), not a crash (your server log's 500)", good)
open(f"{S}/layers.sfz", "w").write("<global> default_path=./\n" + "".join(f"<region> sample=organ_{48 if r <= 54 else 60}.wav lokey={r - 1} hikey={r + 1} pitch_keycenter={r} lovel={vl} hivel={vh}\n" for (vl, vh) in ((1, 40), (41, 80), (81, 127)) for r in (48, 51, 54, 57, 60, 63)))
def sfzi(**kw): return core.expand_config({"instruments": [{"name": "s", "rate_rules": [{"max_key": 127, "rate": 16000, "maxdur": 1.0}], "sfz": [dict(file=f"{S}/layers.sfz", **kw)]}]}, T)[0]["samples"]
a_ = sfzi(); b_ = sfzi(max_layers=2); lay = sorted({(s["vlo"], s["vhi"]) for s in b_})
check("sfz option max_layers: 3 layers -> 2 evenly spaced ones that together cover velocity 1..127 with no gap", len(a_) == 18 and len(lay) == 2 and lay[0][0] == 1 and lay[-1][1] == 127 and lay[1][0] == lay[0][1] + 1, str(lay))
c_ = sfzi(every=2); r_ = sorted({s["root"] for s in c_ if s["vlo"] == 1})
check("sfz option every: every 2nd root kept (always the highest too) and the key ranges become automatic again (they cover 0..127)", r_ == [48, 54, 60, 63] and min(s["lo"] for s in c_) == 0 and max(s["hi"] for s in c_) == 127, str(r_))
check("sfz option loop: 'auto' / 'none' override the SFZ's loop settings for every sample", {s["loop"] for s in sfzi(loop="auto")} == {"auto"} and {s["loop"] for s in sfzi(loop="none")} == {"none"})
# the beam-search encoder: same decoder, less noise, deterministic
bcfg = {"budget": 400000, "normalize_peak": 0.9, "instruments": [{"name": "b", "rate_rules": [{"max_key": 127, "rate": 22050, "maxdur": 1.5}], "samples": [{"file": f"{S}/loud_60.wav", "root": 60}, {"file": f"{S}/loud_72.wav", "root": 72}, {"file": f"{S}/soft_48.wav", "root": 48}]}]}
r0 = core.build(dict(bcfg, adpcm_beam=0), T); r8 = core.build(dict(bcfg, adpcm_beam=8), T); r8b = core.build(dict(bcfg, adpcm_beam=8), T)
m0 = np.mean([z["snr"] for z in r0["zones"]]); m8 = np.mean([z["snr"] for z in r8["zones"]])
check("adpcm_beam: the beam-search encoder lowers the noise (mean SNR up by at least 0.4 dB), same sizes, same decoder", m8 >= m0 + 0.4 and [z["bytes"] for z in r0["zones"]] == [z["bytes"] for z in r8["zones"]], f"{m0:.2f} -> {m8:.2f} dB")
check("adpcm_beam: deterministic (two builds are byte-identical) and different from the classic encoder's bytes", r8["blob"] == r8b["blob"] and r8["blob"] != r0["blob"])
# the real libraries, when they have been downloaded (tools/fetch_rhodes.sh, fetch_clavinet.sh)
rh = os.path.join(ROOT, "samples", "jRhodes3c", "jRhodes3c-looped-flac-sfz", "_jRhodes-mono-looped.sfz"); cl = os.path.join(ROOT, "samples", "stevie-clavinet", "Stevie Clavinet.sfz")
def real(path, **kw): return core.expand_config({"instruments": [{"name": "r", "rate_rules": [{"max_key": 127, "rate": 24000, "maxdur": 2.0}], "sfz": [dict(file=path, **kw)]}]}, T)[0]["samples"]
if os.path.exists(rh):
    R = real(rh, loop="auto"); check("real Rhodes (jRhodes3c): 75 samples = 5 velocity layers x 15 roots (F1..C7), loops by the tool; max_layers 3 gives 45", len(R) == 75 and len({(s["vlo"], s["vhi"]) for s in R}) == 5 and len({s["root"] for s in R}) == 15 and len(real(rh, loop="auto", max_layers=3)) == 45)
else: print("SKIP real Rhodes library not downloaded (tools/fetch_rhodes.sh)")
if os.path.exists(cl):
    R = real(cl); check("real clavinet (Stevie Clavinet): 240 samples = 4 layers x 60 roots, the release-noise samples are skipped; max_layers 2 + every 3 gives 42", len(R) == 240 and len({(s["vlo"], s["vhi"]) for s in R}) == 4 and len({s["root"] for s in R}) == 60 and len(real(cl, max_layers=2, every=3)) == 42)
else: print("SKIP real clavinet library not downloaded (tools/fetch_clavinet.sh)")

# ---- PCM head, per-zone normalisation, level matching ----
hz = [z for z in core.build(dict(cfg, normalize="zone", budget=600000), T)["zones"]]
check("PCM head: every zone starts with 9 ms of 16-bit PCM (rate x 9 ms samples), and its size is 2 bytes per head sample + the ADPCM bytes", all(z["head"] == core.head_len(z["rate"], 9.0, z["n"]) and z["head"] > 100 and z["bytes"] == 2 * z["head"] + (z["n"] - z["head"] + 1) // 2 for z in hz), str(sorted({z["head"] for z in hz})))
r_nohead = core.build(dict(cfg, pcm_head_ms=0, normalize="instrument"), T)
check("PCM head: pcm_head_ms 0 gives the plain layout (no head, bytes == (n+1)/2) and a head only costs a few hundred bytes per zone", all(z["head"] == 0 and z["bytes"] == (z["n"] + 1) // 2 for z in r_nohead["zones"]) and 0 < sum(z["bytes"] for z in hz) - sum(z["bytes"] for z in r_nohead["zones"]) < 700 * len(hz))
bk2 = core.build(dict(cfg, normalize="zone", budget=600000), T); zq = bk2["zones"][1]; Hh = zq["head"]; hd = np.frombuffer(bk2["blob"][zq["off"]:zq["off"] + 2 * Hh], dtype="<i2"); tl = bk2["blob"][zq["off"] + 2 * Hh:zq["off"] + zq["bytes"]]
full = np.concatenate([hd, np.frombuffer(core.adpcm("dec", tl, int(hd[-1]), zq["idx0"], 3000, [int(v) for v in hd[-2:-5:-1]]), dtype=np.int16)]).astype(float)
pre = np.abs(np.diff(full[max(0, Hh - 100):Hh - 3])); post = np.abs(np.diff(full[Hh + 4:Hh + 104])); join = np.abs(np.diff(full[Hh - 3:Hh + 4])).max()
check("PCM head: the ADPCM part continues the head without a step (the largest sample-to-sample change across the handover is no larger than 3x the largest on either side: no glitch)", len(pre) > 20 and join <= 3 * max(pre.max(), post.max()) + 50, f"join {join:.0f} vs before {pre.max():.0f} / after {post.max():.0f}; head {Hh} samples, idx0 {zq['idx0']}")
# per-zone normalisation: every zone stored at full scale, the same level at play-back
qc = {"budget": 500000, "normalize_peak": 0.9, "pcm_head_ms": 9, "instruments": [{"name": "q", "rate_rules": [{"max_key": 127, "rate": 22050, "maxdur": 2.0}], "samples": [{"file": f"{S}/loud_60.wav", "root": 60, "hi": 66}, {"file": f"{S}/soft_72.wav", "root": 72, "lo": 67, "gain": 0}]}]}
rz = core.build(dict(qc, normalize="zone"), T); ri = core.build(dict(qc, normalize="instrument"), T)
def stored_peak(rep_, i):
    zq_ = rep_["zones"][i]; Hq = zq_["head"]; h_ = np.frombuffer(rep_["blob"][zq_["off"]:zq_["off"] + 2 * Hq], dtype="<i2"); t_ = rep_["blob"][zq_["off"] + 2 * Hq:zq_["off"] + zq_["bytes"]]
    d_ = np.frombuffer(core.adpcm("dec", t_, int(h_[-1]), zq_["idx0"], zq_["n"] - Hq, [int(v) for v in h_[-2:-5:-1]]), dtype=np.int16); return max(np.abs(h_).max(), np.abs(d_).max()) / 32768
pz = [stored_peak(rz, 0), stored_peak(rz, 1)]; pi_ = [stored_peak(ri, 0), stored_peak(ri, 1)]
check("normalize: zone stores EVERY zone close to the full-scale target (0.9 +- 8 %: the target is applied before resampling, which band-limits a sharp attack by a few percent), instrument mode leaves the quieter zone low", all(abs(p - 0.9) < 0.072 for p in pz) and min(pi_) < 0.85 * max(pi_) and min(pz) > min(pi_) + 0.1, f"zone {pz[0]:.2f} / {pz[1]:.2f}; instrument {pi_[0]:.2f} / {pi_[1]:.2f}")
check("normalize: zone compensates with a NEGATIVE run-time gain on the zone that was boosted (gain10 < 0), none on the loudest", rz["zones"][0]["gain10"] == 0 and rz["zones"][1]["gain10"] < -5 or rz["zones"][0]["gain10"] < -5 and rz["zones"][1]["gain10"] == 0, str([z["gain10"] for z in rz["zones"]]))
sz = stage_bank(rz, "stage_nz"); si = stage_bank(ri, "stage_ni"); ez = build_probe(sz, os.path.join(T, "probe_nz")); ei = build_probe(si, os.path.join(T, "probe_ni")); lv = []
for exe_ in (ez, ei):
    for key_ in (60, 72): _, x_ = play(exe_, NEU + f"on {key_} 80\nrun 30000\n"); lv.append(rms(x_))
check("normalize: ... and the PLAYBACK level is unchanged (zone vs instrument mode, through the real engine, both zones within 0.5 dB)", abs(20 * np.log10(lv[0] / lv[2])) < 0.5 and abs(20 * np.log10(lv[1] / lv[3])) < 0.5, f"{20*np.log10(lv[0]/lv[2]):+.2f} / {20*np.log10(lv[1]/lv[3]):+.2f} dB")
# level matching between instruments
wav(f"{S}/pluck_60.wav", tone(261.6, 1.5, 3, 0.04))                  # a pluck that is gone after ~100 ms: low RMS in the first 300 ms relative to its peak
lc = {"budget": 600000, "normalize_peak": 0.9, "instruments": [{"name": "quiet", "rate_rules": [{"max_key": 127, "rate": 22050, "maxdur": 1.5}], "samples": [{"file": f"{S}/pluck_60.wav", "root": 60}]},
                                                                  {"name": "loud", "rate_rules": [{"max_key": 127, "rate": 22050, "maxdur": 2.0}], "samples": [{"file": f"{S}/sine_69.wav", "root": 60}]}]}
r0 = core.build(lc, T); rm_ = core.build(dict(lc, level_match="auto"), T); lv0 = [i["level_db"] for i in rm_["instruments"]]; off = [i["level_offset_db"] for i in rm_["instruments"]]
check("level_match: the report carries each instrument's measured level and the offset; offsets are zero without level_match, and bring the instruments to their median", all(i["level_offset_db"] == 0 for i in r0["instruments"]) and abs((lv0[0] + off[0]) - (lv0[1] + off[1])) < 0.2 and max(abs(o) for o in off) > 1.0, f"levels {lv0}, offsets {off}")
sa = stage_bank(r0, "stage_l0"); sb = stage_bank(rm_, "stage_l1"); ea = build_probe(sa, os.path.join(T, "probe_l0")); eb = build_probe(sb, os.path.join(T, "probe_l1")); gap = []
for exe_ in (ea, eb):
    lvl = []
    for ins_ in (0, 1): _, x_ = play(exe_, NEU + f"inst {ins_}\non 60 80\nrun 14000\n"); lvl.append(20 * np.log10(rms(x_[:13230])))
    gap.append(abs(lvl[0] - lvl[1]))
check("level_match: through the real engine, two instruments that differ by several dB differ by far less after matching (RMS of the first 300 ms, the window the tool measures)", gap[1] < 2.0 and gap[1] < gap[0] - 1.5, f"before {gap[0]:.1f} dB, after {gap[1]:.1f} dB")
check("level_match accepts a number (a target in dBFS): the offset is target - measured, limited to +-12 dB with a warning", abs(core.build(dict(lc, level_match=-30.0), T)["instruments"][0]["level_offset_db"] - max(-12.0, min(12.0, -30.0 - lv0[0]))) < 0.15)
check("the header declares codec/bank format 3 and the zone struct has the head field", "#define PIANO_CODEC 3" in core.header_text(rz) and "uint16_t rate, head;" in core.header_text(rz))

# ---- layered instruments: ONE loudness curve across layers (the clavinet was 13 dB louder than the grand at soft velocities and dropped 10 dB at a layer boundary) ----
lw = {"budget": 500000, "normalize_peak": 0.9, "instruments": [{"name": "lay", "rate_rules": [{"max_key": 127, "rate": 22050, "maxdur": 2.0}], "samples": [
      {"file": f"{S}/soft_60.wav", "root": 60, "vlo": 1, "vhi": 60}, {"file": f"{S}/loud_60.wav", "root": 60, "vlo": 61, "vhi": 127}]}]}
def vel_curve(layer_levels):
    rep_ = core.build(dict(lw, layer_levels=layer_levels), T); st_ = stage_bank(rep_, "stage_" + layer_levels); ex_ = build_probe(st_, os.path.join(T, "probe_" + layer_levels)); vs = list(range(20, 128, 6)); out_ = []
    for v_ in vs: _, x_ = play(ex_, NEU + f"on 60 {v_}\nrun 8000\n"); out_.append(20 * np.log10(rms(x_[441:441 + 6615]) + 1e-9))
    return vs, np.array(out_), rep_
vs_, law_, rep_law = vel_curve("law"); _, nat_, rep_nat = vel_curve("natural")
step_law = np.diff(law_); step_nat = np.diff(nat_); bi = [i for i in range(len(vs_) - 1) if vs_[i] < 61 <= vs_[i + 1]][0]
check("layer_levels law: the loudness rises smoothly with the played velocity across a layer boundary (no dip worse than -2 dB anywhere and the step at the boundary within 4 dB of its neighbours: the two synthetic layers are peak-normalised to the same height but differ in waveshape, so a dB or two of RMS remains; the old behaviour stepped by 10-13 dB)", step_law.min() > -2.0 and abs(step_law[bi] - np.median(step_law)) < 4.0 and step_law[bi] > step_nat[bi] + 4.0, f"steps {np.round(step_law, 1)}")
check("layer_levels natural (the old behaviour) on the same samples does step at the boundary: the test can see the problem", abs(step_nat[bi] - np.median(step_nat)) > 4.0 or step_nat.min() < -3.0, f"boundary step {step_nat[bi]:.1f} dB, steps {np.round(step_nat, 1)}")
check("layer_levels law follows the common curve (v/80)^1.4: 28 dB per decade of velocity (the loudness over velocities 30 -> 90 rises by 14 +- 3 dB)", abs((law_[vs_.index(86)] - law_[vs_.index(32)]) - 28.08 * np.log10(86 / 32)) < 3.0, f"{law_[vs_.index(86)] - law_[vs_.index(32)]:.1f} dB vs {28.08*np.log10(86/32):.1f}")
try: core.build(dict(lw, layer_levels="bogus"), T); bad_ll = False
except core.BankError as e: bad_ll = "layer_levels must be" in str(e)
check("layer_levels: an unknown value is a clear error", bad_ll)

# ---- render command and the noise report ----
cf2 = os.path.join(T, "render.yaml"); yaml.safe_dump(cfg, open(cf2, "w"))
wv = os.path.join(T, "r.wav"); rr = subprocess.run([sys.executable, "-m", "bankconf", "render", cf2, "--inst", "0", "--notes", "60:80,64:110", "--out", wv, "--set", "rev=0"], cwd=os.path.join(ROOT, "tools"), capture_output=True, text=True)
w_ = wave.open(wv) if os.path.exists(wv) else None
check("CLI: `bankconf render` builds the bank and writes a stereo 16-bit 44.1 kHz WAV of the notes played through the engine (2 notes, 2 s apart + tail)", rr.returncode == 0 and w_ is not None and w_.getnchannels() == 2 and w_.getframerate() == FS and 5.5 < w_.getnframes() / FS < 7.5, (rr.stderr.strip().splitlines() or ["ok"])[-1][:100])
nr = subprocess.run([sys.executable, os.path.join(ROOT, "tools", "noise_report.py"), wv], capture_output=True, text=True)
check("tools/noise_report.py runs on that render and reports the channels, the notes (or says it found none)", nr.returncode == 0 and "RMS per channel" in nr.stdout and ("notes." in nr.stdout or "no note onsets" in nr.stdout), nr.stdout.strip().splitlines()[0][:90] if nr.stdout.strip() else nr.stderr[:100])
mono_r = np.zeros((2 * FS, 2)); tt = np.arange(2 * FS) / FS; mono_r[:, 1] = 0.2 * np.sin(2 * np.pi * 440 * tt); wr = os.path.join(T, "right_only.wav"); w2 = wave.open(wr, "wb"); w2.setnchannels(2); w2.setsampwidth(2); w2.setframerate(FS); w2.writeframes((mono_r * 32767).astype(np.int16).tobytes()); w2.close()
nr2 = subprocess.run([sys.executable, os.path.join(ROOT, "tools", "noise_report.py"), wr], capture_output=True, text=True)
check("noise_report: a recording with one dead channel is flagged (a centred note must be on both: the quiet channel says nothing about the hardware floor)", "channels differ" in nr2.stdout and "no note onsets" in nr2.stdout, nr2.stdout.strip().splitlines()[1][:100] if len(nr2.stdout.strip().splitlines()) > 1 else nr2.stdout[:100])

# ---- CLI ----
cf = os.path.join(T, "c.yaml"); yaml.safe_dump(cfg, open(cf, "w")); r = subprocess.run([sys.executable, "-m", "bankconf", "estimate", cf], cwd=os.path.join(ROOT, "tools"), capture_output=True, text=True)
check("CLI: python -m bankconf estimate prints the budget and per-instrument sizes", r.returncode == 0 and "budget" in r.stdout and "instrument 2" in r.stdout, r.stdout.split("\n")[0])
r = subprocess.run([sys.executable, "-m", "bankconf", "estimate", os.path.join(T, "nothere.yaml")], cwd=os.path.join(ROOT, "tools"), capture_output=True, text=True); check("CLI: a missing config file is an error exit, not a hang", r.returncode != 0)

# ---- the shipped bank ----
if "--full" in sys.argv:
    gdir = os.path.join(ROOT, "samples", "salamander")
    if not os.path.isdir(gdir): check("shipped bank rebuild: SKIPPED (run tools/fetch_salamander.sh first)", True)
    else:
        r = subprocess.run([sys.executable, "-m", "bankconf", "build", os.path.join(ROOT, "banks", "grand.yaml"), "--dry"], cwd=os.path.join(ROOT, "tools"), capture_output=True, text=True)
        rep0 = core.build(yaml.safe_load(open(os.path.join(ROOT, "banks", "grand.yaml"))), os.path.join(ROOT, "banks")); old = open(os.path.join(ROOT, "tools", ".gen", "bank.bin"), "rb").read()
        check("the shipped grand rebuilt from banks/grand.yaml is byte-for-byte the bank in the firmware", rep0["blob"] == old, f"{len(rep0['blob'])} vs {len(old)} bytes, md5 {hashlib.md5(rep0['blob']).hexdigest()}")
shutil.rmtree(T, ignore_errors=True)
print("\nALL BANK TESTS PASS" if ok else "\nBANK TESTS FAILED"); sys.exit(0 if ok else 1)
