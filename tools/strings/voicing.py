"""Draws docs/voicing.png from the REAL engine (via host_test/probe): registration filter responses, ensemble tap delays, and
and middle C through each registration.  Run from the repo root."""
import sys, os
sys.path.insert(0, 'host_test')
import numpy as np, matplotlib; matplotlib.use('Agg'); import matplotlib.pyplot as plt
from dsp_common import build, probe, read_stereo, FS
build()
BG, BOX, TX, LN = '#10131a', '#1b2030', '#eef1f7', '#9aa3b5'
fig, axs = plt.subplots(1, 3, figsize=(16, 4.6), dpi=150); fig.patch.set_facecolor(BG)
for ax in axs:
    ax.set_facecolor(BOX); ax.tick_params(colors=LN, labelsize=8); [s.set_color('#3a4258') for s in ax.spines.values()]; ax.grid(color='#2a3350', lw=0.5)
    ax.title.set_color(TX); ax.xaxis.label.set_color(LN); ax.yaxis.label.set_color(LN)
# (a) registration filters
fr = np.geomspace(40, 12000, 70); names = [('bass', "BASS 16'", '#2ecc40'), ('cello', "CELLO 8'", '#19d3da'), ('viola', "VIOLA 8'", '#ff8c00'), ('violin', "VIOLIN 4'", '#e83ee8'), ('pizz', 'PIZZ', '#f2d600')]
out = probe(''.join(f"resp {n} {f:.2f}\n" for n, _, _ in names for f in fr)).split('\n'); vals = {}
for l in out:
    if l.startswith('RESP'): _, n, f, g = l.split(); vals.setdefault(n, []).append(float(g))
for n, lab, c in names: axs[0].semilogx(fr, 20 * np.log10(np.maximum(vals[n], 1e-4)), color=c, lw=1.8, label=lab)
axs[0].set_ylim(-60, 6); axs[0].set_xlim(40, 12000); axs[0].set_title('registration filters (fixed)'); axs[0].set_xlabel('Hz'); axs[0].set_ylabel('dB'); axs[0].legend(facecolor=BOX, edgecolor='#3a4258', labelcolor=TX, fontsize=8, loc='lower left')
# (b) ensemble tap delays (4 s, moderate shimmer so the slow sweep and the 120-degree spacing are visible)
n = FS * 4; probe(f"ensprobe 1 0.35 1 60 {n} /tmp/ens.f32\n"); y = np.fromfile('/tmp/ens.f32', dtype=np.float32).reshape(-1, 2).astype(float)
d = [[], [], []]; ts = []
for k in range(0, n // 1000 - 2):
    seg = y[k * 1000 + 60: k * 1000 + 420]; pl = np.argsort(seg[:, 0])[::-1]; a = pl[0]; b = [i for i in pl if abs(i - a) > 6][0]; r = np.argmax(seg[:, 1])
    d[0].append(a + 60); d[1].append(b + 60); d[2].append(r + 60); ts.append(k * 1000 / FS)
D = np.array(d, dtype=float); cross = (abs(D[0] - D[1]) < 10) | (abs(D[0] - D[2]) < 10) | (abs(D[1] - D[2]) < 10); D[:, cross] = np.nan   # the peak picker cannot tell crossing taps apart
for i, c in enumerate(('#19d3da', '#ff8c00', '#e83ee8')): axs[1].plot(ts, D[i] / FS * 1000, color=c, lw=1.6, label=f'tap {i}')
axs[1].set_title('ensemble: three delay taps, 120 degrees apart'); axs[1].set_xlabel('s'); axs[1].set_ylabel('delay, ms'); axs[1].legend(facecolor=BOX, edgecolor='#3a4258', labelcolor=TX, fontsize=8, loc='upper right', ncol=3)
# (c) middle C through each registration: smoothed spectrum
from numpy.fft import rfft, rfftfreq
BASE = "patch 0\nset bass 0\nset cello 0\nset viola 0\nset violin 0\nset pizz 0\nset emix 0\nset tone 127\nset atk 0\nset det 0\nset vib 0\nset vol 127\n"
def spec(reg):
    extra = f"set {reg} 127\n" + ("set pdec 127\n" if reg == 'pizz' else "")
    probe(BASE + extra + f"on 60\nskip {2000 if reg == 'pizz' else 30000}\nrun {FS * (1 if reg == 'pizz' else 2)}\nwrite /tmp/sp.raw\n"); x = read_stereo('/tmp/sp.raw')[:, 0]
    seg = 1 << 14; acc = 0; cnt = 0
    for i in range(0, len(x) - seg, seg // 2): acc = acc + np.abs(rfft(x[i:i + seg] * np.hanning(seg))) ** 2; cnt += 1
    f = rfftfreq(seg, 1 / FS); P = acc / cnt; fc = np.geomspace(100, 12000, 120)
    return fc, 10 * np.log10([P[(f > c / 2 ** (1 / 2)) & (f < c * 2 ** (1 / 2))].mean() + 1e-9 for c in fc])
ref = None
for reg, lab, c in (('bass', "BASS 16'", '#2ecc40'), ('cello', "CELLO 8'", '#19d3da'), ('viola', "VIOLA 8'", '#ff8c00'), ('violin', "VIOLIN 4'", '#e83ee8'), ('pizz', 'PIZZ', '#f2d600')):
    fc, sp = spec(reg); ref = sp.max() if ref is None else ref; axs[2].semilogx(fc, sp - ref, color=c, lw=1.6, label=lab)
axs[2].set_xlim(100, 12000); axs[2].set_ylim(-60, 25); axs[2].set_title('middle C through each registration (1-octave smoothed spectrum)'); axs[2].set_xlabel('Hz'); axs[2].set_ylabel('dB (relative)'); axs[2].legend(facecolor=BOX, edgecolor='#3a4258', labelcolor=TX, fontsize=8, loc='lower left')
fig.tight_layout(); fig.savefig('docs/voicing.png', facecolor=BG, bbox_inches='tight', pad_inches=0.15); print('docs/voicing.png written')
