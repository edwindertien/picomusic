# Regenerates docs/ladder.png, docs/architecture.png, docs/demo_bass.wav and docs/spectrogram.png from the REAL engine.
import sys, os, wave, subprocess; HERE = os.path.dirname(os.path.abspath(__file__)); sys.path.insert(0, os.path.join(HERE, '..', 'host_test'))
import numpy as np, matplotlib; matplotlib.use('Agg'); import matplotlib.pyplot as plt
from matplotlib.patches import FancyBboxPatch
import dsp_common as d
D = os.path.join(HERE, '..', 'docs'); FS = 44100; d.build()
BG, FG = '#101418', '#dde'
def style(a): a.set_facecolor('#181e24'); a.tick_params(colors='#9aa', labelsize=8); [s.set_color('#345') for s in a.spines.values()]; a.grid(alpha=.15)
# ---------------- ladder ----------------
fig, ax = plt.subplots(1, 3, figsize=(16, 4.6)); fig.patch.set_facecolor(BG)
a = ax[0]; style(a); fc = 500
for r, col in ((0.0, '#7fd'), (0.5, '#9f9'), (0.9, '#fd6'), (1.0, '#f96')):
    y = np.array(d.probe(f"filtsweep {fc} {r} 40 5000 160\n").split(), float); a.semilogx(np.geomspace(40, 5000, 160), y, color=col, label=f"feedback {r:.1f} x critical")
a.axvline(fc, color='#f55', ls=':', lw=1); a.text(fc * 1.05, -52, 'cutoff 500 Hz', color='#f88', fontsize=8); a.set_ylim(-60, 30); a.set_xlabel('Hz', color='#9aa'); a.set_ylabel('dB', color='#9aa'); a.legend(fontsize=7, facecolor='#181e24', labelcolor=FG); a.set_title('Ladder response at a 500 Hz cutoff: the resonance peak sits AT the cutoff,\nand the passband (bass) falls as resonance rises', color=FG, fontsize=9)
a = ax[1]; style(a)
FSr = FS
def osc_ratio(g):                                                              # the picosynth mapping g = tan(pi f/fs): where does the loop self-oscillate?
    ws = np.linspace(1e-4, np.pi * .999, 100000); z = np.exp(1j * ws); L = (g / (z - 1 + g)) ** 4; ph = np.unwrap(np.angle(L)); return ws[np.argmax(ph < -np.pi)] * FSr / 2 / np.pi
fcs = np.geomspace(55, 5000, 60); old = [osc_ratio(np.tan(np.pi * f / FSr)) / f for f in fcs]
a.semilogx(fcs, old, color='#f96', label='picosynth mapping  g = tan(pi f / fs)')
newcut = [20 * 2 ** (c * 8.1 / 127) for c in range(30, 118, 4)]
err = []
for c, t in zip(range(30, 118, 4), newcut):
    d.probe("")                                                                 # (keep the probe build warm)
    BASE = "".join(f"set {k} {v}\n" for k, v in dict(mix1=0, mix2=0, mix3=0, sub=0, noise=14, wav1=10, oct1=40, res=127, drive=0, key=0, env=0, vel=0, acc=0, glide=0, vol=110, fa=0, fd=127, fs=127, fr=40, aa=0, ad=127, **{'as': 127}, ar=40).items())
    d.probe(BASE + f"set cut {c}\non 36 100\nrun {3*FS}\nwrite /tmp/s.raw\n"); x = np.fromfile('/tmp/s.raw', dtype=np.int16).astype(float)[FS:] / 32768
    n = 1 << 19; sp = np.log(np.abs(np.fft.rfft(x * np.hanning(len(x)), n)) + 1e-12); f = np.fft.rfftfreq(n, 1 / FS); m = np.where((f > t * .6) & (f < t * 1.4))[0]; i = m[np.argmax(sp[m])]
    aa, bb, cc = sp[i - 1], sp[i], sp[i + 1]; err.append((i + 0.5 * (aa - cc) / (aa - 2 * bb + cc)) * FS / n / t)
a.semilogx(newcut, err, 'o-', color='#7fd', ms=4, label='this synth (measured, RES 127, with pitch compensation)'); a.axhline(1.0, color='#fff', ls=':', lw=1)
a.set_ylim(.3, 1.1); a.set_xlabel('nominal cutoff, Hz', color='#9aa'); a.set_ylabel('self-oscillation frequency / nominal', color='#9aa'); a.legend(fontsize=7, facecolor='#181e24', labelcolor=FG, loc='lower left'); a.set_title('Does the resonance self-oscillate AT the cutoff?', color=FG, fontsize=9)
a = ax[2]; style(a); cents = [1200 * np.log2(e) for e in err]; a.semilogx(newcut, cents, 'o-', color='#7fd', ms=4); a.axhspan(-10, 10, color='#3a6', alpha=.2); a.set_xlabel('cutoff, Hz', color='#9aa'); a.set_ylabel('cents', color='#9aa'); a.set_title('Self-oscillation pitch error (green: +-10 cents)', color=FG, fontsize=9)
plt.tight_layout(); plt.savefig(os.path.join(D, 'ladder.png'), dpi=110, facecolor=BG); plt.close()
# ---------------- architecture ----------------
fig, a = plt.subplots(figsize=(13, 6.3)); fig.patch.set_facecolor(BG); a.set_facecolor(BG); a.set_xlim(0, 13); a.set_ylim(0, 6.3); a.axis('off')
def box(x, y, w, h, t, c, fs=9): a.add_patch(FancyBboxPatch((x, y), w, h, boxstyle='round,pad=0.02,rounding_size=0.08', fc=c, ec='#6a7a8a', lw=1)); a.text(x + w / 2, y + h / 2, t, ha='center', va='center', color='w', fontsize=fs)
def arrow(x0, y0, x1, y1, t='', c='#9fb'): a.annotate('', xy=(x1, y1), xytext=(x0, y0), arrowprops=dict(arrowstyle='->', color=c, lw=1.4)); a.text((x0 + x1) / 2, (y0 + y1) / 2 + .12, t, color=c, fontsize=7.5, ha='center')
a.add_patch(FancyBboxPatch((.2, 3.0), 12.6, 3.0, boxstyle='round,pad=0.02,rounding_size=0.1', fc='none', ec='#4a6a8a', lw=1.2, ls='--')); a.text(.4, 5.75, 'CORE 0   loop(): MIDI, USB host, buttons, LCD, serial', color='#8ab', fontsize=10, va='top')
a.add_patch(FancyBboxPatch((.2, .2), 12.6, 2.6, boxstyle='round,pad=0.02,rounding_size=0.1', fc='none', ec='#8a6a4a', lw=1.2, ls='--')); a.text(.4, 2.6, 'CORE 1   loop1(): runs from SRAM, one sample at a time', color='#ba8', fontsize=10, va='top')
box(.5, 4.0, 1.9, 1.0, 'AKAI MPK mini\nkeys + knobs', '#2a3a4a'); box(.5, 3.2, 1.9, .6, 'MIDI clock / DAW', '#2a3a4a', 8)
box(3.0, 4.0, 2.0, 1.0, 'MIDI parsers\nUSB host / device / UART\nreal-time bytes first', '#33445a', 8); box(5.6, 4.0, 2.2, 1.0, 'Key stack (mono)\nlast-note priority\nlegato return, pedal', '#33445a', 8.5); box(8.4, 4.0, 1.9, 1.0, 'Patches + 16 patterns\n16 steps: note, slide,\naccent', '#3a3a5a', 8); box(10.8, 4.0, 1.8, 1.0, 'LCD (4 pages)\nflash save / load', '#33445a', 8)
box(.7, 1.1, 2.4, 1.1, 'Mailboxes (single writer)\nevtWord = seq|vel|note|flags\nctl / pattern / clock / panic', '#4a3a2a', 8); box(3.7, 1.1, 2.4, 1.1, 'SEQUENCER\ncounts samples, swing\ngate length, slide, queue', '#5a4630', 9); box(6.7, 1.1, 3.3, 1.1, 'VOICE   3 osc (PolyBLEP) + sub + noise\n-> 4-pole LADDER (exact coefficients)\n-> amp ADSR, glide, accent', '#5a4630', 8.5); box(10.4, 1.1, 1.1, 1.1, 'softSat\nint16', '#5a4630', 8.5); box(11.7, 1.1, 1.0, 1.1, 'PIO I2S\nDAC', '#5a3a2a', 9)
arrow(2.4, 4.5, 3.0, 4.5); arrow(2.4, 3.5, 3.0, 4.2); arrow(5.0, 4.5, 5.6, 4.5); arrow(7.8, 4.5, 8.4, 4.5); arrow(10.3, 4.5, 10.8, 4.5)
arrow(6.7, 4.0, 2.2, 2.2, 'gate / note / legato, transport, pattern, clock', '#ffb86b'); arrow(3.1, 1.65, 3.7, 1.65, '', '#ffb86b'); arrow(6.1, 1.65, 6.7, 1.65, 'note on / slide / gate off', '#ffb86b'); arrow(10.0, 1.65, 10.4, 1.65, '', '#ffb86b'); arrow(11.5, 1.65, 11.7, 1.65, '', '#ffb86b')
arrow(9.3, 3.0, 5.2, 2.2, 'reads steps (bytes, no lock)', '#8ab')
plt.savefig(os.path.join(D, 'architecture.png'), dpi=110, facecolor=BG, bbox_inches='tight'); plt.close()
# ---------------- demo ----------------
BAR = 16 * int(FS * 15 / 108)
secs = [(0, 0, 0, "Moog Bass: Groove"), (1, 2, 0, "Acid: the cutoff sweeps"), (2, 1, 0, "Sub pulse"), (3, 3, 30, "Pluck: Octaves, swung")]
script = "bpm 108\n"
for pat, patch, sw, name in secs:
    script += f"stop\npatch {patch}\npat {pat}\nset swing {sw}\nplay\n"
    for bar in range(4):
        if patch == 2:                                                            # the acid sweep: open the cutoff a little every half bar
            for h in range(2): script += f"set cut {24 + (bar * 2 + h) * 4}\nrun {BAR // 2}\n"
        else: script += f"run {BAR}\n"
script += f"stop\nrun {FS}\nwrite /tmp/bdemo.raw\n"
d.probe(script); x = np.fromfile('/tmp/bdemo.raw', dtype=np.int16)
w = wave.open(os.path.join(D, 'demo_bass.wav'), 'wb'); w.setnchannels(1); w.setsampwidth(2); w.setframerate(FS); w.writeframes(x.tobytes()); w.close()
s = x.astype(float) / 32768; print(f"demo: {len(s)/FS:.1f} s mono, peak {20*np.log10(np.abs(s).max()):.1f} dBFS, clipped samples {int((np.abs(x) >= 32767).sum())}, rms {20*np.log10(np.sqrt((s**2).mean())):.1f} dBFS")
fig, a = plt.subplots(figsize=(13, 3.8)); fig.patch.set_facecolor(BG); style(a); a.specgram(s, NFFT=2048, Fs=FS, noverlap=1536, cmap='magma', vmin=-110, vmax=-20); a.set_ylim(0, 4000)
t0 = 0
for pat, patch, sw, name in secs: a.axvline(t0, color='w', lw=.6, alpha=.5); a.text(t0 + .2, 3750, name, color='w', fontsize=8, va='top'); t0 += 4 * BAR / FS
a.set_xlabel('s', color='#9aa'); a.set_ylabel('Hz', color='#9aa'); a.set_title('demo_bass.wav: the filter envelope opens on every note (and the acid section sweeps the cutoff up)', color=FG, fontsize=10); plt.tight_layout(); plt.savefig(os.path.join(D, 'spectrogram.png'), dpi=105, facecolor=BG); plt.close()
