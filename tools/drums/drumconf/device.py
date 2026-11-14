"""USB serial link to the drum machine (the Pico's own USB port, 115200 baud). Uses the firmware commands  dump  image <hex>  save  hit  stop.
pyserial is optional: without it the rest of the editor works and these calls report what to install."""
import time
from . import core
from .core import DrumError

def _serial():
    try:
        import serial, serial.tools.list_ports
        return serial
    except ImportError:
        raise DrumError("pyserial is not installed (python3 -m pip install pyserial): needed only to talk to the machine")

def list_ports():
    s = _serial()
    out = []
    for p in s.tools.list_ports.comports():
        out.append({"port": p.device, "desc": p.description or "", "pico": p.vid == 0x2E8A or "pico" in (p.description or "").lower() or "midi" in (p.description or "").lower()})
    return sorted(out, key=lambda x: (not x["pico"], x["port"]))

class Link:
    """transport: anything with write(bytes), readline() -> bytes, reset_input_buffer(), close(); default = a real serial port."""
    def __init__(self, port=None, transport=None, timeout=3.0):
        if transport is None:
            s = _serial()
            try: transport = s.Serial(port, 115200, timeout=timeout)
            except Exception as e: raise DrumError("cannot open %s: %s" % (port, e))
        self.t = transport; self.timeout = timeout

    def close(self):
        try: self.t.close()
        except Exception: pass

    def _cmd(self, line, until, timeout=None):
        """Send one line, return the first answer line that satisfies until(line); other lines (log output, banner) are skipped."""
        self.t.reset_input_buffer()
        self.t.write((line + "\n").encode("ascii"))
        end = time.time() + (timeout or self.timeout)
        while time.time() < end:
            raw = self.t.readline()
            if not raw: continue
            s = raw.decode("ascii", "replace").strip()
            if until(s): return s
        raise DrumError("no answer from the machine to %r (is this the drum firmware, and the right port?)" % line.split()[0])

    def fetch(self):
        """The machine's whole state as a document."""
        s = self._cmd("dump", lambda x: x.startswith("IMAGE "), timeout=self.timeout + 3)
        try: img = bytes.fromhex(s[6:].strip())
        except ValueError: raise DrumError("the machine answered with something that is not hex")
        return core.from_image(img, name="from machine")

    def send(self, doc, flash=False):
        """Replace the machine's state with doc. It must be stopped (the firmware refuses while playing): we send `stop` first. flash=True also runs `save`."""
        img = core.to_image(doc)
        try: self._cmd("stop", lambda x: x == "stop", timeout=1.0)
        except DrumError: pass                                       # an old firmware, or already stopped: the image command decides
        a = self._cmd("image " + img.hex(), lambda x: x.startswith("image "), timeout=self.timeout + 3)
        if a != "image ok": raise DrumError("the machine refused the image: " + a)
        if flash:
            a = self._cmd("save", lambda x: x in ("saved", "SAVE FAILED") or x.startswith("stop first"), timeout=6.0)
            if a != "saved": raise DrumError("the machine could not save to flash: " + a)
        return True

    def hit(self, inst, vel=100):
        self.t.write(("hit %d %d\n" % (inst, vel)).encode("ascii"))
    def play(self, on=True):
        self.t.write((b"play\n" if on else b"stop\n"))
