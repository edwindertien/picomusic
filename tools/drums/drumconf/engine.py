"""Builds and runs engine/drum_render.cpp: the firmware's real DrumManager compiled for this PC (g++ needed, same as the piano tool)."""
import hashlib, os, shutil, subprocess, tempfile
from . import core
from .core import DrumError

def _find_root():
    """Repository root (has firmware/drums), or None for a flat copy of the project (src/ next to tools/)."""
    r = os.path.dirname(core.TOOL)                                  # tools/drums -> tools
    r = os.path.dirname(r)                                          # -> repo root
    return r if os.path.isdir(os.path.join(r, "firmware", "drums")) else None

ROOT = _find_root()
SKIP = {"audio_driver.h", "midi_handler.h", "usb_host.h"}           # hardware-bound: the host mocks from tests/drums/plat replace them

def _sources():
    """(dir with the firmware sources, dir with the mocks and plat headers)"""
    if ROOT is None: raise DrumError("engine audition needs the repository layout (firmware/drums, firmware/shared, tests/drums)")
    return ROOT

class Engine:
    def __init__(self, cache=None):
        self.cache = cache or os.path.join(tempfile.gettempdir(), "drumconf_engine")
        self.exe = None; self.key = None

    def _stage(self):
        r = _sources(); st = os.path.join(self.cache, "stage"); shutil.rmtree(st, ignore_errors=True); os.makedirs(st)
        for d in ("firmware/shared", "firmware/drums"):
            for f in os.listdir(os.path.join(r, d)):
                p = os.path.join(r, d, f)
                if os.path.isfile(p) and f not in SKIP: shutil.copy(p, st)
        plat = os.path.join(r, "tests", "drums", "plat")
        for f in os.listdir(plat): shutil.copy(os.path.join(plat, f), st)
        shutil.copy(os.path.join(core.TOOL, "engine", "drum_render.cpp"), st)
        return st, os.path.join(r, "tests", "drums", "mock")

    def _fingerprint(self):
        r = _sources(); h = hashlib.sha1()
        for d in ("firmware/shared", "firmware/drums"):
            for f in sorted(os.listdir(os.path.join(r, d))):
                p = os.path.join(r, d, f)
                if os.path.isfile(p): h.update(f.encode()); h.update(open(p, "rb").read())
        h.update(open(os.path.join(core.TOOL, "engine", "drum_render.cpp"), "rb").read())
        return h.hexdigest()

    def ensure(self):
        key = self._fingerprint()
        exe = os.path.join(self.cache, "drum_render")
        if self.exe and self.key == key and os.path.exists(exe): return exe
        if shutil.which("g++") is None: raise DrumError("g++ was not found: the audition engine is built from the firmware sources (install build-essential / Xcode command line tools)")
        os.makedirs(self.cache, exist_ok=True)
        st, mock = self._stage()
        cmd = ["g++", "-std=gnu++17", "-O2", "-I", mock, "-I", st, os.path.join(st, "drum_render.cpp"), os.path.join(st, "wavetable.cpp"),
               os.path.join(mock, "mock_globals.cpp"), os.path.join(mock, "mock_globals2.cpp"), "-o", exe]
        p = subprocess.run(cmd, capture_output=True, text=True)
        if p.returncode: raise DrumError("could not build the audition engine:\n" + (p.stderr or p.stdout)[-1500:])
        self.exe, self.key = exe, key
        return exe

    def render(self, doc, args, out_wav):
        """doc: a document dict; args: e.g. ['pattern', '0', '2']. Returns {'peak', 'secs', 'clipped'}; the WAV is written to out_wav."""
        exe = self.ensure()
        img = os.path.join(self.cache, "in.bin")
        with open(img, "wb") as f: f.write(core.to_image(doc))
        p = subprocess.run([exe, img, out_wav] + [str(a) for a in args], capture_output=True, text=True, timeout=120)
        if p.returncode: raise DrumError((p.stderr or "render failed").strip())
        t = p.stdout.split()
        return {"peak": int(t[1]), "secs": float(t[3]), "clipped": int(t[5])}
