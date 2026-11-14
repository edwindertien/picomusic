# Calibrate the default LEVEL of every instrument so that the peak POWER (sqrt(L^2+R^2), independent of pan)
# of a full-velocity hit reaches a target. Targets are musical, not equal: kick biggest, hats / maracas small.
#   python3 calibrate_kit.py          report
#   python3 calibrate_kit.py --apply  rewrite the LEVEL column of KIT[] in src/drum_params.h
import sys, re; sys.path.insert(0, '.')
import numpy as np, dsp_common as d
from drum_names import NAMES
TARGET = [0.70, 0.60, 0.48, 0.48, 0.48, 0.42, 0.50, 0.36, 0.28, 0.30, 0.30, 0.36, 0.22, 0.45, 0.45, 0.45]
def peak_power(i):
    d.probe(f"param {i} 0 127\nparam {i} 4 64\nhit {i} 127\nrun 44100\nwrite /tmp/c.raw\n")
    x = d.read_stereo('/tmp/c.raw'); return float(np.sqrt(x[:, 0]**2 + x[:, 1]**2).max()) / 32768.0
if __name__ == '__main__':
    d.build(); lv = []
    for i in range(16):
        p = peak_power(i); v = int(round(127 * np.sqrt(TARGET[i] / p)))
        lv.append(min(127, v)); print(f"{NAMES[i]}  peak@127={p:6.3f}  target={TARGET[i]:.2f}  -> LEVEL {v}{'  (CLIPPED to 127)' if v > 127 else ''}")
    if '--apply' in sys.argv:
        path = '../src/drum_params.h'; s = open(path).read(); out = []; k = 0
        for line in s.split('\n'):
            m = re.match(r'(\s*\{\s*)(\d+)(,.*// [A-Z]{2}\s*)$', line)
            if m and k < 16: line = f"{m.group(1)}{lv[k]:3d}{m.group(3)}"; k += 1
            out.append(line)
        assert k == 16; open(path, 'w').write('\n'.join(out)); print("applied")
