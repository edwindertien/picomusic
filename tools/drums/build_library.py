#!/usr/bin/env python3
"""Writes library/patterns.json, kits.json and sounds.json (the shipped library). Edit this file and re-run it to change the library;
your own entries live in library/user/ and are never touched. Grid: . off  1 ghost  2 normal  3 accent, 16 steps = one bar of 16ths.
The rhythms are standard textbook grooves written from memory and kept simple; they have NOT been checked by ear against recordings."""
import json, os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from drumconf import core

P = []
def pat(cat, name, bpm, swing, tracks, note="", ln=16):
    if "CH" in tracks and "OH" in tracks:        # an open hat replaces the closed hat on its step (the engine chokes the open hat with a closed one)
        tracks = dict(tracks, CH="".join("." if i < len(tracks["OH"]) and tracks["OH"][i] != "." else c for i, c in enumerate(tracks["CH"])))
    P.append({"name": name, "category": cat, "bpm": bpm, "swing": swing, "len": ln, "tracks": tracks, "note": note})

# ---- house / techno / electronic
pat("House", "Four on the floor", 124, 0,   {"BD": "3...2...2...2...", "CP": "....2.......2...", "CH": "1.2.1.2.1.2.1.2.", "OH": "..2...2...2...2."}, "the classic: kick every beat, clap on 2 and 4, open hat off the beat")
pat("House", "Deep house", 120, 20,         {"BD": "3...2...2...2...", "CP": "....2.......2...", "CH": "..1...1...1...1.", "OH": "..2...2...2...2.", "RS": ".1..1..1..1..1.."}, "slightly swung, ghost rim shots")
pat("House", "Chicago jack", 122, 10,       {"BD": "3...3...3...3...", "SD": "....3.......3...", "CH": "2.2.2.2.2.2.2.2.", "OH": "..1...1...1...1.", "CP": "....2.......2..1"})
pat("Techno", "Driving techno", 132, 0,     {"BD": "3...3...3...3...", "CH": "..2...2...2...2.", "OH": "...1...1...1...1", "CP": "....2.......2...", "RS": "1..1..1...1..1.."}, "straight kick, off-beat hats")
pat("Techno", "Minimal techno", 128, 0,     {"BD": "3...3...3...3...", "CH": "1.1.1.1.1.1.1.1.", "RS": "..1....1..1....1", "CB": ".........2......"})
pat("Techno", "Industrial stomp", 138, 0,   {"BD": "3..23..3.2.33...", "SD": "....3.......3...", "CH": "2.2.2.2.2.2.2.2.", "CY": "3..............."})
pat("Electro", "Electro (Planet Rock style)", 126, 0, {"BD": "3..2..2...2..1..", "SD": "....3.......3...", "CH": "1111111111111111", "CB": ".......2.......2", "OH": "..2.....2......."}, "the factory demo")
pat("Electro", "Miami bass", 108, 0,       {"BD": "3..3.2..3..3.2..", "SD": "....3.......3...", "CH": "2.2.2.2.2.2.2.2.", "CP": "....2.......2..."})
pat("Disco", "Disco", 118, 0,              {"BD": "3...3...3...3...", "SD": "....3.......3...", "CH": "2.2.2.2.2.2.2.2.", "OH": "..2...2...2...2.", "CP": "....1.......1..."}, "four on the floor, open hat on every off-beat")
pat("Disco", "Hi-NRG", 130, 0,             {"BD": "3...3...3...3...", "SD": "....3.......3...", "CH": "2.2.2.2.2.2.2.2.", "OH": "1.1.1.1.1.1.1.1.", "CP": "....2.......2..."})
# ---- hip hop / trap
pat("Hip hop", "Boom bap", 92, 25,         {"BD": "3.....2.2.2.....", "SD": "....3.......3..1", "CH": "2.1.2.1.2.1.2.1.", "RS": "...........1...."}, "the factory demo")
pat("Hip hop", "Lazy boom bap", 84, 40,    {"BD": "3.......2.2.....", "SD": "....3.......3...", "CH": "2.2.2.2.2.2.2.2.", "OH": ".............2.."})
pat("Hip hop", "West-coast bounce", 94, 15, {"BD": "3..2....3..2..2.", "SD": "....3.......3...", "CH": "2.1.2.1.2.1.2.1.", "CP": "....2.......2..."})
pat("Trap", "Trap", 140, 0,                {"BD": "3.....2...3.....", "SD": "........3.......", "CH": "2.2.2.2.2.2.2.2.", "OH": "...............2", "CP": "........2......."}, "half-time feel: snare on 3 only")
pat("Trap", "Trap with hat roll", 140, 0,  {"BD": "3..2....3.......", "SD": "........3.......", "CH": "2.2.2.2.2.222.22", "OH": "..............2.", "CP": "........1......."})
pat("Trap", "Drill", 142, 0,               {"BD": "3.....2....3..2.", "SD": "......2.....3...", "CH": "2.2.2.2.2.2.2.22", "OH": "..2.........2..."})
# ---- breaks / dnb
pat("Breaks", "Funky break", 100, 12,      {"BD": "3..2..2...2..3..", "SD": "....3..1.1..3..1", "CH": "2.2.2.2.2.2.2.2.", "OH": "......2.........", "RS": "...1............"})
pat("Breaks", "Big beat", 118, 0,          {"BD": "3.....2.2.3.....", "SD": "....3.......3...", "CH": "2.2.2.2.2.2.2.2.", "CY": "3...............", "CP": "....1.......1..."})
pat("Drum and bass", "Two-step", 172, 0,   {"BD": "3.........3.....", "SD": "....3.......3...", "CH": "2.2.2.2.2.2.2.2.", "OH": "......2.....2..."})
pat("Drum and bass", "Rolling DnB", 174, 0, {"BD": "3.........3..2..", "SD": "....3..1....3.1.", "CH": "2.2.2.2.2.2.2.2.", "OH": "..2.....2.......", "RS": ".1.............."})
pat("Garage", "UK garage shuffle", 130, 45, {"BD": "3.........2.....", "SD": "....3.......3...", "CH": "2.2.2.2.2.2.2.2.", "OH": "..1...1...1...1.", "RS": ".1..1..1.1..1..1"}, "needs the swing: that is the whole groove")
# ---- rock / pop / soul
pat("Rock", "Basic rock", 110, 0,          {"BD": "3.......2.2.....", "SD": "....3.......3...", "CH": "2.2.2.2.2.2.2.2.", "CY": "3..............."})
pat("Rock", "Driving eighths", 128, 0,     {"BD": "3.2.....3.2.....", "SD": "....3.......3...", "CH": "2.2.2.2.2.2.2.2.", "OH": "...............2"})
pat("Rock", "Half-time rock", 90, 0,       {"BD": "3.......2.......", "SD": "........3.......", "CH": "2.2.2.2.2.2.2.2.", "CY": "3..............."})
pat("Pop", "Pop ballad", 76, 0,            {"BD": "3.......2.2.....", "SD": "....3.......3...", "CH": "2.2.2.2.2.2.2.2.", "RS": "..............1."})
pat("Soul", "Motown", 118, 0,              {"BD": "3.2.....3.2.....", "SD": "....3.......3...", "CH": "2.2.2.2.2.2.2.2."}, "straight eighths on the hat, kick pushing ahead of the beat")
pat("Funk", "Funk", 104, 12,              {"BD": "3..2..2...3...2.", "SD": "....3..1.1..3..1", "CH": "2.2.2.2.2.2.2.2.", "OH": "......2.....2..."})
pat("Shuffle", "Blues shuffle (swung 16ths)", 96, 70, {"BD": "3.......3.......", "SD": "....3.......3...", "CH": "2.2.2.2.2.2.2.2.", "OH": ".............2.."}, "set swing high: the odd 16ths are delayed")
pat("Reggae", "One drop", 78, 15,          {"BD": "........3.......", "RS": "........3.......", "CH": "..2...2...2...2.", "OH": "......1.......1.", "SD": "........2......."}, "kick and rim on beat 3")
pat("Reggae", "Steppers", 140, 0,          {"BD": "3...3...3...3...", "RS": "........2.......", "CH": "..2...2...2...2.", "OH": "......1.......1."})
# ---- latin / world
pat("Latin", "Latin mix", 100, 0,          {"BD": "2.......2.......", "CL": "3..2..2...2.2...", "MA": "2.1.2.1.2.1.2.1.", "LC": "....2.......2...", "MC": ".......2.......2", "HC": "..2.......2.....", "RS": "....1.......1..."}, "the factory demo")
pat("Latin", "Son clave 3-2", 100, 0,      {"CL": "2..2..2...2.2...", "BD": "2.......2.......", "MA": "2.2.2.2.2.2.2.2.", "LC": "....2.......2..."}, "claves: the 3 side first, then the 2 side")
pat("Latin", "Rumba clave", 96, 0,         {"CL": "2..2...2..2.2...", "BD": "2.......2.......", "MA": "2.2.2.2.2.2.2.2.", "MC": ".......2.......2"})
pat("Latin", "Bossa nova", 128, 0,         {"BD": "3..2..2.3..2..2.", "RS": "2..2..2...2.2...", "CH": "2.2.2.2.2.2.2.2."}, "dotted-eighth kick pattern with a clave-like rim shot")
pat("Latin", "Samba", 100, 0,             {"BD": "3..2.3..2..3..2.", "CH": "2.2.2.2.2.2.2.2.", "CB": "2..2..2..2..2...", "MA": "2.1.2.1.2.1.2.1.", "HC": "....1.......1..."})
pat("Latin", "Cumbia", 90, 0,             {"BD": "3.......3.......", "SD": "....1.......1...", "CL": "2...2...2...2...", "MA": "2.2.2.2.2.2.2.2.", "LC": "..2...2...2...2."})
pat("Afro", "Afrobeat", 112, 10,          {"BD": "3..2..2.3..2....", "SD": "....3.......3...", "CH": "2.2.2.2.2.2.2.2.", "CB": "2..2..2...2.2...", "MC": "..2..2..2..2..2."})
# ---- fills / short ones
pat("Fills", "Snare roll (1 bar)", 120, 0,  {"SD": "1.1.2.2.2.3.3333", "BD": "3...............", "CY": "...............3"})
pat("Fills", "Tom fill", 120, 0,           {"HT": "2.2.............", "MT": "....2.2.........", "LT": "........2.2.....", "BD": "............3.3.", "CY": "3..............3"})
pat("Fills", "Half bar (8 steps)", 120, 0, {"BD": "3...2...", "SD": "....3...", "CH": "2.2.2.2."}, "length 8: a pattern may be shorter than a bar", ln=8)
pat("Fills", "Waltz-ish 12 steps", 110, 0, {"BD": "3...........", "SD": "....2...2...", "CH": "2.2.2.2.2.2."}, "length 12 = three beats of 16ths (3/4)", ln=12)
pat("Basics", "Kick only", 120, 0,         {"BD": "3...3...3...3..."}, "a test: just the kick")
pat("Basics", "Hats only", 120, 0,         {"CH": "2.2.2.2.2.2.2.2.", "OH": "..............2."}, "a test: just the hats")

# ---- kits: variations of the factory kit. [level tune decay tone pan] per instrument
K = core.DEFAULT_KIT
def kit(f): return [[core.clamp(v, 0, 127) for v in f(i, list(r))] for i, r in enumerate(K)]
def add(r, lv=0, tu=0, de=0, to=0, pa=None): return [r[0] + lv, r[1] + tu, r[2] + de, r[3] + to, r[4] if pa is None else pa]
KITS = [
    ("808 (factory)", "the default kit", K),
    ("Tight and dry", "shorter decays all over: close-mic'd, good for fast tempos", kit(lambda i, r: add(r, de=-18 if i != 8 else -8))),
    ("Boomy 808", "long sub kick, longer toms and snare tail", kit(lambda i, r: add(r, tu=-14, de=30, to=-10) if i == 0 else add(r, de=12) if i in (1, 2, 3, 4) else r)),
    ("Hard techno", "punchy click kick, bright snare and hats", kit(lambda i, r: add(r, lv=4, de=-10, to=40) if i == 0 else add(r, to=22, lv=4) if i in (1, 6) else add(r, to=16) if i in (8, 9, 10) else r)),
    ("Lo-fi", "dark, soft, everything a little detuned down", kit(lambda i, r: add(r, tu=-8, to=-24, lv=-8))),
    ("Electro", "high-pitched toms, short claps, bright hats", kit(lambda i, r: add(r, tu=14, de=-8) if i in (2, 3, 4) else add(r, to=14) if i in (8, 9, 10, 7) else r)),
    ("Latin percussion", "congas, claves and maracas forward, kick and snare back", kit(lambda i, r: add(r, lv=10) if i >= 11 else add(r, lv=-14) if i in (0, 1) else r)),
    ("Mono (centre)", "everything panned to the centre", kit(lambda i, r: add(r, pa=64))),
    ("Wide stereo", "pan spread twice as wide as the factory kit", kit(lambda i, r: add(r, pa=64 + (r[4] - 64) * 2))),
]
# ---- sound presets: single-instrument settings.  name, values [level tune decay tone pan]
S = []
def snd(inst, name, vals, note=""): S.append({"name": name, "inst": inst, "values": vals, "note": note})
def variants(inst, label, base, short, long_, low, high, dark, bright):
    L, T, D, To, Pa = base
    for nm, d in (("short", short), ("long", long_), ("low", low), ("high", high), ("dark", dark), ("bright", bright)):
        v = [L, T, D, To, Pa]
        if d: v = [L, T + d.get("t", 0), D + d.get("d", 0), To + d.get("o", 0), Pa]
        snd(inst, "%s %s" % (label, nm), [core.clamp(x, 0, 127) for x in v])
for i, lab in enumerate(core.INST):
    base = K[i]; snd(lab, "factory", list(base), "the default setting")
    snd(lab, "short", [base[0], base[1], base[2] - 20, base[3], base[4]]); snd(lab, "long", [base[0], base[1], base[2] + 25, base[3], base[4]])
    snd(lab, "low", [base[0], base[1] - 22, base[2], base[3], base[4]]); snd(lab, "high", [base[0], base[1] + 22, base[2], base[3], base[4]])
    snd(lab, "dark", [base[0], base[1], base[2], base[3] - 28, base[4]]); snd(lab, "bright", [base[0], base[1], base[2], base[3] + 28, base[4]])
HAND = {
    "BD": [("sub boom", [123, 54, 100, 30, 64]), ("punch", [123, 70, 25, 90, 64]), ("click", [120, 76, 32, 120, 64])],
    "SD": [("snappy", [107, 64, 50, 118, 64]), ("fat", [110, 52, 70, 40, 64]), ("tight", [105, 70, 28, 80, 64])],
    "CH": [("tick", [100, 70, 12, 100, 78]), ("sizzle", [100, 64, 45, 80, 78])],
    "OH": [("short open", [94, 64, 35, 64, 84]), ("wash", [90, 60, 90, 70, 84])],
    "CP": [("tight clap", [102, 64, 30, 80, 70]), ("room clap", [100, 58, 85, 55, 70])],
}
for lab, lst in HAND.items():
    for nm, v in lst: snd(lab, nm, v)
# the 'sound' variants above (short/long/low/high/dark/bright) are plain offsets from the factory setting: starting points, not hand-tuned.

def write(kind, items):
    os.makedirs(os.path.join(os.path.dirname(os.path.abspath(__file__)), "library"), exist_ok=True)
    with open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "library", kind + ".json"), "w", encoding="utf-8") as f: json.dump(items, f, indent=1, ensure_ascii=False)
if __name__ == "__main__":
    write("patterns", [core._check_entry("patterns", p) | {"category": p["category"]} for p in P])
    write("kits", [core._check_entry("kits", {"name": n, "kit": k, "note": d}) for n, d, k in KITS])
    write("sounds", [core._check_entry("sounds", s) for s in S])
    print(len(P), "patterns,", len(KITS), "kits,", len(S), "sounds")
