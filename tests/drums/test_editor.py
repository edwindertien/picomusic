#!/usr/bin/env python3
"""Tests of the drum editor (tools/drums): image round trip against the REAL firmware (serialize/deserialize), validation, library,
API, audition through the firmware engine, seamless loop, device protocol (against a fake machine), frontend script syntax."""
import os, sys, json, struct, subprocess, tempfile, wave, shutil, io, threading, re
HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
if not os.path.isdir(os.path.join(ROOT, "tools", "drums")): ROOT = os.path.abspath(os.path.join(HERE, "..", "..", "..", ".."))   # staged: tests/.build/drums/host_test
TOOL = os.path.join(ROOT, "tools", "drums"); sys.path.insert(0, TOOL)
from drumconf import core, engine, device, app as appmod
from drumconf.core import DrumError
fails = 0
def check(c, msg):
    global fails
    print(("PASS " if c else "FAIL ") + msg)
    if not c: fails += 1
def raises(fn, text=None):
    try: fn()
    except DrumError as e: return text is None or text in str(e)
    except Exception: return False
    return False

# ---- 1. document <-> image
d = core.new_document("t"); d["bpm"] = 133.5; d["swing"] = 44; d["vol"] = 90; d["drive"] = 12; d["songMode"] = True; d["padGm"] = True; d["padBase"] = 48; d["selPat"] = 5
d["patterns"][5] = {"name": "x", "len": 12, "tracks": {"BD": "3...2...2...", "HC": ".......3.1.2...."}}; d["patterns"][15]["tracks"] = {"CY": "1" * 16}
d["song"] = [5, 5, 15, 0]; d["kit"][3] = [1, 2, 3, 4, 5]
img = core.to_image(d)
check(len(img) == core.SAVE_SIZE == 4247, "image is 4247 bytes")
back = core.from_image(img)
check(back["bpm"] == 133.5 and back["swing"] == 44 and back["songMode"] and back["padGm"] and back["padBase"] == 48 and back["selPat"] == 5 and back["song"] == [5, 5, 15, 0] and back["kit"][3] == [1, 2, 3, 4, 5], "settings, kit and song survive the image")
check(back["patterns"][5]["tracks"]["BD"] == "3...2...2..." + "....", "pattern steps survive (padded to 16)") 
check(back["patterns"][5]["len"] == 12 and back["patterns"][15]["tracks"]["CY"] == "1" * 16, "pattern length and last slot survive")
check(core.to_image(back) == img, "document -> image -> document -> image is byte-identical")
bad = bytearray(img); bad[1000] ^= 1
check(raises(lambda: core.from_image(bytes(bad)), "CRC"), "a flipped bit is rejected by the CRC")
check(raises(lambda: core.from_image(img[:-1]), "bytes") and raises(lambda: core.from_image(b"X" * 4247), "header"), "wrong size / header rejected")

# ---- 2. validation
check(raises(lambda: core.validate({"bpm": 10}), "bpm") and raises(lambda: core.validate({"patterns": [{"tracks": {"XX": "3"}}]}), "unknown instrument")
      and raises(lambda: core.validate({"patterns": [{"tracks": {"BD": "9"}}]}), "may only contain") and raises(lambda: core.validate({"song": [16]}), "0..15")
      and raises(lambda: core.validate({"kit": [[1, 2, 3]]}), "kit") and raises(lambda: core.validate({"song": [0] * 33}), "32"), "bad documents give readable errors")
v = core.validate({"bpm": 100, "vol": 999, "patterns": [{"len": 99, "tracks": {"BD": "30"}}]})
check(v["vol"] == 127 and v["patterns"][0]["len"] == 16 and v["patterns"][0]["tracks"]["BD"] == "3" + "." * 15, "values are clamped, short tracks padded")

# ---- 3. the image against the REAL firmware: the C++ engine accepts exactly what Python writes (the renderer deserializes it, CRC included)
cache = tempfile.mkdtemp(prefix="dc_test_"); eng = engine.Engine(cache)
out = os.path.join(cache, "o.wav")
info = eng.render(d, ["pattern", 5, 1], out)
check(info["secs"] > 0 and info["peak"] > 1000 and info["clipped"] == 0, "firmware engine loads the Python image and plays it (peak %d)" % info["peak"])
bad_path = os.path.join(cache, "bad.bin"); open(bad_path, "wb").write(bytes(bad))
p = subprocess.run([eng.exe, bad_path, out, "hit", "0", "100"], capture_output=True, text=True)
check(p.returncode == 3 and "not a valid drum image" in p.stderr, "firmware engine refuses the damaged image")

# ---- 4. audio: lengths and the seamless loop
def wavread(path):
    w = wave.open(path); n = w.getnframes(); import array
    a = array.array("h"); a.frombytes(w.readframes(n)); return w.getframerate(), a
g = core.new_document("g"); g["bpm"] = 120; g["patterns"][0]["tracks"] = {"BD": "3...2...2...2...", "CH": "2.2.2.2.2.2.2.2."}
i1 = eng.render(g, ["pattern", 0, 2], out)
check(abs(i1["secs"] - (2 * 16 * 0.125 + 1.2)) < 0.01, "2 bars at 120 BPM = 4.0 s + 1.2 s tail (%.2f)" % i1["secs"])
i2 = eng.render(g, ["pattern", 0, 2, "loop"], out); sr, a = wavread(out)
check(abs(len(a) / 2 / sr - 4.0) < 0.001, "loop render is exactly two bars (%.3f s)" % (len(a) / 2 / sr))
check(max(abs(x) for x in a[:2000]) > 1000 and max(abs(x) for x in a[-2000:]) < 32767, "loop render has sound at the start and does not clip")
iL = eng.render(g, ["pattern", 0, 2, "loop"], out); iN = eng.render(g, ["pattern", 0, 2], out)
check(iL["peak"] <= iN["peak"] * 1.15 and iL["clipped"] == 0, "loop seam does not double the first hit (peak %d vs %d unlooped)" % (iL["peak"], iN["peak"]))
s_empty = core.new_document("e"); check(raises(lambda: eng.render(s_empty, ["song"], out), "empty"), "song mode with an empty chain says so")
g["song"] = [0, 0]; i3 = eng.render(g, ["song"], out); check(abs(i3["secs"] - (2 * 16 * 0.125 + 1.2)) < 0.01, "song of two patterns = 4 s + tail")
i4 = eng.render(g, ["hit", 8, 100], out); check(i4["secs"] == 1.6 and i4["peak"] > 500, "single hit renders 1.6 s")
i5 = eng.render(g, ["kit", 110], out); check(i5["secs"] > 14 and i5["clipped"] == 0, "kit preview plays all 16 drums without clipping (peak %d)" % i5["peak"])
# swing and kit change the sound
k = core.new_document("k"); k["patterns"][0]["tracks"] = {"BD": "3......."}
def tail_rms(dec):
    eng.render(dict(k, kit=[[123, 64, dec, 40, 64]] + core.DEFAULT_KIT[1:]), ["hit", 0, 110], out); sr_, a_ = wavread(out)
    t = a_[2 * int(0.6 * sr_):]; return (sum(x * x for x in t) / len(t)) ** 0.5
r5, r100 = tail_rms(5), tail_rms(100)
check(r100 > 3 * r5 + 1, "kit DECAY reaches the engine: the kick rings on after 0.6 s only with a long decay (rms %.0f vs %.0f)" % (r5, r100))

# ---- 5. library
lib = core.library()
check(len(lib["patterns"]) >= 40 and len(lib["kits"]) >= 8 and len(lib["sounds"]) >= 100, "shipped library: %d patterns, %d kits, %d sounds" % (len(lib["patterns"]), len(lib["kits"]), len(lib["sounds"])))
ok = True
for pt in lib["patterns"]:
    try:
        dd = core.new_document("p"); dd["patterns"][0] = {"name": pt["name"], "len": pt["len"], "tracks": pt["tracks"]}; core.to_image(dd)
    except DrumError: ok = False
check(ok, "every library pattern is a valid pattern")
names = [pt["name"] for pt in lib["patterns"]]; check(len(set(names)) == len(names), "pattern names are unique")
check(all(any(c != "." for t in pt["tracks"].values() for c in t) for pt in lib["patterns"]), "no library pattern is silent")
bad_pairs = [pt["name"] for pt in lib["patterns"] if "CH" in pt["tracks"] and "OH" in pt["tracks"] and any(a != "." and b != "." for a, b in zip(pt["tracks"]["CH"], pt["tracks"]["OH"]))]
check(not bad_pairs, "no closed hat on the step of an open hat (it would choke it): %s" % bad_pairs)
# every shipped kit and every pattern renders without clipping
clip = []
for kt in lib["kits"]:
    dd = core.new_document("k"); dd["kit"] = kt["kit"]
    for q in lib["patterns"][:0]: pass
    dd["patterns"][0]["tracks"] = {l: "3..." * 4 for l in core.INST}                  # worst case: all 16 drums hit together on every beat, accented
    r = eng.render(dd, ["pattern", 0, 1], out)
    if r["clipped"]: clip.append(kt["name"])
check(not clip, "all 16 drums accented together on the beat: no clipping in any shipped kit %s" % clip)
clip = []; quiet = []
for pt in lib["patterns"]:
    dd = core.new_document("p"); dd["bpm"] = pt["bpm"]; dd["swing"] = pt["swing"]; dd["patterns"][0] = {"name": pt["name"], "len": pt["len"], "tracks": pt["tracks"]}
    r = eng.render(dd, ["pattern", 0, 1], out)
    if r["clipped"]: clip.append(pt["name"])
    if r["peak"] < 4000: quiet.append(pt["name"])
check(not clip and not quiet, "every library pattern renders with the factory kit: none clipped %s, none silent %s" % (clip, quiet))
# user library lives in library/user: use a temp location
core.LIB_USER = os.path.join(cache, "user"); core.PROJECTS = os.path.join(cache, "projects")
e1 = core.save_user_entry("patterns", {"name": "mine", "tracks": {"BD": "3..."}, "bpm": 99}); e2 = core.save_user_entry("patterns", {"name": "mine", "tracks": {"SD": "3"}, "bpm": 99})
l2 = core.library(); mine = [p for p in l2["patterns"] if p.get("user")]
check(len(mine) == 1 and mine[0]["tracks"] == {"SD": "3" + "." * 15}, "saving under the same name replaces the user's entry")
core.save_user_entry("sounds", {"name": "boom", "inst": "BD", "values": [100, 50, 90, 20, 64]}); core.save_user_entry("kits", {"name": "kk", "kit": core.DEFAULT_KIT})
check(any(s.get("user") for s in core.library()["sounds"]) and any(s.get("user") for s in core.library()["kits"]), "user sounds and kits are saved")
core.delete_user_entry("patterns", "mine"); check(not [p for p in core.library()["patterns"] if p.get("user")], "delete removes it")
check(raises(lambda: core.delete_user_entry("patterns", "nope")) and raises(lambda: core.save_user_entry("patterns", {"name": ""})) and raises(lambda: core.save_user_entry("sounds", {"name": "x", "inst": "ZZ", "values": [1] * 5})), "library errors are readable")
check("/" not in core.safe_project_name("../../etc/passwd") and "\\" not in core.safe_project_name("..\\x") and not core.safe_project_name("../../etc/passwd").startswith("."), "project names cannot escape the folder")
core.save_project(dict(d, name="proj 1")); check("proj 1" in core.list_projects() and core.load_project("proj 1")["bpm"] == 133.5, "project save / open")

# ---- 6. the Flask API
ap = appmod.create_app(cache); c = ap.test_client()
m = c.get("/api/meta").get_json(); check(m["inst"] == core.INST and m["image_size"] == 4247 and len(m["library"]["patterns"]) >= 40, "GET /api/meta")
r = c.post("/api/render", json={"doc": g, "mode": "pattern", "pat": 0, "bars": 1, "loop": True}); check(r.status_code == 200 and r.data[:4] == b"RIFF" and "peak=" in r.headers["X-Render"], "POST /api/render gives a WAV")
r = c.post("/api/render", json={"doc": g, "mode": "nonsense"}); check(r.status_code == 400 and "unknown render mode" in r.get_json()["error"], "bad render mode -> readable 400")
r = c.post("/api/render", json={"doc": {"bpm": 5}, "mode": "hit"}); check(r.status_code == 400 and "bpm" in r.get_json()["error"], "invalid document -> readable 400")
r = c.post("/api/image", json={"doc": d}); check(r.data == img, "POST /api/image returns the machine image")
r = c.post("/api/open_image", data=img); check(r.get_json()["doc"]["bpm"] == 133.5, "POST /api/open_image")
r = c.post("/api/open_image", data=b"junk"); check(r.status_code == 400, "junk image -> 400")
r = c.post("/api/library/patterns", json={"entry": {"name": "api pat", "tracks": {"BD": "3"}}}); check(r.status_code == 200 and any(p["name"] == "api pat" for p in r.get_json()["library"]["patterns"]), "POST /api/library")
r = c.delete("/api/library/patterns?name=api%20pat"); check(r.status_code == 200 and not any(p["name"] == "api pat" for p in r.get_json()["library"]["patterns"]), "DELETE /api/library")
check(c.get("/").status_code == 200 and b"picomusic drum editor" in c.get("/").data, "the page is served")

# ---- 7. the USB protocol against a fake machine that answers like main.cpp
class FakeMachine:
    """Mimics runCommand() for: stop, dump, image <hex>, save, hit. Banner/log lines are mixed in on purpose."""
    def __init__(self): self.rx = b""; self.out = []; self.state = core.to_image(core.new_document("fw")); self.playing = True; self.saved = None; self.hits = []
    def reset_input_buffer(self): self.out = []
    def close(self): pass
    def write(self, b):
        for line in b.decode().splitlines(): self.cmd(line)
    def say(self, s): self.out.append((s + "\r\n").encode())
    def cmd(self, l):
        if l == "stop": self.playing = False; self.say("some log line"); self.say("stop")
        elif l == "dump": self.say("[note] noise"); self.say("IMAGE " + self.state.hex())
        elif l.startswith("image "):
            if self.playing: return self.say("image refused: stop first")
            try:
                b = bytes.fromhex(l[6:]); core.from_image(b)
            except (ValueError, DrumError): return self.say("image bad: wrong size, wrong header or CRC mismatch (nothing changed)")
            self.state = b; self.say("image ok")
        elif l == "save": self.saved = self.state; self.say("saved")
        elif l.startswith("hit "): self.hits.append(l)
    def readline(self): return self.out.pop(0) if self.out else b""
fm = FakeMachine(); lk = device.Link(transport=fm, timeout=0.3)
lk.send(d, flash=True); check(fm.state == img and fm.saved == img and not fm.playing, "send: stops the machine, sends the image, saves to flash")
check(lk.fetch()["bpm"] == 133.5, "fetch: reads the machine's state back (skipping log lines)")
fm.playing = True; fm.cmd = (lambda orig: (lambda l: None if l == "stop" else orig(l)))(fm.cmd)         # a machine that ignores stop (playing) refuses the image
check(raises(lambda: lk.send(d), "refused"), "a refused image is reported with the machine's reason")
lk.hit(3, 99); check(fm.hits == ["hit 3 99"], "hit command")
class Dead(FakeMachine):
    def cmd(self, l): pass
check(raises(lambda: device.Link(transport=Dead(), timeout=0.2).fetch(), "no answer"), "silent machine -> readable timeout")

# ---- 8. the page's script is syntactically valid JavaScript
html = open(os.path.join(TOOL, "drumconf", "static", "index.html"), encoding="utf-8").read()
js = re.search(r"<script>(.*)</script>", html, re.S).group(1)
if shutil.which("node"):
    jp = os.path.join(cache, "page.js"); open(jp, "w").write(js)
    p = subprocess.run(["node", "--check", jp], capture_output=True, text=True); check(p.returncode == 0, "frontend script passes node --check %s" % p.stderr[:200])
else: print("SKIP node not installed: frontend syntax not checked")
ids = set(re.findall(r'\$\("([A-Za-z]+)"\)', js)); have = set(re.findall(r'id="([A-Za-z]+)"', html)); missing = sorted(ids - have)
check(not missing, "every element the script uses exists in the page %s" % missing)

shutil.rmtree(cache, ignore_errors=True)
print("\nALL EDITOR TESTS PASS" if not fails else "\n%d EDITOR TESTS FAILED" % fails); sys.exit(1 if fails else 0)
