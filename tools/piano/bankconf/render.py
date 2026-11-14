"""python3 -m bankconf render banks/all.yaml --inst 0 --notes 65:60,72:100,65:100 --out pc.wav
Builds the bank, then plays the notes (key:velocity, one after another, 2 s apart, each held 1.5 s) through the firmware's own engine on the PC and writes a stereo 16-bit WAV.
Record the same notes on the Pico, run tools/noise_report.py on both files and compare: if the PC render is clean and the Pico's is not, the difference is on the Pico."""
import io, os, sys, time, wave
import yaml
from . import core

def render(config, inst, notes, out, params=None, hold=1.5, gap=2.0, tail=2.5):
    from .app import create_app
    cfg = yaml.safe_load(open(config)); base = os.path.dirname(os.path.abspath(config)); c = create_app(None).test_client(); c.post("/api/state", json={"cfg": cfg, "base": base})
    print("building (a minute or two)...", file=sys.stderr); jid = c.post("/api/build", json={}).json["job"]
    while True:
        j = c.get(f"/api/job/{jid}").json
        if j["state"] != "running": break
        time.sleep(2)
    if j["state"] != "done": raise core.BankError(j.get("error", "build failed"))
    seq = []
    for i, n in enumerate(notes):
        key, vel = (int(v) for v in n.split(":")); seq.append(dict(key=key, vel=vel, at=i * gap, len=hold))
    r = c.post("/api/audition", json={"instrument": inst, "notes": seq, "tail": tail, "params": params or {}})
    if r.status_code != 200: raise core.BankError(r.json.get("error", "audition failed"))
    open(out, "wb").write(r.data); w = wave.open(io.BytesIO(r.data)); print(f"wrote {out}: {w.getnframes() / w.getframerate():.1f} s, {len(seq)} notes, instrument {inst} ({j['result']['instruments'][inst]['name']})")
    return 0
