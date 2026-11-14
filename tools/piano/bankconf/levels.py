"""python3 -m bankconf levels banks/all.yaml : build the bank, then play a note on every instrument at several keys and velocities through the firmware's own engine
and print the RMS level of the first second (dBFS, default knobs, effects off). Use it to see where one instrument is louder than another and to set `gain` (dB) per instrument."""
import io, os, sys, time, wave, yaml
import numpy as np
from . import core

def report(config, vels=(80, 110), keyspec="30:97:6"):
    from .app import create_app
    cfg = yaml.safe_load(open(config)); base = os.path.dirname(os.path.abspath(config)); c = create_app(None).test_client(); c.post("/api/state", json={"cfg": cfg, "base": base})
    print("building (this takes a minute or two)...", file=sys.stderr); jid = c.post("/api/build", json={}).json["job"]
    while True:
        j = c.get(f"/api/job/{jid}").json
        if j["state"] != "running": break
        time.sleep(2)
    if j["state"] != "done": raise core.BankError(j.get("error", "build failed"))
    r = j["result"]; a, b, st = (int(t) for t in keyspec.split(":")); keys = list(range(a, b, st)); FS = core.FS
    print(f"\n{r['total']:,} bytes, {len(r['zones'])} zones")
    for i in r["instruments"]: print(f"  {i['name']:12s} measured level {i['level_db']:6.1f} dBFS at velocity 80, level match {i['level_offset_db']:+.1f} dB")
    def play(inst, key, vel):
        rr = c.post("/api/audition", json={"instrument": inst, "key": key, "vel": vel, "seconds": 1.2, "tail": 0.1, "params": {"rev": 0, "chor": 0, "trem": 64}})
        if rr.status_code != 200: raise core.BankError(rr.json.get("error", "audition failed"))
        w = wave.open(io.BytesIO(rr.data)); x = np.frombuffer(w.readframes(w.getnframes()), dtype=np.int16).astype(float).reshape(-1, 2).mean(1) / 32768; return 20 * np.log10(np.sqrt((x[:FS] ** 2).mean()) + 1e-9)
    print("\nRMS of the first second, dBFS (default knobs, no effects)")
    print("key".ljust(18) + " ".join(f"{k:5d}" for k in keys) + "  | median")
    for vel in vels:
        for i, ins in enumerate(r["instruments"]):
            v = [play(i, k, vel) for k in keys]; print(f"v{vel:3d} {ins['name'][:12]:12s} " + " ".join(f"{x:5.1f}" for x in v) + f"  | {np.median(v):6.1f}")
    print("\nTo make an instrument louder or quieter add `gain: <dB>` to it in the config (or use level_match: auto for a first approximation).")
    return 0
