"""Prints TRIM_* values that equalize the loudness of the five registrations at middle C (run after changing a filter)."""
import sys, os, numpy as np
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from dsp_common import *
build()
base = "patch 0\nset bass 0\nset cello 0\nset viola 0\nset violin 0\nset pizz 0\nset emix 0\nset tone 127\nset atk 0\nset det 0\nset vib 0\nset vol 127\n"
regs = ['bass', 'cello', 'viola', 'violin']
print("RMS (int16, mix 0, tone open, volume 127) at register = 127, per note:")
res = {}
for r in regs + ['pizz']:
    row = []
    for n in (36, 48, 60, 72, 84):
        if r == 'pizz': setup = base + f"set pizz 127\nset pdec 127\n"; v = sustained_rms(setup, n, skip=2000, run=8000)     # first 50-230 ms: still ringing
        else: setup = base + f"set {r} 127\n"; v = sustained_rms(setup, n)
        row.append(v)
    res[r] = row; print(f"  {r:7}", ' '.join(f'{v:8.0f}' for v in row))
tgt = res['cello'][2]
print("\ntarget = cello @ middle C =", round(tgt))
for r in regs + ['pizz']: print(f"  TRIM_{r.upper():7} = {tgt / res[r][2]:.3f}f")
