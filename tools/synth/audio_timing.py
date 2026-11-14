"""Draws docs/audio_timing.png: what each PIO audio program puts on the wire, and what a PCM5101A-style receiver
(samples DIN on every rising BCK edge, TI SLAS859C) reads from it.  Uses the real src/ headers via host_test/pio_emu.py.
Run from the repo root."""
import sys, os
sys.path.insert(0, 'host_test')
import numpy as np, matplotlib; matplotlib.use('Agg')
import matplotlib.pyplot as plt
from pio_emu import parse_header, run, word_from_bits

BG, BOX, TX, LN, GRN, RED, CY = '#10131a', '#1b2030', '#eef1f7', '#9aa3b5', '#2ecc40', '#e8463a', '#19d3da'
L, R = 0xA5C3, 0x3C5A
# the words audio_driver_put() really builds:  Rev 2.1 = left<<16 | right ;  I2S board = right<<16 | left (LRCK=0 = left = low half)
FRAMES = {'rev': [((L << 16) | R)] * 10, 'i2s': [((R << 16) | L)] * 10}
def bits(v): return format(v, '016b')

def prepare(header, pc0_name, slot_len, nbck=40):
    words, wt, wr, entry = parse_header(header)
    tr, _ = run(words, wt, wr, entry, FRAMES[pc0_name])
    lr = tr[:, 1].astype(int)
    falls = np.where((lr[1:] == 0) & (lr[:-1] == 1))[0] + 1       # LRCK 1 -> 0 : start of the LRCK=0 slot
    e = falls[3]                                                  # steady state
    a = e - 3; b = a + 2 * nbck
    seg = tr[a:b]
    rise = np.where((tr[:, 0][1:].astype(int) == 1) & (tr[:, 0][:-1].astype(int) == 0))[0] + 1
    after = rise[rise >= e]                                       # first = the I2S delay bit (previous slot's LSB)
    slot = [int(tr[i, 2]) for i in after[1:1 + slot_len]]         # the bits a standard receiver clocks in for this slot
    return seg, a, after[1:1 + slot_len], slot

def panel(ax, seg, a, edges, slot, title, sent_label, sent_bits, verdict, vcol, per_bit):
    t = np.arange(len(seg)) / 2.0                                  # x axis in BCK periods
    for k, (name, col, y0) in enumerate((('LRCK', LN, 4.6), ('BCK', LN, 3.1), ('DIN', CY, 1.6))):
        v = seg[:, {'BCK': 0, 'LRCK': 1, 'DIN': 2}[name]].astype(float)
        ax.step(t, v + y0, where='post', color=col, lw=1.8)
        ax.text(-1.2, y0 + 0.5, name, ha='right', va='center', color=col, fontsize=9, weight='bold')
    for ei in edges:
        x = (ei - a) / 2.0
        if 0 <= x < t[-1]:
            ax.axvline(x, color='#3a4258', lw=0.6, ls=':')
            ax.plot([x], [1.6 + seg[int(ei - a), 2]], 'o', color=GRN, ms=4.5, zorder=5)
    ax.set_xlim(-0.5, t[-1]); ax.set_ylim(0.4, 6.4); ax.axis('off')
    ax.text(0, 6.15, title, color=TX, fontsize=11, weight='bold', va='center')
    ax.text(t[-1], 6.15, 'green dots = where a PCM5101A samples DIN (every rising BCK edge)', color=GRN, fontsize=7.8, ha='right', va='center')
    y = 0.95
    ax.text(0, y, f'sent  ({sent_label}):', color=LN, fontsize=8.5, va='center', family='monospace')
    ax.text(21.5, y, ' '.join(sent_bits[i:i+4] for i in range(0, 16, 4)), color=TX, fontsize=8.5, va='center', family='monospace')
    s = ''.join(map(str, slot)); s = ' '.join(s[i:i+4] for i in range(0, len(s), 4))
    ax.text(0, y - 0.5, 'received (slot):', color=LN, fontsize=8.5, va='center', family='monospace')
    ax.text(21.5, y - 0.5, s, color=vcol, fontsize=8.5, va='center', family='monospace')
    ax.text(0, y - 1.0, verdict, color=vcol, fontsize=8.8, va='center', weight='bold')

SRC = 'src'
seg1, a1, e1, slot1 = prepare(os.path.join(SRC, 'audio_pio.pio.h'), 'rev', 32)
seg0, a0, e0, slot0 = prepare(os.path.join(SRC, 'audio_pio_i2s.pio.h'), 'i2s', 16)
fig, axs = plt.subplots(2, 1, figsize=(13, 8.2), dpi=150); fig.patch.set_facecolor(BG)
for ax in axs: ax.set_facecolor(BG)
# Rev2.1 program: its LRCK=0 slot carries the RIGHT half-word (3C5A); the receiver reads 32 bits per 16-bit sample
val32 = word_from_bits(slot1, 32) & 0xffffffff
panel(axs[0], seg1, a1, e1, slot1, 'audio_pio.pio  (Rev 2.1 / CS4344 program):  BCK = 64 × fs, every data bit held for TWO BCK periods',
      f'one LRCK=0 half-frame of the word, 0x{R:04X}', bits(R),
      f'-> every bit is read twice (shifted by one edge): the DAC sees 0x{val32:08X} instead of 0x{R:04X}  =  severe distortion on a PCM5101A',
      RED, 2)
panel(axs[1], seg0, a0, e0, slot0, 'audio_pio_i2s.pio  (Initial Version / PCM5101A program):  standard I²S, BCK = 32 × fs, ONE data bit per BCK',
      f'left sample, 0x{L:04X}', bits(L),
      f'-> received 0x{word_from_bits(slot0, 16) & 0xffff:04X}: bit-exact', GRN, 1)
fig.text(0.5, 0.012, 'Generated from the real PIO programs by tools/audio_timing.py (emulator in host_test/pio_emu.py). The Rev 2.1 hardware accepts the first format; the PCM5101A needs the second.',
         ha='center', color=LN, fontsize=8)
fig.tight_layout(rect=(0.02, 0.03, 0.99, 1)); os.makedirs('docs', exist_ok=True)
fig.savefig('docs/audio_timing.png', facecolor=BG, bbox_inches='tight', pad_inches=0.15); print('docs/audio_timing.png written')
