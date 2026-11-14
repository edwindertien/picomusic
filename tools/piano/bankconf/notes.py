import re
_SEMI = {"C": 0, "D": 2, "E": 4, "F": 5, "G": 7, "A": 9, "B": 11}
def parse_note(s):
    """'A0' 'C#4' 'Ds1' 'Fs7' 'Bb2' or a plain number -> MIDI key (C4 = 60, A0 = 21)"""
    if isinstance(s, (int, float)): return int(s)
    t = str(s).strip()
    if re.fullmatch(r"-?\d+", t): return int(t)
    m = re.fullmatch(r"([A-Ga-g])([#sSbB]?)(-?\d+)", t)
    if not m: raise ValueError(f"not a note name: {s!r}")
    n = _SEMI[m.group(1).upper()] + (1 if m.group(2) in "#sS" and m.group(2) else -1 if m.group(2) in "bB" and m.group(2) else 0)
    return 12 * (int(m.group(3)) + 1) + n
_NAMES = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"]
def note_name(k): return f"{_NAMES[k % 12]}{k // 12 - 1}"
def is_black(k): return k % 12 in (1, 3, 6, 8, 10)
