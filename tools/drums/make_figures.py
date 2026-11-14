# Regenerates docs/instruments.png, docs/architecture.png, docs/timing.png and docs/demo_drums.wav from the REAL engine.
import sys, os, wave; HERE = os.path.dirname(os.path.abspath(__file__)); sys.path.insert(0, os.path.join(HERE, '..', 'host_test'))
import numpy as np, matplotlib; matplotlib.use('Agg'); import matplotlib.pyplot as plt
from matplotlib.patches import FancyBboxPatch
import dsp_common as d
from drum_names import NAMES
D = os.path.join(HERE, '..', 'docs'); FS = 44100
d.build()
FULL = ["Bass Drum","Snare","Low Tom","Mid Tom","High Tom","Rim Shot","Hand Clap","Cowbell","Closed Hat","Open Hat","Cymbal","Claves","Maracas","Low Conga","Mid Conga","High Conga"]
# ---------------- instruments ----------------
WIN = [.7,.5,.6,.6,.5,.06,.25,.5,.2,.8,1.6,.08,.15,.4,.35,.3]
fig, ax = plt.subplots(4, 4, figsize=(15, 8.6)); fig.patch.set_facecolor('#101418')
cols = ['#ff5a4d','#ff9a3d','#ffe14d','#e8e8e8']
for i in range(16):
    d.probe(f"hit {i} 127\nrun {int(WIN[i]*FS)}\nwrite /tmp/g.raw\n"); x = d.read_stereo('/tmp/g.raw'); m = (x[:,0]+x[:,1])/2/32768
    a = ax[i//4][i%4]; a.set_facecolor('#181e24'); t = np.arange(len(m))/FS*1000
    a.plot(t, m, lw=.5, color=cols[(i%4)]); a.set_title(f"{NAMES[i]}  {FULL[i]}", color='w', fontsize=10, loc='left')
    a.set_ylim(-1, 1); a.tick_params(colors='#9aa', labelsize=7); [s.set_color('#345') for s in a.spines.values()]
    e = np.abs(m); pk = e.max(); idx = np.where(e > pk/100)[0]; a.text(.98, .06, f"peak {pk:.2f}   -40 dB at {t[idx[-1]]:.0f} ms", transform=a.transAxes, ha='right', color='#9fb', fontsize=7.5)
    if i >= 12: a.set_xlabel('ms', color='#9aa', fontsize=8)
fig.suptitle('The 16 instruments at the default kit, velocity 127 (rendered by the real engine)', color='w', fontsize=13); plt.tight_layout(rect=[0,0,1,.96]); plt.savefig(os.path.join(D, 'instruments.png'), dpi=110, facecolor=fig.get_facecolor()); plt.close()
# ---------------- timing (swing) from the trace ----------------
def steps(sw):
    out = d.probe("clearall\n" + "".join(f"step 0 {s} 2\n" for s in range(16)) + f"set swing {sw}\nplay\nskip {int(8*5512.5)}\ntrace\n"); return np.array([int(l.split()[1]) for l in out.split('\n') if l.startswith('T ')])/FS*1000
fig, ax = plt.subplots(3, 1, figsize=(11, 4.6), sharex=True); fig.patch.set_facecolor('#101418')
for a, (sw, lab) in zip(ax, ((0, 'swing 50 % (straight)'), (43, 'swing 59 %'), (127, 'swing 75 % (maximum)'))):
    t = steps(sw); a.set_facecolor('#181e24'); a.vlines(t[:8], 0, 1, color=[cols[(i//4)%4] if False else ('#ff9a3d' if i%2 else '#ff5a4d') for i in range(len(t[:8]))], lw=3); a.set_yticks([]); a.set_ylabel(lab, color='w', rotation=0, ha='right', va='center', fontsize=9)
    for k in range(0, 8, 2): a.axvline(k*125, color='#345', lw=.8, ls=':')
    a.tick_params(colors='#9aa'); [s.set_color('#345') for s in a.spines.values()]
ax[-1].set_xlabel('ms at 120 BPM: red = even steps (stay on the grid), orange = odd steps (delayed); every pair still lasts exactly 250 ms', color='#9aa', fontsize=9)
plt.tight_layout(); plt.savefig(os.path.join(D, 'timing.png'), dpi=110, facecolor=fig.get_facecolor()); plt.close()
# ---------------- architecture ----------------
fig, a = plt.subplots(figsize=(13, 6.3)); fig.patch.set_facecolor('#101418'); a.set_facecolor('#101418'); a.set_xlim(0, 13); a.set_ylim(0, 6.3); a.axis('off')
def box(x, y, w, h, t, c, fs=9): a.add_patch(FancyBboxPatch((x, y), w, h, boxstyle='round,pad=0.02,rounding_size=0.08', fc=c, ec='#6a7a8a', lw=1)); a.text(x+w/2, y+h/2, t, ha='center', va='center', color='w', fontsize=fs)
def arrow(x0, y0, x1, y1, t='', c='#9fb'): a.annotate('', xy=(x1, y1), xytext=(x0, y0), arrowprops=dict(arrowstyle='->', color=c, lw=1.4)); a.text((x0+x1)/2, (y0+y1)/2+.12, t, color=c, fontsize=7.5, ha='center')
a.add_patch(FancyBboxPatch((.2, 3.0), 12.6, 3.0, boxstyle='round,pad=0.02,rounding_size=0.1', fc='none', ec='#4a6a8a', lw=1.2, ls='--')); a.text(.4, 5.75, 'CORE 0   loop(): MIDI, USB host, buttons, LCD, serial', color='#8ab', fontsize=10, va='top')
a.add_patch(FancyBboxPatch((.2, .2), 12.6, 2.6, boxstyle='round,pad=0.02,rounding_size=0.1', fc='none', ec='#8a6a4a', lw=1.2, ls='--')); a.text(.4, 2.6, 'CORE 1   loop1(): runs from SRAM, one sample at a time', color='#ba8', fontsize=10, va='top')
box(.5, 4.0, 1.9, 1.0, 'AKAI MPK mini\npads + knobs', '#2a3a4a'); box(.5, 3.2, 1.9, .6, 'MIDI clock / DAW', '#2a3a4a', 8)
box(3.0, 4.0, 2.0, 1.0, 'MIDI parsers\nUSB host / device / UART\nreal-time bytes first', '#33445a', 8); box(5.6, 4.0, 1.9, 1.0, 'DrumManager\n(core-0 API)\npad map, params, UI', '#33445a', 8.5); box(8.3, 4.0, 1.7, 1.0, 'Pattern memory\n16 x 16 x 16 steps\nkit, song', '#3a3a5a', 8); box(10.6, 4.0, 2.0, 1.0, 'LCD (4 pages)\nflash save / load\n(core 0 only, stopped)', '#33445a', 8)
box(.7, 1.1, 2.4, 1.1, 'Mailboxes (single writer)\ntrigSeq+trigVel / ctlSeq\nreqPatSeq / clkSeq / panicSeq', '#4a3a2a', 8); box(3.7, 1.1, 2.5, 1.1, 'SEQUENCER\ncounts samples\nswing, queue, song, record', '#5a4630', 9); box(6.8, 1.1, 2.4, 1.1, '16 voices\nsine / noise / metal\nexp. envelopes', '#5a4630', 9); box(9.8, 1.1, 1.4, 1.1, 'mix + pan\nsoftSat', '#5a4630', 9); box(11.5, 1.1, 1.2, 1.1, 'PIO I2S\nDAC', '#5a3a2a', 9)
arrow(2.4, 4.5, 3.0, 4.5); arrow(2.4, 3.5, 3.0, 4.2); arrow(5.0, 4.5, 5.6, 4.5); arrow(7.5, 4.5, 8.3, 4.5); arrow(10.0, 4.5, 10.6, 4.5)
arrow(6.5, 4.0, 2.2, 2.2, 'hit / transport / pattern / clock', '#ffb86b'); arrow(3.1, 1.65, 3.7, 1.65, '', '#ffb86b'); arrow(6.2, 1.65, 6.8, 1.65, 'step -> voice start', '#ffb86b'); arrow(9.2, 1.65, 9.8, 1.65, '', '#ffb86b'); arrow(11.2, 1.65, 11.5, 1.65, '', '#ffb86b')
arrow(9.2, 3.0, 5.0, 2.2, 'reads steps (bytes, no lock)', '#8ab'); arrow(4.9, 2.25, 8.6, 3.95, 'recorded hits: ring buffer', '#9fb')
plt.savefig(os.path.join(D, 'architecture.png'), dpi=110, facecolor=fig.get_facecolor(), bbox_inches='tight'); plt.close()
# ---------------- demo ----------------
BAR = 16 * int(FS * 15 / 104)
d.probe(f"songmode 1\nbpm 104\nset swing 35\nplay\nrun {8*BAR}\nset drive 70\nrun {8*BAR}\nstop\nrun {2*FS}\nwrite /tmp/demo.raw\n")
x = np.fromfile('/tmp/demo.raw', dtype=np.int16)
w = wave.open(os.path.join(D, 'demo_drums.wav'), 'wb'); w.setnchannels(2); w.setsampwidth(2); w.setframerate(FS); w.writeframes(x.tobytes()); w.close()
s = x.reshape(-1, 2).astype(float) / 32768; print(f"demo: {len(s)/FS:.1f} s, peak {20*np.log10(np.abs(s).max()):.1f} dBFS, clipped samples {int((np.abs(x) >= 32767).sum())}, rms {20*np.log10(np.sqrt((s**2).mean())):.1f} dBFS")
