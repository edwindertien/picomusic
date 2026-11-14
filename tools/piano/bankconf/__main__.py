"""python3 -m bankconf build  banks/grand.yaml            build the bank and write src/piano_bank.h + src/piano_data.cpp
   python3 -m bankconf estimate banks/grand.yaml          instant size estimate and warnings (no encoding)
   python3 -m bankconf levels banks/all.yaml              build, then print the loudness of every instrument across the keyboard, measured through the firmware's own engine
   python3 -m bankconf render banks/all.yaml --inst 0 --notes 65:60,72:100 --out pc.wav    play notes through the firmware's own engine on the PC and write a WAV
   python3 -m bankconf gui [config.yaml] [--port 5000]    the web GUI"""
import sys, os, argparse, yaml
from . import core

def main(argv=None):
    ap = argparse.ArgumentParser(prog="bankconf"); sub = ap.add_subparsers(dest="cmd", required=True)
    for c in ("build", "estimate"):
        p = sub.add_parser(c); p.add_argument("config"); p.add_argument("--src", default=core.SRC); p.add_argument("--gen", default=core.GEN); p.add_argument("--dry", action="store_true", help="build but write nothing")
    lv = sub.add_parser("levels"); lv.add_argument("config"); lv.add_argument("--vel", type=int, nargs="*", default=[80, 110]); lv.add_argument("--keys", default="30:97:6", help="first:last+1:step of the MIDI keys to measure")
    rn = sub.add_parser("render"); rn.add_argument("config"); rn.add_argument("--inst", type=int, default=0); rn.add_argument("--notes", required=True, help="key:velocity,key:velocity,..."); rn.add_argument("--out", default="render.wav"); rn.add_argument("--set", nargs="*", default=[], help="knob settings, e.g. rev=0 vol=100 hard=64")
    g = sub.add_parser("gui"); g.add_argument("config", nargs="?"); g.add_argument("--port", type=int, default=5000); g.add_argument("--host", default="127.0.0.1")
    a = ap.parse_args(argv)
    if a.cmd == "gui":
        from .app import create_app
        create_app(a.config).run(host=a.host, port=a.port, debug=False); return 0
    if a.cmd == "render":
        from .render import render
        try: return render(a.config, a.inst, a.notes.split(","), a.out, dict((kv.split("=")[0], int(kv.split("=")[1])) for kv in a.set))
        except core.BankError as e: print("error:", e, file=sys.stderr); return 2
    if a.cmd == "levels":
        from .levels import report
        try: return report(a.config, a.vel, a.keys)
        except core.BankError as e: print("error:", e, file=sys.stderr); return 2
    cfg = yaml.safe_load(open(a.config)); base = os.path.dirname(os.path.abspath(a.config))
    try:
        if a.cmd == "estimate":
            e = core.estimate(cfg, base); print(f"budget {e['budget']:,} bytes, estimated {e['total']:,} (duration scale {e['scale']:.3f})")
            for i, b in enumerate(e["per_instrument"]): print(f"  instrument {i}: {b:,} bytes")
            for w in e["warnings"]: print("  WARNING:", w)
            return 0
        rep = core.build(cfg, base, progress=lambda i, n, m: print(f"\r[{i + 1}/{n}] {m:50s}", end="", file=sys.stderr))
        print(file=sys.stderr)
        print(f"{len(rep['zones'])} zones in {len(rep['instruments'])} instrument(s), {rep['total']:,} bytes of {rep['budget']:,}")
        for i in rep["instruments"]: print(f"  {i['name']:12s} {i['zoneCount']:3d} zones {i['bytes']:>9,} bytes")
        for w in rep["warnings"]: print("  WARNING:", w)
        if not a.dry: core.write_outputs(rep, a.src, a.gen); print(f"wrote {a.src}/piano_bank.h, piano_data.cpp and {a.gen}/bank.bin, bank.json")
        return 0
    except core.BankError as e:
        print("error:", e, file=sys.stderr); return 2
if __name__ == "__main__": sys.exit(main())
