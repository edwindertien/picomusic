"""Suggests per-patch VOL values so every patch has the same RMS for a 6-note chord (run after changing patches or voicing)."""
import sys, re, os, numpy as np
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from dsp_common import *
build()
chord = ''.join(f"on {n}\n" for n in (48, 55, 60, 64, 67, 72))
def rms(patch):
    s = f"patch {patch}\n{chord}skip {500 if patch == 3 else 110000}\nrun {22050 if patch == 3 else 44100}\nwrite /tmp/l.raw\n"   # patch 3 = Pizz: measure right after the strike
    probe(s); d = read_stereo('/tmp/l.raw'); return float(np.sqrt(np.mean(d ** 2)))
par = open(os.path.join(HERE, '..', 'src', 'strings_params.h')).read()
vols = [int(re.findall(r'\d+', r.split('//')[0])[-1]) for r in re.findall(r'\{\s+\d+,[^{}]*\}', re.search(r'PATCHES\[N_PATCHES\]\[N_PARAMS\] = \{(.*?)\n\};', par, re.S).group(1))]
r = [rms(p) for p in range(len(vols))]; print("current VOL:", vols, " rms:", [round(x) for x in r])
print("suggested VOL:", [min(127, round(v * np.sqrt(r[0] / x))) for v, x in zip(vols, r)])
