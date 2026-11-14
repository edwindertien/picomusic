# Regenerates docs/bank.png, docs/dynamics.png, docs/architecture.png, docs/spectrogram.png and docs/demo_piano.wav from the REAL engine and bank.
import sys, os, json, wave; HERE = os.path.dirname(os.path.abspath(__file__)); sys.path.insert(0, os.path.join(HERE, '..', '..', 'tests', 'piano')); sys.path.insert(0, os.path.join(HERE, '..', 'host_test'))
import numpy as np, matplotlib; matplotlib.use('Agg'); import matplotlib.pyplot as plt
from matplotlib.patches import FancyBboxPatch
import dsp_common as d
D = os.path.join(HERE, '..', '..', 'docs', 'piano') if os.path.isdir(os.path.join(HERE, '..', '..', 'docs', 'piano')) else os.path.join(HERE, '..', 'docs'); FS = 44100; d.build(); BG, FG, GOLD = '#0b0b0d', '#eee', '#d4a937'
Z = json.load(open(os.path.join(HERE, '.gen', 'bank.json')))['zones']      # the grand's 30 zones (instrument 0)
def style(a): a.set_facecolor('#15151a'); a.tick_params(colors='#aaa', labelsize=8); [s.set_color('#443') for s in a.spines.values()]; a.grid(alpha=.15)
NEUTRAL = "set vol 127\nset tone 64\nset hard 64\nset width 64\nset tune 64\nset soft 0\nset decay 127\nset rel 64\nset wah 0\nset trem 64\nset chor 0\nset rev 0\n"
def render(script, n): d.probe(script + f"run {n}\nwrite /tmp/p.raw\n"); return np.fromfile('/tmp/p.raw', dtype=np.int16).astype(float).reshape(-1, 2) / 32768
# ---------------- the bank ----------------
fig, ax = plt.subplots(1, 3, figsize=(16, 4.6)); fig.patch.set_facecolor(BG)
root = [z['root'] for z in Z]; col = {16000: '#c0562f', 24000: '#d4a937', 32000: '#e8e2c8'}
a = ax[0]; style(a); a.bar(root, [z['bytes'] / 1000 for z in Z], width=2.4, color=[col[z['rate']] for z in Z])
for r, lab in ((16000, '16 kHz (A0..C2)'), (24000, '24 kHz (D#2..C5)'), (32000, '32 kHz (D#5..C8)')): a.bar([], [], color=col[r], label=lab)
a.set_xlabel('MIDI key of the zone', color='#aaa'); a.set_ylabel('KB of 4-bit ADPCM', color='#aaa'); a.legend(fontsize=8, facecolor='#15151a', labelcolor=FG); a.set_title(f"30 zones, {sum(z['bytes'] for z in Z)/1e6:.2f} MB: long bass, short bright treble", color=FG, fontsize=10)
a2 = a.twinx(); a2.plot(root, [z['dur'] for z in Z], 'o-', color='#7fd', ms=3, lw=1); a2.set_ylabel('seconds kept', color='#7fd'); a2.tick_params(colors='#7fd', labelsize=8)
a = ax[1]; style(a); a.plot(root, [z['cents'] for z in Z], 'o-', color=GOLD, ms=4); a.axhline(0, color='#888', lw=.8); a.set_xlabel('MIDI key', color='#aaa'); a.set_ylabel('cents re equal temperament (A = 440)', color='#aaa'); a.set_title("Measured tuning of the recording: a real piano's stretch,\nflat in the bass and sharp in the treble (kept, not 'corrected')", color=FG, fontsize=10)
a = ax[2]; style(a); a.plot(root, [z['snr'] for z in Z], 'o-', color='#c0562f', ms=4); a.axhline(24, color='#888', ls=':', lw=.8); a.set_xlabel('MIDI key', color='#aaa'); a.set_ylabel('dB', color='#aaa'); a.set_title('ADPCM signal-to-noise per zone (4 bits, adaptive step)', color=FG, fontsize=10)
plt.tight_layout(); plt.savefig(os.path.join(D, 'bank.png'), dpi=110, facecolor=BG); plt.close()
# ---------------- dynamics ----------------
fig, ax = plt.subplots(1, 2, figsize=(14, 4.4)); fig.patch.set_facecolor(BG)
a = ax[0]; style(a); cols = ['#7fd', '#9f9', GOLD, '#fff']
for vel, c in zip((20, 50, 80, 127), cols):
    x = render(NEUTRAL + f"on 60 {vel}\n", int(.3 * FS)).sum(1) / 2; sp = np.abs(np.fft.rfft(x * np.hanning(len(x)))) ** 2; f = np.fft.rfftfreq(len(x), 1 / FS)
    k = np.ones(15) / 15; sm = np.convolve(10 * np.log10(sp + 1e-20), k, 'same'); a.semilogx(f[5:], sm[5:], color=c, lw=1.1, label=f"velocity {vel}")
a.set_xlim(100, 20000); a.set_ylim(-20, 45); a.set_xlabel('Hz', color='#aaa'); a.set_ylabel('dB (first 300 ms of middle C)', color='#aaa'); a.legend(fontsize=8, facecolor='#15151a', labelcolor=FG); a.set_title('Softer notes are quieter AND darker; 80 and up play the recording unfiltered', color=FG, fontsize=10)
a = ax[1]; style(a)
for key, c in ((36, '#c0562f'), (60, GOLD), (96, '#e8e2c8')):
    x = render(NEUTRAL + f"on {key} 80\nrun 44100\noff {key}\n", int(1.6 * FS)).sum(1) / 2; b = 441; e = np.sqrt((x[:len(x) // b * b] ** 2).reshape(-1, b).mean(1)); a.plot(np.arange(len(e)) * b / FS, 20 * np.log10(e + 1e-6), color=c, label=f"key {key}")
a.axvline(1.0, color='#888', ls=':', lw=1); a.text(1.02, -10, 'key released', color='#aaa', fontsize=8); a.set_ylim(-80, 0); a.set_xlabel('s', color='#aaa'); a.set_ylabel('dB', color='#aaa'); a.legend(fontsize=8, facecolor='#15151a', labelcolor=FG); a.set_title('The damper: bass notes ring on longer than treble (REL = 64)', color=FG, fontsize=10)
plt.tight_layout(); plt.savefig(os.path.join(D, 'dynamics.png'), dpi=110, facecolor=BG); plt.close()
# ---------------- architecture ----------------
fig, a = plt.subplots(figsize=(14.4, 6.4)); fig.patch.set_facecolor(BG); a.set_facecolor(BG); a.set_xlim(0, 14.4); a.set_ylim(0, 6.4); a.axis('off')
def box(x, y, w, h, t, c, fs=9): a.add_patch(FancyBboxPatch((x, y), w, h, boxstyle='round,pad=0.02,rounding_size=0.08', fc=c, ec='#887', lw=1)); a.text(x + w / 2, y + h / 2, t, ha='center', va='center', color='w', fontsize=fs)
def arrow(x0, y0, x1, y1, t='', c='#9fb'): a.annotate('', xy=(x1, y1), xytext=(x0, y0), arrowprops=dict(arrowstyle='->', color=c, lw=1.4)); a.text((x0 + x1) / 2, (y0 + y1) / 2 + .12, t, color=c, fontsize=7.5, ha='center')
a.add_patch(FancyBboxPatch((.2, 3.2), 14.0, 3.0, boxstyle='round,pad=0.02,rounding_size=0.1', fc='none', ec='#6a7a8a', lw=1.2, ls='--')); a.text(.4, 5.95, 'CORE 0   loop(): MIDI, USB host, buttons, LCD, serial  -  all the pow() / exp() work', color='#9ab', fontsize=10, va='top')
a.add_patch(FancyBboxPatch((.2, .2), 14.0, 2.8, boxstyle='round,pad=0.02,rounding_size=0.1', fc='none', ec='#9a7a3a', lw=1.2, ls='--')); a.text(.4, 2.8, 'CORE 1   loop1(): SRAM, voices + FX, no libm', color='#cb9', fontsize=10, va='top')
box(.5, 4.1, 1.9, 1.1, 'AKAI MPK mini\nkeys, pedals, knobs\nprogram change', '#2a2a30'); box(3.0, 4.1, 2.1, 1.1, 'MIDI parsers\nUSB host / device / UART', '#33333a', 8.5)
box(5.7, 4.1, 4.4, 1.1, 'PianoManager (core-0 API)\ninstrument -> zone (velocity layer, key range, nearest root)\nplayback increment, velocity -> gain / filter, pan\neffect knobs -> coefficients', '#33333a', 7.8); box(10.7, 4.1, 3.2, 1.1, 'LCD: 2 knob pages + 88 keys\nA pedal, B una corda\nhold PRESS: next instrument', '#33333a', 8)
box(.5, 1.0, 2.3, 1.2, 'Event ring (64)\nnote on: zone, increment,\ngains, filter / off / pedal\n+ panic, all-off COUNTERS', '#443a2a', 7.8); box(3.3, 1.0, 2.5, 1.2, 'Voice allocator\n24 notes in 32 slots:\na steal FADES the victim\n(13 ms) in a spare slot', '#5a4a2a', 8)
box(6.3, 1.0, 3.4, 1.2, 'PCM head, then ADPCM + 4-tap LMS;\nloop jump (full state restored) ->\nHermite -> velocity LP -> envelope', '#5a4a2a', 8); box(10.1, 1.0, 2.3, 1.2, 'volume -> FX\nwah, trem / auto-pan,\nchorus, reverb', '#4a3a5a', 8); box(12.7, 1.0, .85, 1.2, 'knee\nlimiter', '#5a4a2a', 8); box(13.65, 1.0, .5, 1.2, 'I2S', '#5a3a2a', 8)
box(6.3, 2.35, 3.4, .5, 'ADPCM bank in flash (XIP): instruments, zones, loops', '#2a3a4a', 8)
arrow(2.4, 4.65, 3.0, 4.65); arrow(5.1, 4.65, 5.7, 4.65); arrow(10.1, 4.65, 10.7, 4.65); arrow(7.6, 4.1, 1.9, 2.2, 'events', '#ffb86b'); arrow(2.8, 1.6, 3.3, 1.6, '', '#ffb86b'); arrow(5.8, 1.6, 6.3, 1.6, '', '#ffb86b'); arrow(9.7, 1.6, 10.1, 1.6, '', '#ffb86b'); arrow(12.4, 1.6, 12.7, 1.6, '', '#ffb86b'); arrow(13.55, 1.6, 13.65, 1.6, '', '#ffb86b')
arrow(8.0, 3.2, 8.0, 2.85, '', '#8ab'); arrow(12.3, 4.1, 12.3, 2.2, 'effect coefficients', '#c9f')
plt.savefig(os.path.join(D, 'architecture.png'), dpi=110, facecolor=BG, bbox_inches='tight'); plt.close()
# ---------------- demo: the harmonic outline of the C-major arpeggio prelude (BWV 846, public domain) ----------------
BARS = [[48, 64, 67, 72, 76], [48, 62, 69, 74, 77], [47, 62, 67, 74, 77], [48, 64, 67, 72, 76], [48, 64, 69, 76, 81], [48, 62, 66, 69, 74], [47, 62, 67, 74, 79], [47, 60, 64, 67, 72]]
S16 = int(0.16 * FS); ev = []                                                    # (time in samples, command)
rng = np.random.RandomState(4); t = 0
for bi, bar in enumerate(BARS):
    ev.append((t, "pedal 0")); ev.append((t + 700, "pedal 1"))
    seq = (bar[0], bar[1], bar[2], bar[3], bar[4], bar[2], bar[3], bar[4]) * 2
    for i, n in enumerate(seq):
        vel = int(np.clip(62 + (10 if i % 8 == 0 else 4 if i % 4 == 0 else 0) + (bi % 4) * 2 + rng.randint(-3, 4), 30, 120)); on = t + i * S16 + rng.randint(0, 220)
        ev.append((on, f"on {n} {vel}")); ev.append((on + int(0.95 * FS), f"off {n}"))
    t += 16 * S16
final = [36, 48, 52, 55, 60]; ev.append((t, "pedal 0")); ev.append((t + 700, "pedal 1"))
for j, n in enumerate(final): ev.append((t + j * 3000, f"on {n} {64 + 6 * (j % 2)}")); ev.append((t + 5 * FS, f"off {n}"))
ev.append((t + 7 * FS, "pedal 0")); ev.sort(key=lambda e: e[0]); end = t + 11 * FS
script = NEUTRAL + "set vol 110\nset width 80\n"; now = 0
for tt, cmd in ev:
    if tt > now: script += f"run {tt - now}\n"; now = tt
    script += cmd + "\n"
script += f"run {end - now}\nwrite /tmp/pdemo.raw\n"
d.probe(script); x = np.fromfile('/tmp/pdemo.raw', dtype=np.int16)
w = wave.open(os.path.join(D, 'demo_piano.wav'), 'wb'); w.setnchannels(2); w.setsampwidth(2); w.setframerate(FS); w.writeframes(x.tobytes()); w.close()
s = x.astype(float).reshape(-1, 2) / 32768; print(f"demo: {len(s)/FS:.1f} s stereo, peak {20*np.log10(np.abs(s).max()):.1f} dBFS, clipped samples {int((np.abs(x) >= 32767).sum())}, rms {20*np.log10(np.sqrt((s**2).mean())):.1f} dBFS")
fig, a = plt.subplots(figsize=(13, 3.8)); fig.patch.set_facecolor(BG); style(a); a.specgram(s.sum(1) / 2, NFFT=4096, Fs=FS, noverlap=3072, cmap='magma', vmin=-120, vmax=-30); a.set_ylim(0, 6000)
a.set_xlabel('s', color='#aaa'); a.set_ylabel('Hz', color='#aaa'); a.set_title('demo_piano.wav: eight bars of arpeggios under the sustain pedal, then a held chord', color=FG, fontsize=10); plt.tight_layout(); plt.savefig(os.path.join(D, 'spectrogram.png'), dpi=105, facecolor=BG); plt.close()
