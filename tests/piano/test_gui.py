# Tests of the bank GUI: the Flask API against real builds (including audition through the real engine) and the frontend script in Node.
import sys, os, io, json, time, wave, hashlib, shutil, tempfile, subprocess, re
ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..')); sys.path.insert(0, os.path.join(ROOT, 'tools'))
import numpy as np, yaml
from bankconf import core
from bankconf.app import create_app
FS = 44100; ok = True
def check(name, cond, detail=""):
    global ok; ok &= bool(cond); print(("PASS " if cond else "FAIL ") + name + (("  " + detail) if detail else ""))
def wav(path, x):
    w = wave.open(path, 'wb'); w.setnchannels(1); w.setsampwidth(2); w.setframerate(FS); w.writeframes((np.clip(x, -1, 1) * 32767).astype(np.int16).tobytes()); w.close()
def tone(f, dur, harm, tau, noise=0.0):
    t = np.arange(int(dur * FS)) / FS; x = sum((1.0 / h) * np.sin(2 * np.pi * f * h * t + 0.3 * h) for h in range(1, harm + 1)); env = np.exp(-t / tau) * np.minimum(1, t / 0.004) if tau else np.minimum(1, t / 0.02)
    return 0.3 * x / harm ** 0.5 * env + noise * np.random.RandomState(1).randn(len(t)) * np.exp(-t / 0.03)
T = tempfile.mkdtemp(prefix="gui_test_"); S = os.path.join(T, "samples"); os.makedirs(S)
for root in (48, 60, 72):
    f = 440 * 2 ** ((root - 69) / 12); wav(f"{S}/soft_{root}.wav", tone(f, 3.0, 3, 1.2)); wav(f"{S}/loud_{root}.wav", 1.8 * tone(f, 3.0, 12, 1.0, 0.02)); wav(f"{S}/organ_{root}.wav", tone(f, 2.5, 8, None))
open(f"{S}/organ.sfz", "w").write("<global> default_path=./\n<group> lovel=1 hivel=127\n" + "".join(f"<region> sample=organ_{r}.wav pitch_keycenter={r} lokey={max(0, r - 6) if r > 48 else 0} hikey={r + 5 if r < 72 else 127} loop_mode=loop_continuous loop_start={int(0.6 * FS)} loop_end={int(2.0 * FS)}\n" for r in (48, 60, 72)))
cfg = yaml.safe_load(f"""
budget: 600000
normalize_peak: 0.9
instruments:
  - name: Layers
    rate_rules: [{{max_key: 127, rate: 22050, maxdur: 2.8}}]
    samples: [{{file: {S}/soft_48.wav, root: 48, vlo: 1, vhi: 70}}, {{file: {S}/soft_60.wav, root: 60, vlo: 1, vhi: 70}}, {{file: {S}/soft_72.wav, root: 72, vlo: 1, vhi: 70}},
              {{file: {S}/loud_48.wav, root: 48, vlo: 71, vhi: 127, gain: -3}}, {{file: {S}/loud_60.wav, root: 60, vlo: 71, vhi: 127, gain: -3}}, {{file: {S}/loud_72.wav, root: 72, vlo: 71, vhi: 127, gain: -3}}]
  - name: Organ
    rate_rules: [{{max_key: 127, rate: 16000, maxdur: 1.5}}]
    import: [{{dir: {S}, pattern: "organ_{{midi}}.wav", loop: auto}}]
  - name: Sine
    rate_rules: [{{max_key: 127, rate: 16000, maxdur: 1.0}}]
    samples: [{{file: {S}/soft_60.wav, root: C4}}]
""")
app = create_app(None, workdir=os.path.join(T, "work")); c = app.test_client()

# ---- page ----
r = c.get("/"); html = r.get_data(as_text=True)
check("GET / serves the page", r.status_code == 200 and "bank configurator" in html)
ids = set(re.findall(r'\$\("(\w+)"\)', html)); have = set(re.findall(r'id="(\w+)"', html)); dyn = {"iname", "igain", "ibudget", "idel"}
check("every element id the script looks up exists in the page (typos caught)", not (ids - have - dyn), str(ids - have - dyn))
js = re.search(r"<script>(.*)</script>", html, re.S).group(1); open(os.path.join(T, "gui.js"), "w").write(js); r = subprocess.run(["node", "--check", os.path.join(T, "gui.js")], capture_output=True, text=True)
check("the page's script is valid JavaScript", r.returncode == 0, r.stderr[:200])

# ---- state ----
bad = json.loads(json.dumps(cfg)); bad["instruments"][0]["defaults"] = {"nonsense": 3}; r = c.post("/api/state", json={"cfg": bad, "base": T})
check("state: an invalid config is rejected with the reason", r.status_code == 400 and "unknown default" in r.json["error"], r.json.get("error", "")[:80])
r = c.post("/api/state", json={"cfg": cfg, "base": T, "path": os.path.join(T, "c.yaml")}); check("state: a valid config is accepted", r.status_code == 200)
st = c.get("/api/state").json; check("state: GET returns the config, the 16 firmware parameter names and their defaults", st["cfg"]["instruments"][0]["name"] == "Layers" and len(st["params"]) == 16 and st["params"][0] == "vol" and len(st["param_defaults"]) == 16 and not st["has_build"])

# ---- estimate ----
e = c.post("/api/estimate", json={}).json
check("estimate: sizes of the three instruments add up to the total; one row per sample", sum(e["per_instrument"]) == e["total"] or abs(sum(e["per_instrument"]) - e["total"]) < 3, f"{e['per_instrument']} vs {e['total']}")
check("estimate: resolved samples carry the automatic key ranges and reference velocities", e["instruments"][0]["samples"][0]["lo"] == 0 and e["instruments"][0]["samples"][0]["hi"] == 54 and e["instruments"][0]["samples"][3]["vref"] == 99)
i0 = e["instruments"][0]; check("estimate: coverage map = 88 keys x velocity layers; layer boundaries reported", len(i0["keys"]) == 88 and i0["layers"] == [[1, 70], [71, 127]] and i0["keys"]["50"] == [0, 3] and i0["keys"]["100"] == [2, 5])
g = c.post("/api/estimate", json={"cfg": {"budget": 1000000, "instruments": [{"name": "g", "samples": [{"file": f"{S}/soft_60.wav", "root": 60, "lo": 55, "hi": 65}]}]}}).json
check("estimate: unmapped keys show as gaps (null) and as a warning", g["instruments"][0]["keys"]["30"] == [None] and any("no sample for keys" in w for w in g["warnings"]))
r = c.post("/api/estimate", json={"cfg": {"instruments": [{"name": "x", "samples": [{"file": f"{T}/nope.wav", "root": 60}]}]}}); check("estimate: a missing sample file is a clear 400", r.status_code == 400 and "not found" in r.json["error"])
r = c.post("/api/estimate", json={"cfg": {"instruments": []}}); check("estimate: an empty config is fine (zero bytes)", r.status_code == 200 and r.json["total"] == 0)

# ---- import ----
r = c.post("/api/import", json={"base": T, "dir": S, "pattern": "organ_{midi}.wav", "loop": "auto", "rate": 16000, "maxdur": 1.5}).json
check("import (folder + pattern): three samples found by MIDI number, loop setting and rate applied, key ranges left automatic", [x["root"] for x in r["samples"]] == ["C3", "C4", "C5"] and all(x["loop"] == "auto" and x["rate"] == 16000 and "lo" not in x for x in r["samples"]))
imp_dir = r; r = c.post("/api/import", json={"base": T, "sfz": f"{S}/organ.sfz", "rate": 16000, "maxdur": 1.5}).json
check("import (SFZ): the SFZ's key ranges and loop points come through", len(r["samples"]) == 3 and r["samples"][0]["lo"] == "C-1" and r["samples"][0]["loop"] == "sfz" and r["samples"][0]["loop_sfz"] == [int(0.6 * FS), int(2.0 * FS)] and r["samples"][1]["lo"] == "F#3", str(r["samples"][1]["lo"]))
imp_sfz = r
check("import: a folder that does not exist and an SFZ that does not exist are 400s with a message", c.post("/api/import", json={"dir": f"{T}/none"}).status_code == 400 and c.post("/api/import", json={"sfz": f"{T}/none.sfz"}).status_code == 400)

# ---- browse / waveform / save / load ----
b = c.get("/api/browse", query_string={"path": S}).json
check("browse: lists files with audio / sfz flags, hides dotfiles", b["path"] == S and any(f["name"] == "organ.sfz" and f["sfz"] for f in b["files"]) and any(f["name"] == "soft_48.wav" and f["audio"] for f in b["files"]))
check("browse: a file path lists its folder; a missing folder is a 404", c.get("/api/browse", query_string={"path": f"{S}/soft_48.wav"}).json["path"] == S and c.get("/api/browse", query_string={"path": f"{T}/nothere"}).status_code == 404)
w = c.get("/api/waveform", query_string={"file": f"{S}/loud_60.wav", "points": 400}).json
check("waveform: min/max envelope of the trimmed sample with its duration and peak", len(w["lo"]) == len(w["hi"]) >= 300 and all(h >= l for h, l in zip(w["hi"], w["lo"])) and w["peak"] > 0.3 and 2.5 < w["dur"] < 3.1 and w["loop"] is None)
check("waveform: a missing file is a 404", c.get("/api/waveform", query_string={"file": f"{T}/nope.wav"}).status_code == 404)
p = os.path.join(T, "saved.yaml"); c.post("/api/save", json={"path": p, "cfg": cfg}); lo = c.post("/api/load", json={"path": p}).json
check("save / load round trip gives the same config", lo["cfg"] == cfg and os.path.exists(p))
open(f"{T}/bad.yaml", "w").write("foo: 1\n"); check("load: a YAML file that is not a bank config, and a missing file, are 400s", c.post("/api/load", json={"path": f"{T}/bad.yaml"}).status_code == 400 and c.post("/api/load", json={"path": f"{T}/nope.yaml"}).status_code == 400)
check("save: an unwritable path is a 400, not a crash", c.post("/api/save", json={"path": f"{T}/nodir/x.yaml", "cfg": cfg}).status_code == 400)

r = c.post("/api/estimate", json={"cfg": {"instruments": [{"name": "x", "sfz": [{"file": f"{T}/samples/jRhodes3c/none.sfz"}]}]}, "base": T})
check("estimate: a missing SFZ (the 500 in a real server log) is a 400 that says which script to run", r.status_code == 400 and "SFZ file not found" in r.json["error"] and "fetch_rhodes.sh" in r.json["error"], r.json.get("error", "")[:90])
r = c.get("/api/does-not-exist"); check("any unexpected URL or error answers in JSON, never an HTML error page", r.status_code == 404 and "error" in r.json and r.is_json)
r = c.post("/api/import", json={"base": T, "sfz": f"{S}/organ.sfz", "loop": "none"}).json; r2 = c.post("/api/import", json={"base": T, "sfz": f"{S}/organ.sfz", "loop": "auto"}).json
check("import SFZ: the loop menu overrides the SFZ's own loop points (none / auto); max_layers is accepted", all(x["loop"] == "none" for x in r["samples"]) and all(x["loop"] == "auto" for x in r2["samples"]) and len(c.post("/api/import", json={"base": T, "sfz": f"{S}/organ.sfz", "max_layers": 1}).json["samples"]) == 3)
r = c.post("/api/import", json={"base": T, "dir": S, "pattern": "{note}.*"}); check("import: a pattern that matches nothing is a clear 400", r.status_code == 400 or r.json["samples"] == [])

# ---- build, export, audition ----
check("export and audition before any build are 409 with an explanation", c.post("/api/export", json={}).status_code == 409 and c.post("/api/audition", json={}).status_code == 409 and "build first" in c.post("/api/audition", json={}).json["error"].lower())
check("build with nothing to build is a 400", c.post("/api/build", json={"cfg": {"instruments": []}}).status_code == 400)
def run_job(body):
    jid = c.post("/api/build", json=body).json["job"]; seen = set(); t0 = time.time()
    while time.time() - t0 < 180:
        j = c.get(f"/api/job/{jid}").json; seen.add(j["state"])
        if j["state"] != "running": return j, seen
        time.sleep(0.2)
    return None, seen
c.post("/api/state", json={"cfg": cfg, "base": T}); job, seen = run_job({})
check("build: runs as a background job and finishes with a summary (3 instruments, 10 zones)", job and job["state"] == "done" and len(job["result"]["zones"]) == 10 and len(job["result"]["instruments"]) == 3, str(job and (job["state"], job.get("error"))))
fixture_build = job["result"]; ref = core.build(cfg, T)
check("build: the GUI's build is byte-identical to the command-line build of the same config", hashlib.md5(app.state["last"]["blob"]).hexdigest() == hashlib.md5(ref["blob"]).hexdigest(), f"{app.state['last']['total']} bytes")
check("build: the result is JSON-clean (no raw bytes) and carries the warnings and per-zone SNR", "blob" not in job["result"] and all("snr" in z for z in job["result"]["zones"]) and isinstance(job["result"]["warnings"], list))
j2, _ = run_job({"cfg": {"instruments": [{"name": "x", "samples": [{"file": f"{T}/nope.wav", "root": 60}]}]}, "base": T})
check("build: a failing build ends in state 'error' with the message (and keeps the previous good build)", j2 and j2["state"] == "error" and "not found" in j2["error"] and app.state["last"]["total"] == ref["total"])
wl = c.get("/api/waveform", query_string={"file": f"{S}/organ_60.wav"}).json; check("waveform after a build: the loop points and the kept length are reported", wl["loop"] and 0 < wl["loop"]["start"] < wl["loop"]["end"] and wl["kept"], str(wl["loop"]))
od = os.path.join(T, "out"); os.makedirs(od); r = c.post("/api/export", json={"src": od, "gen": os.path.join(od, "gen")}).json
check("export: writes piano_bank.h, piano_data.cpp, bank.bin, bank.json; bank.bin is the built blob", all(os.path.exists(f) for f in r["files"]) and open(os.path.join(od, "gen", "bank.bin"), "rb").read() == ref["blob"] and "PIANO_ZONES 10" in open(os.path.join(od, "piano_bank.h")).read())
check("export: a folder that does not exist is a 400", c.post("/api/export", json={"src": f"{T}/nope"}).status_code == 400)

def audio(body):
    r = c.post("/api/audition", json=body)
    if r.status_code != 200: return None, r
    w = wave.open(io.BytesIO(r.data)); return (np.frombuffer(w.readframes(w.getnframes()), dtype=np.int16).astype(float).reshape(-1, w.getnchannels()) / 32768, w), r
t0 = time.time(); (a, w), r = audio({"instrument": 0, "key": 60, "vel": 80, "seconds": 1.0, "tail": 1.0, "params": {"tone": 127}})
check("audition: a real WAV (stereo, 44.1 kHz, exactly 2.0 s) rendered by the firmware's own engine from the built bank", a is not None and w.getframerate() == FS and w.getnchannels() == 2 and a.shape[0] == 2 * FS, f"first call incl. compiling the engine {time.time()-t0:.1f} s")
rms = lambda x: float(np.sqrt((x ** 2).mean()))
check("audition: the note sounds (attack well above silence) and is damped after the key is released", rms(a[:FS // 2]) > 0.02 and rms(a[-4000:]) < rms(a[:FS // 2]) * 0.2, f"{rms(a[:FS//2]):.3f} -> {rms(a[-4000:]):.4f}")
t0 = time.time(); (a2, _), _ = audio({"instrument": 0, "key": 60, "vel": 80, "seconds": 1.0, "tail": 1.0, "params": {"tone": 127}}); check("audition: deterministic (same request, same samples) and the engine is cached after the first call", np.array_equal(a, a2) and time.time() - t0 < 5, f"{time.time()-t0:.1f} s")
V = {v: audio({"instrument": 0, "key": 60, "vel": v, "seconds": 1.0, "tail": 0.2})[0][0][:FS] for v in (30, 60, 80, 110)}
def cent(x):
    m = x.mean(1)[:32768] * np.hanning(32768); sp = np.abs(np.fft.rfft(m)); return float((sp * np.fft.rfftfreq(32768, 1 / FS)).sum() / sp.sum())
check("audition: harder is louder inside a velocity layer (30 < 60 in the soft layer, 80 < 110 in the loud layer)", rms(V[30]) < rms(V[60]) and rms(V[80]) < rms(V[110]), f"{rms(V[30]):.3f} < {rms(V[60]):.3f}; {rms(V[80]):.3f} < {rms(V[110]):.3f}")
check("audition: crossing the layer boundary (velocity 70 / 71) switches to the brighter sample (centroid at least x1.2; not more, because at velocity 80 the loud layer, recorded at 99, is played softer and the velocity filter darkens it a little)", cent(V[80]) > 1.2 * cent(V[60]), f"{cent(V[60]):.0f} Hz -> {cent(V[80]):.0f} Hz")
(dry, _), _ = audio({"instrument": 0, "key": 60, "vel": 80, "seconds": 0.5, "tail": 2.5, "params": {"rev": 0}}); (wet, _), _ = audio({"instrument": 0, "key": 60, "vel": 80, "seconds": 0.5, "tail": 2.5, "params": {"rev": 127, "rsize": 100}})
check("audition: the effect knobs are applied (reverb: the same note rings on after the release)", rms(wet[int(1.5 * FS):]) > 3 * rms(dry[int(1.5 * FS):]) + 1e-4, f"tail RMS {rms(dry[int(1.5*FS):]):.5f} -> {rms(wet[int(1.5*FS):]):.5f}")
(one, _), _ = audio({"instrument": 0, "key": 60, "vel": 80, "seconds": 1.5, "tail": 0.1}); (chord, _), _ = audio({"instrument": 0, "notes": [{"key": 60, "at": 0, "len": 1.5}, {"key": 64, "at": 0.05, "len": 1.5}, {"key": 67, "at": 0.1, "len": 1.5}], "vel": 80, "tail": 0.1})
check("audition: a chord (list of notes with start times and lengths) is louder than one note", rms(chord) > 1.3 * rms(one), f"{rms(chord):.3f} vs {rms(one):.3f}")
(org, _), _ = audio({"instrument": 1, "key": 60, "vel": 80, "seconds": 6.0, "tail": 0.5}); check("audition: the looped instrument sustains for 6 s at a steady level (loop plays through), then stops", abs(20 * np.log10(rms(org[2 * FS:3 * FS]) / rms(org[5 * FS:int(5.9 * FS)]))) < 1.0 and rms(org[2 * FS:3 * FS]) > 0.05, f"{rms(org[2*FS:3*FS]):.3f} vs {rms(org[5*FS:int(5.9*FS)]):.3f}")
(ped, _), _ = audio({"instrument": 0, "key": 60, "vel": 80, "seconds": 0.3, "tail": 1.2, "pedal": True}); (nop, _), _ = audio({"instrument": 0, "key": 60, "vel": 80, "seconds": 0.3, "tail": 1.2})
check("audition: the sustain pedal keeps the note ringing after the key is up", rms(ped[int(1.0 * FS):]) > 3 * rms(nop[int(1.0 * FS):]) + 1e-4, f"{rms(ped[int(1.0*FS):]):.4f} vs {rms(nop[int(1.0*FS):]):.5f}")
check("audition: a nonexistent instrument and nonsense input are 400s, never a crash", c.post("/api/audition", json={"instrument": 9}).status_code == 400 and c.post("/api/audition", json={"key": "abc"}).status_code == 400 and c.post("/api/audition", json={"notes": [{"vel": 5}]}).status_code == 400)
check("audition: absurd lengths are clamped (a request for an hour does not hang the server)", (lambda t0: (audio({"instrument": 0, "key": 60, "seconds": 3600, "tail": 0.1}), time.time() - t0)[1] < 20)(time.time()))

# ---- the frontend script in Node ----
fix = dict(state=dict(st, cfg=cfg), estimate=c.post("/api/estimate", json={}).json, import_dir=imp_dir, import_sfz=imp_sfz, build=fixture_build); json.dump(fix, open(os.path.join(T, "fixture.json"), "w"))
r = subprocess.run(["node", os.path.join(ROOT, "host_test", "gui_dom_test.js"), os.path.join(ROOT, "tools", "bankconf", "static", "index.html"), os.path.join(T, "fixture.json")], capture_output=True, text=True, timeout=120)
print(r.stdout.rstrip()); check("frontend script executed in Node against the real API responses", r.returncode == 0, r.stderr[:200])
shutil.rmtree(T, ignore_errors=True)
print("\nALL GUI TESTS PASS" if ok else "\nGUI TESTS FAILED"); sys.exit(0 if ok else 1)
