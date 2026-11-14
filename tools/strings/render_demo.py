"""Renders docs/demo_strings.wav (stereo, 44.1 kHz, 16 bit) with the REAL engine: a short tour of the four patches.  Run from the repo root."""
import sys, wave, numpy as np
sys.path.insert(0, 'host_test')
from dsp_common import build, probe, read_stereo, FS
build(); sc = []
def adv(sec): sc.append(f"run {int(sec * FS)}")
def chord(notes, hold, tail):
    sc.extend(f"on {n}" for n in notes); adv(hold); sc.extend(f"off {n}" for n in notes); adv(tail)
# 1. Ensemble patch: Am - F - C
sc += ["patch 0"]
for ch in ((45, 52, 57, 60, 64), (41, 48, 53, 57, 60), (48, 55, 60, 64, 67)): chord(ch, 2.8, 0.5)
sc += ["panic"]; adv(0.5)
# 2. Violins with vibrato and the sustain pedal
sc += ["patch 2", "set vib 45", "pedal 1"]
for n, d in ((76, 1.0), (74, 0.5), (72, 0.5), (71, 1.2), (69, 2.0)): sc += [f"on {n}"]; adv(d); sc += [f"off {n}"]
adv(1.0); sc += ["pedal 0"]; adv(2.0); sc += ["panic"]; adv(0.4)
# 3. Cello + bass line
sc += ["patch 1"]
for n in (45, 43, 41, 40): sc += [f"on {n}", f"on {n + 12}"]; adv(1.4); sc += [f"off {n}", f"off {n + 12}"]; adv(0.15)
adv(1.5); sc += ["panic"]; adv(0.3)
# 4. Pizzicato arpeggio
sc += ["patch 3"]
for _ in range(2):
    for n in (45, 52, 57, 60, 64, 69, 64, 60): sc += [f"on {n}"]; adv(0.12); sc += [f"off {n}"]; adv(0.12)
adv(1.2); sc += ["panic"]; adv(0.3)
# 5. big slow chord, pedal, ensemble
sc += ["patch 0", "set atk 95", "set rel 105", "set emix 127", "pedal 1"]
sc += [f"on {n}" for n in (33, 40, 45, 52, 57, 60, 64, 69, 71)]; adv(3.5); sc += [f"off {n}" for n in (33, 40, 45, 52, 57, 60, 64, 69, 71)]; adv(0.5); sc += ["pedal 0"]; adv(5.0)
probe("\n".join(sc) + "\nwrite /tmp/demo.raw\n"); d = read_stereo('/tmp/demo.raw'); d *= 0.9 * 32767 / max(abs(d).max(), 1)
w = wave.open('docs/demo_strings.wav', 'wb'); w.setnchannels(2); w.setsampwidth(2); w.setframerate(FS); w.writeframes(d.astype('<i2').tobytes()); w.close()
print(f"docs/demo_strings.wav written: {len(d)/FS:.1f} s, peak normalised to -0.9 dB")
