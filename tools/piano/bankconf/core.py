"""bankconf core: turns a bank config (YAML/dict) into the firmware's sample bank.

Pipeline per sample:  decode (ffmpeg, mono 44.1 kHz) -> trim to the onset -> keep as much as the flash budget allows ->
fade out (or find a seamless loop) -> resample (soxr) -> 4-bit IMA ADPCM -> zone table entry.
Config -> a list of instruments, each with samples (key range, velocity range, root, loop, rate ...). See bank_example.yaml.
"""
import os, re, sys, json, glob, hashlib, subprocess, shutil, tempfile
import numpy as np
from .notes import parse_note, note_name
from . import sfz as sfzmod

FS = 44100
HERE = os.path.dirname(os.path.abspath(__file__))
# Two layouts, found by looking: the picomusic repository (this tool in tools/piano/) and the flat layout of the stand-alone piano project / the test staging (tools/, src/, host_test/).
_flat = os.path.normpath(os.path.join(HERE, "..", ".."))
if os.path.isfile(os.path.join(_flat, "src", "piano_params.h")):
    ROOT = _flat; SRC_DIRS = [os.path.join(ROOT, "src")]; ADPCM_SRC = os.path.join(ROOT, "tools", "adpcm_tool.cpp"); GEN = os.path.join(ROOT, "tools", ".gen")
    HOSTTEST = os.path.join(ROOT, "host_test"); SAMPLES_ROOT = ROOT
else:
    ROOT = os.path.normpath(os.path.join(HERE, "..", "..", ".."))                # the repository root
    SRC_DIRS = [os.path.join(ROOT, "firmware", "piano"), os.path.join(ROOT, "firmware", "shared")]      # the piano's own headers, then the shared ones (stereo_fx.h ...)
    ADPCM_SRC = os.path.join(ROOT, "tools", "piano", "adpcm_tool.cpp"); GEN = os.path.join(ROOT, "tools", "piano", ".gen")
    HOSTTEST = os.path.join(ROOT, "tests", "piano"); SAMPLES_ROOT = os.path.join(ROOT, "tools", "piano")
SRC = SRC_DIRS[0]
ADPCM_TOOL = os.path.join(tempfile.gettempdir(), "picopiano_adpcm_tool")

class BankError(Exception): pass

def missing_hint(path):
    """a missing file under samples/ almost always means the sample set has not been downloaded yet"""
    p = str(path).replace("\\", "/").lower()
    for key, script in (("salamander", "tools/piano/fetch_salamander.sh"), ("jrhodes", "tools/piano/fetch_rhodes.sh"), ("clavinet", "tools/piano/fetch_clavinet.sh")):
        if key in p: return f" (download the samples first: {script})"
    return " (download the samples first with the matching tools/piano/fetch_*.sh script, or check the path in the config)" if "/samples/" in p or p.startswith("samples/") or "/../samples/" in p else ""

# ------------------------------------------------------------------------------------------------ tools
SETUP_HELP = ("\nWhat to do:\n"
              "  macOS : run  xcode-select --install  in a Terminal (the compiler needs the Xcode Command Line Tools). If they are installed and the error stays,\n"
              "          after a macOS update the tools are often stale: sudo rm -rf /Library/Developer/CommandLineTools && xcode-select --install\n"
              "  Linux : sudo apt install build-essential   (or your distribution's equivalent)\n"
              "  Then run  python3 tools/check_setup.py  to check everything the tools need.")

def cxx_candidates():
    c = []
    if os.environ.get("CXX"): c.append(os.environ["CXX"])
    c += ["c++", "clang++", "g++"]; seen = []; out = []
    for x in c:
        w = shutil.which(x)
        if w and w not in seen: seen.append(w); out.append(w)
    return out

def compile_cpp(sources, out, includes=(), flags=("-O2", "-std=gnu++17"), what="the C++ helper"):
    """Compile and link C++ sources with whatever compiler works: $CXX, c++, clang++, g++; on macOS also with the SDK path from xcrun (fixes the
    'cstdio file not found' of a Command Line Tools install that is stale or points at no SDK). Raises a BankError that says what to install."""
    cands = cxx_candidates()
    if not cands: raise BankError(f"no C++ compiler found (needed to build {what})." + SETUP_HELP)
    variants = [[]]
    if sys.platform == "darwin":
        try:
            sdk = subprocess.run(["xcrun", "--show-sdk-path"], capture_output=True, text=True, timeout=20).stdout.strip()
            if sdk: variants.append(["-isysroot", sdk]); variants.append(["-isysroot", sdk, "-stdlib=libc++"])
        except (OSError, subprocess.SubprocessError): pass
    errs = []
    for cxx in cands:
        for v in variants:
            cmd = [cxx] + list(flags) + v + [f for inc in includes for f in ("-I", inc)] + list(sources) + ["-o", out]
            try: r = subprocess.run(cmd, capture_output=True, text=True, timeout=600)
            except (OSError, subprocess.SubprocessError) as e: errs.append(f"{cxx}: {e}"); continue
            if r.returncode == 0: return cxx
            errs.append(f"{os.path.basename(cxx)}{' ' + ' '.join(v) if v else ''}: " + " ".join(r.stderr.strip().splitlines()[:2])[:260])
    raise BankError(f"could not compile {what} with any C++ compiler.\n  " + "\n  ".join(errs[:4]) + SETUP_HELP)

def ensure_adpcm_tool():
    """the ADPCM encoder/decoder, compiled from tools/adpcm_tool.cpp + src/piano_adpcm.h. The file name carries a hash of both sources, so a stale binary is impossible."""
    src = ADPCM_SRC; hdr = os.path.join(SRC, "piano_adpcm.h")
    h = hashlib.md5(open(src, "rb").read() + open(hdr, "rb").read()).hexdigest()[:10]; exe = ADPCM_TOOL + "_" + h + (".exe" if sys.platform == "win32" else "")
    if not os.path.exists(exe): compile_cpp([src], exe, what="tools/adpcm_tool.cpp (the ADPCM encoder)")
    return exe

def adpcm(mode, data, pred0=0, idx0=0, n=0, hist=None):
    """run tools/adpcm_tool. enc: pred0 idx0 | encb: ... n = beam width | dec, state: ... n = number of samples. hist = the 3 samples before pred0 (newest first) when the stream continues after a PCM head"""
    args = [str(pred0), str(idx0)] + ([] if mode == "enc" else [str(n)]) + ([str(int(h)) for h in hist] if hist is not None else [])
    r = subprocess.run([ensure_adpcm_tool(), mode] + args, input=data, capture_output=True, check=True); return r.stdout

def read_params(src_dir=None):
    """The knob table is read from the firmware header, so the tool and the firmware can never disagree about parameter names or defaults."""
    t = open(os.path.join(src_dir or SRC, "piano_params.h")).read()
    cmds = re.findall(r'\{\s*"[^"]*",\s*"[^"]*",\s*"(\w+)",\s*\d+', t); m = re.search(r"PARAM_DEFAULT\[N_PARAMS\]\s*=\s*\{([^}]*)\}", t)
    defaults = [int(v) for v in m.group(1).split(",") if v.strip()]
    if len(cmds) != len(defaults): raise BankError(f"piano_params.h: {len(cmds)} parameters but {len(defaults)} defaults")
    return cmds, defaults

# ------------------------------------------------------------------------------------------------ audio
_cache = {}
def load_sample(path, onset=0.02, lead_ms=3.0):
    """decode + trim to the onset (3 ms of lead-in kept). Cached by file mtime."""
    if not os.path.isfile(path): raise BankError(f"sample file not found: {path}" + missing_hint(path))
    key = (os.path.abspath(path), os.path.getmtime(path), onset, lead_ms)
    if key in _cache: return _cache[key]
    r = subprocess.run(["ffmpeg", "-v", "error", "-i", path, "-f", "f32le", "-ac", "1", "-ar", str(FS), "-"], capture_output=True)
    if r.returncode or not r.stdout: raise BankError(f"ffmpeg could not decode {path}: {r.stderr.decode()[:200]}")
    x = np.frombuffer(r.stdout, dtype=np.float32).astype(np.float64)
    fr = subprocess.run(["ffprobe", "-v", "error", "-select_streams", "a:0", "-show_entries", "stream=sample_rate", "-of", "csv=p=0", path], capture_output=True, text=True).stdout.strip()
    pk = float(np.abs(x).max()); on = int(np.argmax(np.abs(x) > onset * pk)); st = max(0, on - int(lead_ms / 1000 * FS))
    d = dict(x=x[st:], peak=pk, natural=len(x[st:]) / FS, start=st, file_rate=int(fr) if fr.isdigit() else FS); _cache[key] = d; return d

def resample(x, r):
    p = subprocess.run(["ffmpeg", "-v", "error", "-f", "f32le", "-ar", str(FS), "-ac", "1", "-i", "-", "-af", "aresample=resampler=soxr:precision=28", "-ar", str(r), "-f", "f32le", "-"],
                       input=x.astype(np.float32).tobytes(), capture_output=True)
    if p.returncode: raise BankError("ffmpeg resample failed: " + p.stderr.decode()[:200])
    return np.frombuffer(p.stdout, dtype=np.float32).astype(np.float64)

def fundamental_cents(x, k):
    """cents of the sample's true fundamental relative to equal temperament A=440 (positive = sharp). Informational: playback keeps the recording's own tuning."""
    f_et = 440 * 2 ** ((k - 69) / 12); seg = x[int(0.15 * FS):int(3.15 * FS)] if len(x) > 3.2 * FS else x[int(0.05 * FS):]
    if len(seg) < 2048: return 0.0
    n = 1 << 19; sp = np.abs(np.fft.rfft(seg * np.hanning(len(seg)), n)); fr = np.fft.rfftfreq(n, 1 / FS); best = None
    for h in (1, 2, 3, 4):
        lo, hi = h * f_et * 2 ** (-150 / 1200), h * f_et * 2 ** (150 / 1200)
        if hi > FS / 2 - 500: break
        m = np.where((fr > lo) & (fr < hi))[0]
        if not len(m): continue
        i = m[np.argmax(sp[m])]; s = np.log(sp[[i - 1, i, i + 1]] + 1e-12); pf = (i + 0.5 * (s[0] - s[2]) / (s[0] - 2 * s[1] + s[2])) * FS / n
        med = np.median(sp[m])
        if best is None or sp[i] > 8 * med: best = pf / h
        if sp[i] > 8 * med: break
    return 0.0 if best is None else float(1200 * np.log2(best / f_et))

def find_loop(y, rate, root, min_s=0.25, max_s=1.0, xfade_ms=12.0):
    """Find a seamless loop at the END of y (the loop end is len(y)); returns (loop_start, xfade_len) or None.
    The waveform just before the loop end should resemble the waveform just before the loop start; the end is then crossfaded into it."""
    le = len(y); W = int(xfade_ms / 1000 * rate); lo_len = int(min_s * rate); hi_len = min(int(max_s * rate), int(le * 0.7) - W)
    if hi_len <= lo_len or le < lo_len + 2 * W: return None
    p = rate / (440 * 2 ** ((root - 69) / 12)); tail = y[le - W:le]; et = float((tail ** 2).sum()) + 1e-12
    def mismatch(ln):
        ls = le - ln
        if ls - W < 0: return 9e9
        seg = y[ls - W:ls]; return float(((tail - seg) ** 2).sum()) / (et + float((seg ** 2).sum()) + 1e-12)
    step = max(1, int(p / 16)); best = min(range(lo_len, hi_len, step), key=mismatch)
    best = min(range(max(lo_len, best - step), min(hi_len, best + step + 1)), key=mismatch)
    return le - best, W

def crossfade_loop(y, ls, W):
    le = len(y); y = y.copy(); a = np.arange(W) / W; y[le - W:le] = (1 - a) * y[le - W:le] + a * y[ls - W:ls]; return y

# ------------------------------------------------------------------------------------------------ config
def _rules_for(rules, root):
    for r in rules:
        if root <= parse_note(r.get("max_key", 127)): return r
    return rules[-1] if rules else {}

EXT_RANK = {".wav": 0, ".flac": 1, ".aiff": 2, ".aif": 3, ".mp3": 4, ".ogg": 5, ".m4a": 6}      # when a folder holds one note in several formats the best one is used

def expand_config(cfg, base_dir="."):
    """config dict -> list of instruments with fully resolved samples (key ranges, rates, durations ...)."""
    if "instruments" not in cfg or not cfg["instruments"]: raise BankError("config has no instruments")
    cmds, pdef = read_params(); out = []
    for ii, ic in enumerate(cfg["instruments"]):
        name = str(ic.get("name", f"Instrument {ii + 1}"))[:12]
        rules = ic.get("rate_rules") or cfg.get("rate_rules") or [dict(max_key=127, rate=24000, maxdur=6.0)]
        defaults = list(pdef)
        for k, v in (ic.get("defaults") or {}).items():
            if k not in cmds: raise BankError(f"instrument {name}: unknown default '{k}' (known: {', '.join(cmds)})")
            defaults[cmds.index(k)] = int(v)
        raw = []; notes_out = []
        for imp in ic.get("import", []) or []:
            d = os.path.join(base_dir, imp["dir"]) if not os.path.isabs(imp["dir"]) else imp["dir"]; pat = imp.get("pattern", "{note}.*")
            rx = re.compile("^" + re.escape(pat).replace(r"\{note\}", r"(?P<note>[A-Ga-g][#sSb]?-?\d+)").replace(r"\{midi\}", r"(?P<midi>\d+)").replace(r"\*", ".*") + "$")
            files = sorted(os.listdir(d)) if os.path.isdir(d) else []
            if not files: raise BankError(f"import directory not found or empty: {os.path.normpath(d)}" + missing_hint(d))
            found = {}                                           # root -> file name: ONE file per note per import entry
            for fn in files:
                m = rx.match(fn)
                if not m: continue
                root = parse_note(m.group("note")) if "note" in m.groupdict() and m.group("note") else int(m.group("midi"))
                if root in found:
                    old = found[root]; keep_new = EXT_RANK.get(os.path.splitext(fn)[1].lower(), 9) < EXT_RANK.get(os.path.splitext(old)[1].lower(), 9)
                    notes_out.append(f"{name}: two files for {note_name(root)} in {os.path.basename(d.rstrip('/'))}: using {fn if keep_new else old}, ignoring {old if keep_new else fn}")
                    if keep_new: found[root] = fn
                else: found[root] = fn
            for root, fn in sorted(found.items()):
                s = dict(file=os.path.join(d, fn), root=root); s.update({k: v for k, v in imp.items() if k not in ("dir", "pattern")}); raw.append(s)
        sfz_entries = ic.get("sfz") or []
        for e in ([sfz_entries] if isinstance(sfz_entries, (str, dict)) else sfz_entries):
            e = {"file": e} if isinstance(e, str) else dict(e)                                  # "file", and optionally "loop": auto|none and "max_layers": N
            p = e["file"] if os.path.isabs(e["file"]) else os.path.join(base_dir, e["file"])
            if not os.path.isfile(p): raise BankError(f"SFZ file not found: {os.path.normpath(p)}" + missing_hint(p))
            try: regs = sfzmod.parse(p)
            except (OSError, ValueError) as ex: raise BankError(f"cannot read the SFZ file {p}: {ex}")
            if e.get("max_layers"): regs = limit_layers(regs, int(e["max_layers"]))
            if e.get("every"): regs = thin_roots(regs, int(e["every"]))
            for r in regs:
                s = dict(file=r["file"], root=r["root"], lo=r["lo"], hi=r["hi"], vlo=r["vlo"], vhi=r["vhi"], gain=r["volume"], loop_sfz=r["loop_sfz"])
                if e.get("loop") in ("auto", "none"): s["loop"] = e["loop"]                      # many SFZ sets keep their loop points inside the audio files: let the tool find loops
                raw.append(s)
        for s in ic.get("samples", []) or []:
            s = dict(s)
            if not os.path.isabs(s["file"]): s["file"] = os.path.join(base_dir, s["file"])
            raw.append(s)
        if not raw: raise BankError(f"instrument {name} has no samples")
        samples = []
        for s in raw:
            root = parse_note(s["root"]); r = _rules_for(rules, root)
            loop = s.get("loop") or ("sfz" if s.get("loop_sfz") else "none")
            if loop == "sfz" and not s.get("loop_sfz"): loop = "none"
            samples.append(dict(file=s["file"], root=root, lo=(parse_note(s["lo"]) if s.get("lo") not in (None, "") else None), hi=(parse_note(s["hi"]) if s.get("hi") not in (None, "") else None),
                                vlo=int(s.get("vlo") or 1), vhi=int(s.get("vhi") or 127), vref=(s.get("vref") or None), gain=float(s.get("gain") or 0.0), loop=loop, loop_sfz=s.get("loop_sfz"),
                                rate=int(s.get("rate") or r.get("rate", 24000)), maxdur=float(s.get("maxdur") or r.get("maxdur", 6.0))))
        _auto_ranges(samples)
        for s in samples:
            if s["vref"] is None: s["vref"] = 80 if (s["vlo"] <= 1 and s["vhi"] >= 127) else (s["vlo"] + s["vhi"]) // 2
            s["vref"] = int(max(1, min(127, s["vref"]))); s["name"] = os.path.splitext(os.path.basename(s["file"]))[0]
        samples.sort(key=lambda s: (s["vlo"], s["root"]))
        out.append(dict(name=name, notes=notes_out, credit=str(ic.get("credit", "")), defaults=defaults, gain=float(ic.get("gain", 0.0)), samples=samples))
    return out

def thin_roots(regs, n):
    """keep every n-th root of each velocity layer (always the lowest and the highest) and let the key ranges split automatically: a sample every semitone is overkill when notes can shift a semitone or two"""
    out = []
    for lay in sorted({(r["vlo"], r["vhi"]) for r in regs}):
        g = sorted([r for r in regs if (r["vlo"], r["vhi"]) == lay], key=lambda r: r["root"]); keep = {i for i in range(0, len(g), max(1, n))} | {len(g) - 1}
        out += [dict(g[i], lo=None, hi=None) for i in sorted(keep)]
    return out

def limit_layers(regs, n):
    """keep n evenly spaced velocity layers of an SFZ and stretch them so that together they cover 1..127 (boundaries halfway between the layers' middle velocities)"""
    layers = sorted({(r["vlo"], r["vhi"]) for r in regs})
    if n >= len(layers) or n < 1: return regs
    keep = [layers[int(round(i * (len(layers) - 1) / (n - 1)))] if n > 1 else layers[len(layers) // 2] for i in range(n)]; keep = sorted(set(keep))
    mid = [(a + b) // 2 for a, b in keep]; newr = {}
    for i, k in enumerate(keep): newr[k] = (1 if i == 0 else (mid[i - 1] + mid[i]) // 2 + 1, 127 if i == len(keep) - 1 else (mid[i] + mid[i + 1]) // 2)
    out = []
    for r in regs:
        k = (r["vlo"], r["vhi"])
        if k in newr: r = dict(r, vlo=newr[k][0], vhi=newr[k][1]); out.append(r)
    return out

def _auto_ranges(samples):
    """within each velocity layer, split the keyboard at the midpoints between neighbouring roots (ties go to the lower root)"""
    layers = {}
    for s in samples: layers.setdefault((s["vlo"], s["vhi"]), []).append(s)
    for grp in layers.values():
        grp.sort(key=lambda s: s["root"])
        for i, s in enumerate(grp):
            if s["lo"] is None: s["lo"] = 0 if i == 0 else (grp[i - 1]["root"] + s["root"]) // 2 + 1
            if s["hi"] is None: s["hi"] = 127 if i == len(grp) - 1 else (s["root"] + grp[i + 1]["root"]) // 2

def coverage(samples):
    """for every key 0..127: the index of the sample that would play for each velocity layer (None = a gap)"""
    layers = sorted({(s["vlo"], s["vhi"]) for s in samples}); res = {}
    for key in range(128):
        row = []
        for (vl, vh) in layers:
            c = [i for i, s in enumerate(samples) if (s["vlo"], s["vhi"]) == (vl, vh) and s["lo"] <= key <= s["hi"]]
            row.append(min(c, key=lambda i: (abs(samples[i]["root"] - key), samples[i]["root"])) if c else None)
        res[key] = row
    return layers, res

def check_config(instruments):
    warns = []
    for ins in instruments:
        warns.extend(ins.get("notes", []))
        seen = {}
        for q in ins["samples"]:
            k = (q["root"], q["vlo"], q["vhi"])
            if k in seen: warns.append(f"{ins['name']}: two samples for {note_name(q['root'])} in the same velocity layer ({seen[k]} and {q['name']}): only one can play")
            seen.setdefault(k, q["name"])
        if len(ins["credit"]) > 38: warns.append(f"{ins['name']}: the credit line is {len(ins['credit'])} characters; the screen shows 38")
        S = ins["samples"]; layers, cov = coverage(S)
        gaps = [k for k in range(21, 109) if any(c is None for c in cov[k])]
        if gaps: warns.append(f"{ins['name']}: no sample for keys {note_name(gaps[0])}..{note_name(gaps[-1])} ({len(gaps)} keys) in at least one velocity layer")
        for v in range(1, 128, 1):
            if not any(s["vlo"] <= v <= s["vhi"] for s in S): warns.append(f"{ins['name']}: no velocity layer covers velocity {v}"); break
        for s in S:
            for key in range(max(21, s["lo"]), min(108, s["hi"]) + 1):
                if cov[key][layers.index((s["vlo"], s["vhi"]))] != S.index(s): continue
                inc = s["rate"] / FS * 2 ** ((key - s["root"]) / 12)
                if inc >= 1.0:
                    warns.append(f"{ins['name']}/{s['name']}: key {note_name(key)} is shifted {key - s['root']:+d} semitones at {s['rate']} Hz: more than one ADPCM sample per output sample (extra CPU, more aliasing)"); break
            keys = [key for key in range(21, 109) if cov[key][layers.index((s["vlo"], s["vhi"]))] == S.index(s)]
            if keys and max(abs(k - s["root"]) for k in keys) > 4:
                warns.append(f"{ins['name']}/{s['name']}: plays keys up to {max(abs(k - s['root']) for k in keys)} semitones from its root: large pitch shifts sound unnatural")
    return warns

# ------------------------------------------------------------------------------------------------ budget
def head_len(rate, head_ms, n_total=None):
    """the number of PCM head samples of a zone (0 = none)"""
    H = int(round(head_ms / 1000.0 * rate)) if head_ms and head_ms > 0 else 0
    if n_total is not None: H = min(H, n_total // 2)
    return H if H >= 8 else 0

def _size(s, info, scale, head_ms=0.0):
    if s["loop"] == "sfz":
        n = int((s["loop_sfz"][1] - info["start"] * info["file_rate"] / FS) / info["file_rate"] * s["rate"])
    else: n = int(min(info["natural"], s["maxdur"] * scale) * s["rate"])
    H = head_len(s["rate"], head_ms, n)
    return int(n / 2) + (1 if s["loop"] == "sfz" else 0) + int(1.5 * H)                 # a head sample costs 2 bytes instead of half a byte

def solve_scale(instruments, budget, infos, head_ms=0.0):
    flat = [(s, infos[s["file"]]) for ins in instruments for s in ins["samples"]]
    def total(scale): return sum(_size(s, i, scale, head_ms) for s, i in flat)
    lo, hi = 0.2, 1.5
    if total(lo) > budget: lo = 0.001
    if total(hi) < budget:                                                      # the budget is not binding: take the full lengths (scale up until nothing grows)
        while total(hi) < budget and hi < 64: hi *= 2
        if total(hi) < budget: return hi, total(hi)
    for _ in range(40):
        mid = (lo + hi) / 2; lo, hi = (mid, hi) if total(mid) < budget else (lo, mid)
    return lo, total(lo)

def load_all(instruments):
    infos = {}
    for ins in instruments:
        for s in ins["samples"]:
            if s["file"] not in infos: infos[s["file"]] = load_sample(s["file"])
    return infos

def estimate(cfg, base_dir="."):
    """fast: sizes and durations from the decoded lengths, without resampling or encoding"""
    instruments = expand_config(cfg, base_dir); infos = load_all(instruments); budget = int(cfg.get("budget", 3000000))
    head_ms = float(cfg.get("pcm_head_ms", 9.0)); scale, tot = solve_scale(instruments, budget, infos, head_ms); rows = []
    for ii, ins in enumerate(instruments):
        for s in ins["samples"]:
            n = _size(s, infos[s["file"]], scale, head_ms); rows.append(dict(instrument=ii, name=s["name"], root=s["root"], bytes=n, dur=2 * n / s["rate"], natural=infos[s["file"]]["natural"], rate=s["rate"]))
    per = [sum(r["bytes"] for r in rows if r["instrument"] == i) for i in range(len(instruments))]
    return dict(budget=budget, total=tot, scale=scale, per_instrument=per, samples=rows, warnings=check_config(instruments))

# ------------------------------------------------------------------------------------------------ build
LEVEL_VEL = 80                                              # the velocity at which instruments are level-matched
LEVEL_WINDOW = 0.3                                          # seconds: loudness is measured on the first 300 ms (the hit and its body): a sustaining Rhodes and a decaying piano compare fairly there, not over a whole second

def _rms_db(x): return float(20 * np.log10(np.sqrt((x ** 2).mean()) + 1e-9))

def build(cfg, base_dir=".", progress=None):
    """full build. Returns a report dict (zones, instruments, header text, data bytes ...). Nothing is written: see write_outputs().
    Config keys: budget, normalize_peak (0.92), normalize: zone|instrument, layer_levels: law|natural, level_match: false|auto|<dBFS>, pcm_head_ms (9), adpcm_beam (0 = greedy)."""
    cmds, pdef = read_params(); instruments = expand_config(cfg, base_dir); infos = load_all(instruments); budget = int(cfg.get("budget", 3000000)); peak_t = float(cfg.get("normalize_peak", 0.92))
    beam = int(cfg.get("adpcm_beam", 0))                       # 0 = the greedy IMA-style encoder; N = a beam search of N paths (about +2.5 dB SNR at 16, the same decoder, no cost on the Pico)
    layer_levels = str(cfg.get("layer_levels", "law"))         # law: ONE loudness curve for all instruments and layers, (velocity/80)^1.4 (layers supply timbre only; right for normalised sets such as the clavinet);
    if layer_levels not in ("law", "natural"): raise BankError(f"layer_levels must be 'law' or 'natural', not {layer_levels!r}")      # natural: each layer keeps the level it was recorded at, and a layer's loudness rises with (velocity/vref)^1.4 inside it (right for sets with real level differences between layers)
    norm = str(cfg.get("normalize", "zone"))                   # zone: every zone is stored at full scale and its level restored by the zone gain at run time (better SNR for quiet zones); instrument: one gain per instrument (the old way)
    head_ms = float(cfg.get("pcm_head_ms", 9.0))               # the first milliseconds of every zone (the hammer strike) are stored as 16-bit PCM
    if norm not in ("zone", "instrument"): raise BankError(f"normalize must be 'zone' or 'instrument', not {norm!r}")
    scale, _ = solve_scale(instruments, budget, infos, head_ms); blob = bytearray(); zones = []; zi = 0; ntot = sum(len(i["samples"]) for i in instruments); warns = check_config(instruments)
    # ---- level matching: the measured loudness (RMS of the first second, at the old per-instrument scaling) of every instrument, and the gain that brings them together
    G = [peak_t / max(infos[q["file"]]["peak"] for q in ins["samples"]) for ins in instruments]
    lay = lambda q: (q["vlo"], q["vhi"])
    GL = [{l: peak_t / max(infos[q["file"]]["peak"] for q in ins["samples"] if lay(q) == l) for l in {lay(q) for q in ins["samples"]}} for ins in instruments]      # the same scaling per velocity layer
    LAW_P = 1.404; Gref = lambda ii, q: GL[ii][lay(q)] if layer_levels == "law" else G[ii]
    law_db = lambda q: (LAW_P * 20 * np.log10(max(1, q["vref"]) / LEVEL_VEL)) if layer_levels == "law" else 0.0                              # the layer is played at (v/vref)^p inside it, so it is recorded 'as if at vref' on the common curve
    def _ref_zones(ins):                                    # level matching compares what plays at a normal playing velocity (80): the zones of the velocity layer that contains it
        z = [q for q in ins["samples"] if q["vlo"] <= LEVEL_VEL <= q["vhi"]]; return z or ins["samples"]
    def _zone_level(q, ii):                                 # RMS of the first second at the instrument's scaling, plus (natural layers) what the velocity law does to a note at LEVEL_VEL in this layer: (80 / vref) ^ 1.4 (the default HARD)
        if layer_levels == "law": return _rms_db(infos[q["file"]]["x"][:int(LEVEL_WINDOW * FS)] * GL[ii][lay(q)])          # on the common curve every layer plays at the same level at velocity 80
        return _rms_db(infos[q["file"]]["x"][:int(LEVEL_WINDOW * FS)] * G[ii]) + 1.404 * 20 * np.log10(LEVEL_VEL / max(1, q["vref"]))
    levels = [float(np.median([_zone_level(q, ii) for q in _ref_zones(ins)])) for ii, ins in enumerate(instruments)]
    lm = cfg.get("level_match", False); offsets = [0.0] * len(instruments)
    if lm not in (False, None, "off", "none") and len(instruments) >= 1:
        target = float(np.median(levels)) if lm in (True, "auto") else float(lm)
        offsets = [max(-12.0, min(12.0, target - L)) for L in levels]
        for ii, (L, o) in enumerate(zip(levels, offsets)):
            if abs(target - L) > 12.0: warns.append(f"{instruments[ii]['name']}: level matching wanted {target - L:+.1f} dB but is limited to +-12 dB")
    for ii, ins in enumerate(instruments):
        for s in ins["samples"]:
            if progress: progress(zi, ntot, f"{ins['name']}: {s['name']}")
            info = infos[s["file"]]; R = s["rate"]; k = s["root"]; xt = info["x"]; loop_ls = loop_le = 0; lh = [0, 0, 0, 0]; lw = [0, 0, 0, 0]; lidx = 0
            def zone_gain(x_raw):                              # (gain applied to the stored data, the run-time gain in dB that restores the old per-instrument level)
                pk = float(np.abs(x_raw).max()) + 1e-12
                gr = Gref(ii, s); return (peak_t / pk, 20 * np.log10(gr / (peak_t / pk))) if norm == "zone" else (gr, 0.0)
            if s["loop"] == "sfz":
                le44 = int(round((s["loop_sfz"][1] - info["start"] * info["file_rate"] / FS) * FS / info["file_rate"])); ls44 = int(round((s["loop_sfz"][0] - info["start"] * info["file_rate"] / FS) * FS / info["file_rate"]))
                x_raw = xt[:max(le44, 64)]; g, comp_db = zone_gain(x_raw); y = resample(x_raw * g, R); loop_le = len(y); loop_ls = max(0, int(round(ls44 * R / FS)))
                if loop_ls < int(0.012 * R) + 1: loop_ls = int(0.012 * R) + 1
                y = crossfade_loop(y, loop_ls, int(0.012 * R))
            else:
                D = min(info["natural"], s["maxdur"] * scale); x_raw = xt[:int(D * FS)]; g, comp_db = zone_gain(x_raw); x = x_raw * g
                if s["loop"] == "auto":
                    y = resample(x, R); fl = find_loop(y, R, k)
                    if fl is None: warns.append(f"{ins['name']}/{s['name']}: no seamless loop found (sample too short); it plays once"); y = y.copy(); m = int(min(1.5, 0.25 * D) * R); y[-m:] *= 0.5 * (1 + np.cos(np.linspace(0, np.pi, m)))
                    else: loop_ls, W = fl; loop_le = len(y); y = crossfade_loop(y, loop_ls, W)
                else:
                    x = x.copy(); fl = int(min(1.5, 0.25 * D) * FS); x[-fl:] *= 0.5 * (1 + np.cos(np.linspace(0, np.pi, fl))); y = resample(x, R)
            y16 = np.clip(np.round(y * 32767), -32768, 32767).astype(np.int16); n = len(y16); pred0 = int(y16[0])
            H = head_len(R, head_ms, n)
            if loop_le: H = min(H, max(0, loop_ls - 1)) if loop_ls > 8 else 0          # a loop jump lands in the ADPCM part
            if H < 8: H = 0
            tail = y16[H:]; start = int(y16[H - 1]) if H else pred0; hist = [int(y16[H - 2]), int(y16[H - 3]), int(y16[H - 4])] if H else None
            # the step index the ADPCM part starts at: the strike is loud at the handover, so the default index 0 would start far too fine; pick the best of a few by the error of a quick greedy pass
            idx0 = 0
            if H:
                probe = tail[:int(0.15 * R)]; best = None
                for cand in (0, 20, 35, 50, 65):
                    e_ = adpcm("enc", probe.tobytes(), start, cand, hist=hist); d_ = np.frombuffer(adpcm("dec", e_, start, cand, len(probe), hist=hist), dtype=np.int16).astype(float); err = float(((probe.astype(float) - d_) ** 2).sum())
                    if best is None or err < best[0]: best = (err, cand)
                idx0 = best[1]
            enc = adpcm("encb", tail.tobytes(), start, idx0, beam, hist) if beam > 0 else adpcm("enc", tail.tobytes(), start, idx0, hist=hist)
            if loop_le:
                v = [int(t) for t in adpcm("state", enc, start, idx0, loop_ls - H, hist).decode().split()]; lh, lw, lidx = v[0:4], v[4:8], v[8]      # the full decoder state at the loop start: history, predictor weights, step index
            dec = np.concatenate([y16[:H].astype(float), np.frombuffer(adpcm("dec", enc, start, idx0, len(tail), hist), dtype=np.int16).astype(float)])
            snr = 10 * np.log10((y16.astype(float) ** 2).sum() / (((y16 - dec) ** 2).sum() + 1e-9))
            while len(blob) % 4: blob.append(0)
            head_bytes = y16[:H].astype("<i2").tobytes()
            zones.append(dict(instrument=ii, name=s["name"], file=s["file"], root=k, lo=s["lo"], hi=s["hi"], vlo=s["vlo"], vhi=s["vhi"], vref=s["vref"], off=len(blob), n=n, rate=R, head=H, pred0=pred0, idx0=idx0,
                              loopStart=loop_ls, loopEnd=loop_le, lh=lh, lw=lw, lidx=lidx, gain10=int(round(10 * (ins["gain"] + s["gain"] + offsets[ii] + comp_db + law_db(s)))), cents=round(fundamental_cents(xt, k), 2) if len(xt) > FS else 0.0,
                              dur=round(n / R, 2), natural=round(info["natural"], 2), bytes=len(head_bytes) + len(enc), snr=round(float(snr), 1)))
            blob += head_bytes + enc; zi += 1
    inst_rows = []; first = 0
    for ii, ins in enumerate(instruments):
        cnt = sum(1 for z in zones if z["instrument"] == ii); inst_rows.append(dict(name=ins["name"], credit=ins["credit"], zoneFirst=first, zoneCount=cnt, defaults=ins["defaults"], bytes=sum(z["bytes"] for z in zones if z["instrument"] == ii), level_db=round(levels[ii], 1), level_offset_db=round(offsets[ii], 1))); first += cnt
    if scale < 0.2: warns.append(f"the budget is very tight: every sample is cut to {scale * 100:.0f}% of its intended length ({budget:,} bytes for {len(zones)} zones): raise the budget, drop samples, or lower the sample rates")
    for z in zones:
        if z['snr'] < 20: warns.append(f"{instruments[z['instrument']]['name']}/{z['name']}: ADPCM SNR is only {z['snr']} dB (bright or noisy content suffers at 4 bits): use a higher sample rate or a shorter length")
    if len(blob) > budget * 1.02: warns.append(f"bank is {len(blob)} bytes, over the budget of {budget}")
    return dict(zones=zones, instruments=inst_rows, total=len(blob), budget=budget, scale=scale, params=cmds, blob=bytes(blob), warnings=warns, normalize=norm, layer_levels=layer_levels, pcm_head_ms=head_ms)

def header_text(rep):
    nd = len(rep["params"]); z = rep["zones"]
    h = "#pragma once\n// GENERATED by tools/bankconf (bank format v3): do not edit. See NOTICE.md for the sample licences.\n#include <stdint.h>\n"
    h += f"#define PIANO_ZONES {len(z)}\n#define PIANO_N_INSTRUMENTS {len(rep['instruments'])}\n#define PIANO_DATA_BYTES {rep['total']}\n#define PIANO_N_DEFAULTS {nd}\n#define PIANO_CODEC 3            // the codec and bank layout this bank was built for (src/piano_adpcm.h)\n"
    h += ("struct PianoZone { uint32_t off, n, loopStart, loopEnd; int16_t pred0, lh[4], lw[4]; uint16_t rate, head; uint8_t idx0, lidx, root, lo, hi, vlo, vhi, vref; int16_t gain10, cents10; };\n"
          "// off: byte offset of the zone's data = `head` samples of 16-bit PCM (the strike) followed by the ADPCM stream; n: samples in all (== loopEnd for a looped zone); pred0/idx0: decoder start state (the predictor history starts as pred0 and the weights as 1,0,0,0); lh/lw/lidx: the full decoder state at loopStart; lpred/lidx: decoder state at loopStart;\n"
          "// lo..hi key range, vlo..vhi velocity range, vref: the velocity the zone was recorded at; gain10: zone gain in tenths of a dB; cents10: measured tuning (information)\n"
          "// credit: the line shown at the bottom of the screen while this instrument is selected (at most 38 characters)\n"
          "struct PianoInstrument { const char* name; const char* credit; uint16_t zoneFirst, zoneCount; uint8_t def[PIANO_N_DEFAULTS]; };\n")
    h += "static const PianoZone PIANO_ZONE[PIANO_ZONES] = {\n"
    for q in z: h += f"    {{ {q['off']}u, {q['n']}u, {q['loopStart']}u, {q['loopEnd']}u, {q['pred0']}, {{ {', '.join(str(t) for t in q['lh'])} }}, {{ {', '.join(str(t) for t in q['lw'])} }}, {q['rate']}, {q['head']}, {q['idx0']}, {q['lidx']}, {q['root']}, {q['lo']}, {q['hi']}, {q['vlo']}, {q['vhi']}, {q['vref']}, {q['gain10']}, {int(round(q['cents'] * 10))} }},   // {q['name']}  {q['dur']} s  {q['snr']} dB{'  LOOP' if q['loopEnd'] else ''}\n"
    h += "};\nstatic const PianoInstrument PIANO_INSTRUMENT[PIANO_N_INSTRUMENTS] = {\n"
    for i in rep["instruments"]: h += f"    {{ \"{i['name']}\", \"{i['credit'].replace(chr(92), '').replace(chr(34), chr(39))}\", {i['zoneFirst']}, {i['zoneCount']}, {{ {', '.join(str(v) for v in i['defaults'])} }} }},\n"
    return h + "};\nextern const uint8_t PIANO_DATA[PIANO_DATA_BYTES];\n"

def data_text(rep):
    b = rep["blob"]; s = '// GENERATED by tools/bankconf: the ADPCM sample data (lives in flash, read in place).\n#include "piano_bank.h"\nconst uint8_t PIANO_DATA[PIANO_DATA_BYTES] = {\n'
    for i in range(0, len(b), 32): s += ",".join(str(v) for v in b[i:i + 32]) + ",\n"
    return s + "};\n"

def write_outputs(rep, src_dir, gen_dir=None):
    open(os.path.join(src_dir, "piano_bank.h"), "w").write(header_text(rep)); open(os.path.join(src_dir, "piano_data.cpp"), "w").write(data_text(rep))
    if gen_dir:
        os.makedirs(gen_dir, exist_ok=True); open(os.path.join(gen_dir, "bank.bin"), "wb").write(rep["blob"])
        j = {k: v for k, v in rep.items() if k != "blob"}; json.dump(j, open(os.path.join(gen_dir, "bank.json"), "w"), indent=1)
