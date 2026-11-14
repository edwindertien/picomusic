"""The web GUI of the bank tool (Flask). Local use: python3 tools/bank_gui.py [config.yaml] -> http://127.0.0.1:5000

API (all JSON unless noted):  GET /api/state  POST /api/state  POST /api/estimate  POST /api/import  GET /api/browse  GET /api/waveform
                              POST /api/build -> job id, GET /api/job/<id>  POST /api/export  POST /api/save  POST /api/load  POST /api/audition (audio/wav)"""
import os, io, json, time, uuid, wave, hashlib, shutil, tempfile, threading, subprocess
import numpy as np, yaml
from flask import Flask, jsonify, request, send_file, send_from_directory
from . import core
from .notes import note_name

AUDIO_EXT = {".wav", ".flac", ".mp3", ".ogg", ".aif", ".aiff", ".m4a"}
DEFAULT_CFG = {"budget": 3000000, "normalize_peak": 0.92, "instruments": []}

def _summary(rep):
    return {k: v for k, v in rep.items() if k != "blob"}

def create_app(config_path=None, workdir=None):
    app = Flask(__name__, static_folder=os.path.join(os.path.dirname(os.path.abspath(__file__)), "static"), static_url_path="/static")
    S = dict(cfg=json.loads(json.dumps(DEFAULT_CFG)), path=config_path, base=os.getcwd(), last=None, jobs={}, exes={}, work=workdir or tempfile.mkdtemp(prefix="bankconf_"), lock=threading.Lock())
    if config_path:
        S["cfg"] = yaml.safe_load(open(config_path)) or json.loads(json.dumps(DEFAULT_CFG)); S["base"] = os.path.dirname(os.path.abspath(config_path))
    app.state = S

    @app.errorhandler(Exception)
    def any_error(e):                        # whatever goes wrong, the GUI gets a readable JSON message, not an HTML error page
        from werkzeug.exceptions import HTTPException
        if isinstance(e, HTTPException): return jsonify(error=f"{e.code} {e.name}: {e.description}"), e.code
        app.logger.exception("unexpected error")
        return jsonify(error=f"{type(e).__name__}: {e}"), 500

    def err(msg, code=400): return jsonify(error=str(msg)), code
    def cfg_from_request():
        j = request.get_json(silent=True) or {}
        return j.get("cfg") if j.get("cfg") is not None else S["cfg"], j.get("base") or S["base"]

    @app.route("/")
    def index(): return send_from_directory(app.static_folder, "index.html")

    @app.get("/api/state")
    def get_state():
        cmds, pdef = core.read_params()
        return jsonify(cfg=S["cfg"], path=S["path"], base=S["base"], params=cmds, param_defaults=pdef, has_build=S["last"] is not None, root=core.ROOT)

    @app.post("/api/state")
    def set_state():
        j = request.get_json(silent=True) or {}
        if "cfg" not in j: return err("missing cfg")
        try:
            if j["cfg"].get("instruments"): core.expand_config(j["cfg"], j.get("base") or S["base"])
        except (core.BankError, ValueError, KeyError, TypeError, OSError) as e: return err(e)
        S["cfg"] = j["cfg"]; S["base"] = j.get("base") or S["base"]; S["path"] = j.get("path", S["path"]); return jsonify(ok=True)

    @app.post("/api/estimate")
    def estimate():
        cfg, base = cfg_from_request()
        if not cfg.get("instruments"): return jsonify(budget=int(cfg.get("budget", 3000000)), total=0, scale=1.0, per_instrument=[], samples=[], warnings=[], instruments=[])
        try:
            est = core.estimate(cfg, base); ins = core.expand_config(cfg, base)
        except (core.BankError, ValueError, KeyError, TypeError, OSError) as e: return err(e)
        out = []
        for ii, i in enumerate(ins):
            layers, cov = core.coverage(i["samples"])
            out.append(dict(name=i["name"], credit=i["credit"], gain=i["gain"], defaults=i["defaults"], layers=layers,
                            keys={str(k): cov[k] for k in range(21, 109)},
                            samples=[dict(file=s["file"], name=s["name"], root=s["root"], root_name=note_name(s["root"]), lo=s["lo"], hi=s["hi"], vlo=s["vlo"], vhi=s["vhi"], vref=s["vref"], gain=s["gain"],
                                          loop=s["loop"], loop_sfz=s["loop_sfz"], rate=s["rate"], maxdur=s["maxdur"]) for s in i["samples"]]))
        est["instruments"] = out; return jsonify(est)

    @app.post("/api/import")
    def do_import():
        j = request.get_json(silent=True) or {}; base = j.get("base") or S["base"]
        ic = {"name": "tmp", "rate_rules": j.get("rate_rules") or [{"max_key": 127, "rate": int(j.get("rate", 24000)), "maxdur": float(j.get("maxdur", 6.0))}]}
        if j.get("dir"): ic["import"] = [dict({"dir": j["dir"], "pattern": j.get("pattern", "{note}.*")}, **({"loop": j["loop"]} if j.get("loop") else {}))]
        if j.get("sfz"):
            e = {"file": j["sfz"]}
            if j.get("loop"): e["loop"] = j["loop"]
            if j.get("max_layers"): e["max_layers"] = int(j["max_layers"])
            ic["sfz"] = [e]
        try: ins = core.expand_config({"instruments": [ic]}, base)
        except (core.BankError, ValueError, KeyError, OSError, TypeError) as e: return err(e)
        rows = []
        for s in ins[0]["samples"]:
            r = dict(file=s["file"], root=note_name(s["root"]), vlo=s["vlo"], vhi=s["vhi"], gain=s["gain"], loop=s["loop"], rate=s["rate"], maxdur=s["maxdur"])
            if j.get("sfz"): r.update(lo=note_name(s["lo"]), hi=note_name(s["hi"]))                  # an SFZ carries its own key ranges; a directory import leaves them automatic
            if s["loop"] == "sfz": r["loop_sfz"] = list(s["loop_sfz"])
            rows.append(r)
        return jsonify(samples=rows, notes=ins[0].get("notes", []))

    @app.get("/api/browse")
    def browse():
        p = os.path.abspath(os.path.expanduser(request.args.get("path") or S["base"] or os.getcwd()))
        if os.path.isfile(p): p = os.path.dirname(p)
        if not os.path.isdir(p): return err(f"not a folder: {p}", 404)
        dirs, files = [], []
        try:
            for n in sorted(os.listdir(p), key=str.lower):
                if n.startswith("."): continue
                fp = os.path.join(p, n)
                if os.path.isdir(fp): dirs.append(n)
                else: files.append(dict(name=n, size=os.path.getsize(fp), audio=os.path.splitext(n)[1].lower() in AUDIO_EXT, sfz=n.lower().endswith(".sfz"), yaml=n.lower().endswith((".yaml", ".yml"))))
        except PermissionError: return err("permission denied", 403)
        return jsonify(path=p, parent=os.path.dirname(p), dirs=dirs, files=files)

    @app.get("/api/waveform")
    def waveform():
        f = request.args.get("file", ""); n = int(request.args.get("points", 800))
        try: info = core.load_sample(f)
        except core.BankError as e: return err(e, 404)
        x = info["x"]; seg = max(1, len(x) // n); m = len(x) // seg; a = x[:m * seg].reshape(m, seg)
        z = None
        if S["last"]:
            for q in S["last"]["zones"]:
                if q["file"] == f: z = q; break
        return jsonify(dur=len(x) / core.FS, peak=info["peak"], lo=a.min(1).tolist(), hi=a.max(1).tolist(), loop=(dict(start=z["loopStart"] / z["rate"], end=z["loopEnd"] / z["rate"]) if z and z["loopEnd"] else None), kept=(z["n"] / z["rate"] if z else None))

    @app.post("/api/build")
    def build():
        cfg, base = cfg_from_request()
        if not cfg.get("instruments"): return err("nothing to build: the config has no instruments")
        jid = uuid.uuid4().hex[:8]; job = dict(state="running", i=0, n=1, msg="starting", result=None, error=None); S["jobs"][jid] = job
        def run():
            try:
                def prog(i, n, m): job.update(i=i, n=n, msg=m)
                rep = core.build(cfg, base, progress=prog)
                with S["lock"]: S["last"] = rep; S["last_cfg"] = json.loads(json.dumps(cfg))
                job.update(state="done", result=_summary(rep))
            except Exception as e: job.update(state="error", error=str(e))
        threading.Thread(target=run, daemon=True).start(); return jsonify(job=jid)

    @app.get("/api/job/<jid>")
    def job(jid):
        j = S["jobs"].get(jid)
        return jsonify(j) if j else err("no such job", 404)

    @app.post("/api/export")
    def export():
        if not S["last"]: return err("build first", 409)
        j = request.get_json(silent=True) or {}; src = j.get("src") or core.SRC; gen = j.get("gen") or core.GEN
        if not os.path.isdir(src): return err(f"not a folder: {src}")
        core.write_outputs(S["last"], src, gen); return jsonify(ok=True, files=[os.path.join(src, "piano_bank.h"), os.path.join(src, "piano_data.cpp"), os.path.join(gen, "bank.bin"), os.path.join(gen, "bank.json")], bytes=S["last"]["total"])

    @app.post("/api/save")
    def save():
        j = request.get_json(silent=True) or {}
        if not j.get("path") or "cfg" not in j: return err("need path and cfg")
        try: open(j["path"], "w").write(yaml.safe_dump(j["cfg"], sort_keys=False))
        except OSError as e: return err(e)
        S["path"] = j["path"]; S["cfg"] = j["cfg"]; S["base"] = os.path.dirname(os.path.abspath(j["path"])); return jsonify(ok=True, path=j["path"])

    @app.post("/api/load")
    def load():
        j = request.get_json(silent=True) or {}
        try: cfg = yaml.safe_load(open(j.get("path", "")))
        except (OSError, yaml.YAMLError) as e: return err(e)
        if not isinstance(cfg, dict) or "instruments" not in cfg: return err("not a bank config (no 'instruments')")
        S["cfg"] = cfg; S["path"] = j["path"]; S["base"] = os.path.dirname(os.path.abspath(j["path"])); return jsonify(cfg=cfg, base=S["base"])

    def audition_exe(rep):
        h = hashlib.md5(rep["blob"] + core.header_text(rep).encode()).hexdigest()
        if h in S["exes"] and os.path.exists(S["exes"][h]): return S["exes"][h]
        d = os.path.join(S["work"], "aud_" + h[:10]); os.makedirs(d, exist_ok=True)
        for sd in reversed(core.SRC_DIRS):                                    # shared headers first, so the piano's own win
            for f in os.listdir(sd):
                if f.endswith(".h") and f != "piano_bank.h": shutil.copy(os.path.join(sd, f), d)
        core.write_outputs(rep, d); exe = os.path.join(d, "audition")
        core.compile_cpp([os.path.join(core.HOSTTEST, "probe.cpp"), os.path.join(d, "piano_data.cpp"), os.path.join(core.HOSTTEST, "mock", "mock_globals.cpp"), os.path.join(core.HOSTTEST, "mock", "mock_globals2.cpp")],
                         exe, includes=[os.path.join(core.HOSTTEST, "mock"), d], flags=("-std=gnu++17", "-O1"), what="the audition engine (the firmware's own sound engine for your PC)")
        S["exes"][h] = exe; return exe

    @app.post("/api/audition")
    def audition():
        if not S["last"]: return err("build first: the audition plays the built bank through the firmware's own engine", 409)
        j = request.get_json(silent=True) or {}; rep = S["last"]
        try:
            inst = int(j.get("instrument", 0)); key = int(j.get("key", 60)); vel = int(j.get("vel", 80)); sec = max(0.1, min(20.0, float(j.get("seconds", 2.0)))); notes = j.get("notes")
            if not 0 <= inst < len(rep["instruments"]): return err("no such instrument")
            exe = audition_exe(rep); cmds = rep["params"]
            script = f"inst {inst}\n"
            for k, v in (j.get("params") or {}).items():
                if k in cmds: script += f"set {k} {int(v)}\n"
            if j.get("pedal"): script += "pedal 1\n"
            seq = notes if notes else [dict(key=key, vel=vel, at=0.0, len=sec)]                              # a list of notes (a chord or a phrase), or one note
            ev = sorted([(float(n.get("at", 0)), "on", int(n["key"]), int(n.get("vel", vel))) for n in seq] + [(float(n.get("at", 0)) + float(n.get("len", sec)), "off", int(n["key"]), 0) for n in seq])
            now = 0
            for t, kind, k, v in ev:
                n = int(t * core.FS) - now
                if n > 0: script += f"run {n}\n"; now += n
                script += (f"on {k} {v}\n" if kind == "on" else f"off {k}\n")
            tail = int(float(j.get("tail", 2.0)) * core.FS); script += f"run {tail}\n"
            raw = os.path.join(S["work"], "aud_%s.raw" % uuid.uuid4().hex[:6]); r = subprocess.run([exe], input=script + f"write {raw}\n", capture_output=True, text=True, timeout=120)
            pcm = np.fromfile(raw, dtype=np.int16); os.remove(raw)
        except core.BankError as e: return err(e, 500)
        except (ValueError, KeyError, TypeError) as e: return err(e)
        buf = io.BytesIO(); w = wave.open(buf, "wb"); w.setnchannels(2); w.setsampwidth(2); w.setframerate(core.FS); w.writeframes(pcm.tobytes()); w.close(); buf.seek(0)
        return send_file(buf, mimetype="audio/wav")
    return app
