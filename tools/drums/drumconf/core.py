"""Data model and the machine's 4247-byte image (the same bytes the firmware saves to flash, see DrumManager::serialize).

A *document* is a plain dict (JSON in the browser, on disk and over the API):

  {"name": str, "bpm": 30..300, "swing": 0..127, "vol": 0..127, "drive": 0..127,
   "songMode": bool, "extSync": bool, "padGm": bool, "padBase": 0..127, "selPat": 0..15,
   "kit":      [16 x [level, tune, decay, tone, pan]]         0..127 each, in instrument order BD SD LT ...
   "patterns": [16 x {"name": str, "len": 1..16, "tracks": {"BD": "3...2...2...2...", ...}}]   . off  1 ghost  2 normal  3 accent
   "song":     [pattern index 0..15, ...]  (at most 32; the chain, empty = no song)}

Pattern and kit names exist only in the document: the image on the machine has no names.
"""
import copy, json, os, re, struct, zlib

INST = ["BD", "SD", "LT", "MT", "HT", "RS", "CP", "CB", "CH", "OH", "CY", "CL", "MA", "LC", "MC", "HC"]
INST_NAME = ["Bass Drum", "Snare", "Low Tom", "Mid Tom", "High Tom", "Rim Shot", "Hand Clap", "Cowbell",
             "Closed Hat", "Open Hat", "Cymbal", "Claves", "Maracas", "Low Conga", "Mid Conga", "High Conga"]
KIT_FIELDS = ["level", "tune", "decay", "tone", "pan"]
N_INST, N_STEPS, N_PATTERNS, SONG_MAX, N_INST_PARAMS = 16, 16, 16, 32, 5
SAVE_SIZE = 4 + 5 + N_INST * N_INST_PARAMS + 4 + 4 + 2 + N_PATTERNS + N_PATTERNS * N_INST * N_STEPS + SONG_MAX + 4     # 4247
TEMPO_KNOB_BASE = 60            # config.h: bpm = 60 + knob (the image stores the exact float, the knob is only the display value)

# The factory kit (drum_params.h KIT): LEVEL TUNE DECAY TONE PAN
DEFAULT_KIT = [[123, 64, 45, 40, 64], [107, 64, 55, 75, 64], [104, 64, 60, 35, 36], [104, 64, 55, 35, 52], [103, 64, 50, 35, 72],
               [94, 64, 50, 64, 58], [102, 64, 55, 70, 70], [92, 64, 60, 64, 84], [105, 64, 30, 64, 78], [94, 64, 55, 64, 84],
               [77, 64, 65, 64, 92], [89, 64, 50, 64, 46], [79, 64, 40, 64, 98], [100, 64, 60, 35, 26], [100, 64, 55, 35, 40], [100, 64, 50, 35, 56]]

class DrumError(ValueError):
    pass

def clamp(v, lo, hi):
    return max(lo, min(hi, int(round(v))))

def empty_pattern(name="empty"):
    return {"name": name, "len": N_STEPS, "tracks": {}}

def new_document(name="new"):
    return {"name": name, "bpm": 120.0, "swing": 0, "vol": 100, "drive": 0, "songMode": False, "extSync": False, "padGm": False, "padBase": 36,
            "selPat": 0, "kit": copy.deepcopy(DEFAULT_KIT), "patterns": [empty_pattern("pattern %d" % (i + 1)) for i in range(N_PATTERNS)], "song": []}

def track_string(s):
    """'3...2...' -> exactly 16 characters of . 1 2 3 (shorter strings are padded with '.', anything else is an error)."""
    s = (s or "").strip()
    if not re.fullmatch(r"[.0123]*", s): raise DrumError("a track may only contain . 0 1 2 3, got %r" % s)
    if len(s) > N_STEPS: raise DrumError("a track has at most %d steps, got %d" % (N_STEPS, len(s)))
    return s.replace("0", ".").ljust(N_STEPS, ".")

def validate(doc):
    """Normalise a document (fill defaults, clamp, check shapes). Returns a NEW dict; raises DrumError with a readable message."""
    d = new_document(str(doc.get("name", "new"))[:60])
    try:
        d["bpm"] = float(doc.get("bpm", 120.0))
        for k in ("swing", "vol", "drive"): d[k] = clamp(doc.get(k, d[k]), 0, 127)
        d["padBase"] = clamp(doc.get("padBase", 36), 0, 127 - N_INST + 1)
        d["selPat"] = clamp(doc.get("selPat", 0), 0, N_PATTERNS - 1)
        for k in ("songMode", "extSync", "padGm"): d[k] = bool(doc.get(k, False))
    except (TypeError, ValueError) as e:
        raise DrumError("bad number in the document: %s" % e)
    if not 30.0 <= d["bpm"] <= 300.0: raise DrumError("bpm must be 30..300, got %s" % d["bpm"])
    kit = doc.get("kit", DEFAULT_KIT)
    if len(kit) != N_INST or any(len(r) != N_INST_PARAMS for r in kit): raise DrumError("kit must be 16 instruments x 5 values")
    d["kit"] = [[clamp(v, 0, 127) for v in r] for r in kit]
    pats = doc.get("patterns", [])
    if len(pats) > N_PATTERNS: raise DrumError("at most %d patterns" % N_PATTERNS)
    for i, p in enumerate(pats):
        tr = {}
        for k, v in (p.get("tracks") or {}).items():
            if k not in INST: raise DrumError("pattern %d: unknown instrument %r (use %s)" % (i + 1, k, " ".join(INST)))
            t = track_string(v)
            if t != "." * N_STEPS: tr[k] = t
        d["patterns"][i] = {"name": str(p.get("name", "pattern %d" % (i + 1)))[:40], "len": clamp(p.get("len", N_STEPS), 1, N_STEPS), "tracks": tr}
    song = [int(x) for x in (doc.get("song") or [])]
    if len(song) > SONG_MAX: raise DrumError("the song chain has at most %d slots" % SONG_MAX)
    if any(not 0 <= x < N_PATTERNS for x in song): raise DrumError("song entries must be pattern numbers 0..15")
    d["song"] = song
    return d

def to_image(doc):
    """document -> the 4247-byte image the firmware loads (same layout and CRC as DrumManager::serialize)."""
    d = validate(doc)
    b = bytearray(b"DRM\x01" + bytes([N_INST, N_STEPS, N_PATTERNS, SONG_MAX, N_INST_PARAMS]))
    for r in d["kit"]: b += bytes(r)
    b += struct.pack("<f", d["bpm"])
    b += bytes([d["swing"], d["vol"], d["drive"], (1 if d["songMode"] else 0) | (2 if d["extSync"] else 0) | (4 if d["padGm"] else 0), d["padBase"], d["selPat"]])
    b += bytes(p["len"] for p in d["patterns"])
    for p in d["patterns"]:
        for i, lab in enumerate(INST):
            t = p["tracks"].get(lab, "." * N_STEPS)
            b += bytes(0 if c == "." else int(c) for c in t)
    b += bytes(d["song"] + [0xFF] * (SONG_MAX - len(d["song"])))
    b += struct.pack("<I", zlib.crc32(bytes(b)) & 0xFFFFFFFF)
    assert len(b) == SAVE_SIZE
    return bytes(b)

def from_image(b, name="from machine"):
    """4247 bytes -> document. Raises DrumError for a wrong size, header or CRC (the firmware refuses the same images)."""
    b = bytes(b)
    if len(b) != SAVE_SIZE: raise DrumError("an image is %d bytes, got %d" % (SAVE_SIZE, len(b)))
    if b[:4] != b"DRM\x01" or tuple(b[4:9]) != (N_INST, N_STEPS, N_PATTERNS, SONG_MAX, N_INST_PARAMS): raise DrumError("not a picomusic drum image (header mismatch)")
    if struct.unpack("<I", b[-4:])[0] != zlib.crc32(b[:-4]) & 0xFFFFFFFF: raise DrumError("CRC mismatch: the image is damaged")
    n = 9
    kit = [list(b[n + i * 5:n + i * 5 + 5]) for i in range(N_INST)]; n += 80
    bpm = struct.unpack("<f", b[n:n + 4])[0]; n += 4
    sw, vo, dr, fl, pb, sp = b[n:n + 6]; n += 6
    plen = list(b[n:n + N_PATTERNS]); n += N_PATTERNS
    pats = []
    for p in range(N_PATTERNS):
        tr = {}
        for i, lab in enumerate(INST):
            row = b[n:n + N_STEPS]; n += N_STEPS
            if any(row): tr[lab] = "".join("." if v == 0 or v > 3 else str(v) for v in row)
        pats.append({"name": "pattern %d" % (p + 1), "len": plen[p] if 1 <= plen[p] <= N_STEPS else N_STEPS, "tracks": tr})
    song = []
    for v in b[n:n + SONG_MAX]:
        if v >= N_PATTERNS: break
        song.append(v)
    return validate({"name": name, "bpm": round(bpm, 2), "swing": sw, "vol": vo, "drive": dr, "songMode": bool(fl & 1), "extSync": bool(fl & 2), "padGm": bool(fl & 4),
                     "padBase": pb, "selPat": sp, "kit": kit, "patterns": pats, "song": song})

def swing_percent(knob):
    """Display value for the SWING knob: 50 % = straight, knob 127 = 75 % (config.h SWING_MAX 0.25)."""
    return 50.0 + 25.0 * knob / 127.0

# ---------------------------------------------------------------- library
HERE = os.path.dirname(os.path.abspath(__file__))
TOOL = os.path.dirname(HERE)
LIB_SHIPPED = os.path.join(TOOL, "library")
LIB_USER = os.path.join(TOOL, "library", "user")
PROJECTS = os.path.join(TOOL, "projects")

def _load(path, default):
    try:
        with open(path, encoding="utf-8") as f: return json.load(f)
    except (OSError, ValueError): return default

def library():
    """{patterns, kits, sounds}: the shipped library plus the user's own entries (library/user/*.json), each entry tagged user: true/false."""
    out = {}
    for kind in ("patterns", "kits", "sounds"):
        items = [dict(i, user=False) for i in _load(os.path.join(LIB_SHIPPED, kind + ".json"), [])]
        items += [dict(i, user=True) for i in _load(os.path.join(LIB_USER, kind + ".json"), [])]
        out[kind] = items
    return out

def _check_entry(kind, e):
    name = str(e.get("name", "")).strip()[:50]
    if not name: raise DrumError("give it a name")
    if kind == "patterns":
        tr = {}
        for k, v in (e.get("tracks") or {}).items():
            if k not in INST: raise DrumError("unknown instrument %r" % k)
            t = track_string(v)
            if t != "." * N_STEPS: tr[k] = t
        return {"name": name, "category": str(e.get("category", "user"))[:30], "bpm": clamp(e.get("bpm", 120), 30, 300), "swing": clamp(e.get("swing", 0), 0, 127),
                "len": clamp(e.get("len", N_STEPS), 1, N_STEPS), "tracks": tr, "note": str(e.get("note", ""))[:200]}
    if kind == "kits":
        kit = e.get("kit")
        if not kit or len(kit) != N_INST or any(len(r) != N_INST_PARAMS for r in kit): raise DrumError("a kit is 16 x 5 values")
        return {"name": name, "kit": [[clamp(v, 0, 127) for v in r] for r in kit], "note": str(e.get("note", ""))[:200]}
    if kind == "sounds":
        inst = e.get("inst")
        if inst not in INST: raise DrumError("unknown instrument %r" % inst)
        v = e.get("values")
        if not v or len(v) != N_INST_PARAMS: raise DrumError("a sound is 5 values (level tune decay tone pan)")
        return {"name": name, "inst": inst, "values": [clamp(x, 0, 127) for x in v], "note": str(e.get("note", ""))[:200]}
    raise DrumError("unknown library section %r" % kind)

def save_user_entry(kind, entry):
    """Add or replace (by name) one of the user's own library entries. Returns the cleaned entry."""
    e = _check_entry(kind, entry)
    os.makedirs(LIB_USER, exist_ok=True)
    path = os.path.join(LIB_USER, kind + ".json")
    items = [i for i in _load(path, []) if not (i.get("name") == e["name"] and i.get("inst") == e.get("inst"))]
    items.append(e)
    _atomic_json(path, items)
    return e

def delete_user_entry(kind, name, inst=None):
    path = os.path.join(LIB_USER, kind + ".json")
    items = _load(path, []); keep = [i for i in items if not (i.get("name") == name and (inst is None or i.get("inst") == inst))]
    if len(keep) == len(items): raise DrumError("no such entry of yours: %r" % name)
    _atomic_json(path, keep)

def _atomic_json(path, obj):
    tmp = path + ".tmp"
    with open(tmp, "w", encoding="utf-8") as f: json.dump(obj, f, indent=1, ensure_ascii=False)
    os.replace(tmp, path)

def safe_project_name(name):
    n = re.sub(r"[^A-Za-z0-9._ -]", "_", str(name)).strip(" .")[:60]
    if not n: raise DrumError("give the project a name")
    return n

def list_projects():
    try: return sorted(f[:-5] for f in os.listdir(PROJECTS) if f.endswith(".json"))
    except OSError: return []

def save_project(doc):
    d = validate(doc); os.makedirs(PROJECTS, exist_ok=True)
    _atomic_json(os.path.join(PROJECTS, safe_project_name(d["name"]) + ".json"), d)
    return d

def load_project(name):
    p = os.path.join(PROJECTS, safe_project_name(name) + ".json")
    try: return validate(_load(p, None) or (_ for _ in ()).throw(DrumError("no such project: %s" % name)))
    except OSError: raise DrumError("no such project: %s" % name)
