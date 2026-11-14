"""Minimal RP2040/RP2350 PIO emulator (just the opcodes these audio programs use) + an I2S receiver model."""
import re, numpy as np

def parse_header(path):
    s = open(path).read()
    words = [int(w, 16) for w in re.findall(r'0x([0-9a-fA-F]{4}),\s*//\s*\d+:', s)]
    wrap_target = int(re.search(r'_wrap_target\s+(\d+)', s).group(1)); wrap = int(re.search(r'_wrap\s+(\d+)', s).group(1))
    entry = re.search(r'offset_entry_point\s+(\d+)u', s); entry = int(entry.group(1)) if entry else 0
    return words, wrap_target, wrap, entry

def disasm(w, sideset_bits=2):
    op = w >> 13; delay_side = (w >> 8) & 0x1f
    side = delay_side >> (5 - sideset_bits); delay = delay_side & ((1 << (5 - sideset_bits)) - 1)
    arg = w & 0xff
    if op == 0b000:
        cond = (arg >> 5) & 7; addr = arg & 31
        t = {0: 'jmp', 2: 'jmp x--', 3: 'jmp x!=y'}.get(cond, f'jmp?{cond}')
        s = f'{t} {addr}'
    elif op == 0b011: s = f'out {["pins","x","y","null"][ (arg>>5)&3 ] if ((arg>>5)&7)<4 else "?"}, {arg&31 or 32}'
    elif op == 0b111: s = f'set {["pins","x","y","?","pindirs"][(arg>>5)&7] if ((arg>>5)&7)<5 else "?"}, {arg&31}'
    elif op == 0b101: s = 'nop' if arg == 0x42 else f'mov {arg:#x}'
    else: s = f'op{op}'
    return f'{s:14} side {side}' + (f' [{delay}]' if delay else '')

class PIO:
    def __init__(self, words, wrap_target, wrap, pc0, sideset_bits=2, shift_left=True, autopull=32):
        self.w, self.wt, self.wr, self.pc = words, wrap_target, wrap, pc0
        self.sb = sideset_bits; self.x = 0; self.osr = 0; self.cnt = 32; self.fifo = []; self.din = 0; self.side = 0
        self.autopull = autopull; self.stalled = 0
    def feed(self, word): self.fifo.append(word & 0xffffffff)
    def cycle(self):
        """execute one PIO cycle; return (BCK, LRCK, DIN) after it"""
        w = self.w[self.pc]; op = w >> 13; ds = (w >> 8) & 0x1f; side = ds >> (5 - self.sb); arg = w & 0xff
        nxt = self.pc + 1 if self.pc != self.wr else self.wt
        if op == 0b011:                                   # OUT pins, n   (autopull + shift left = MSB first)
            if self.cnt >= self.autopull:
                if not self.fifo: self.stalled += 1; return (self.side & 1, (self.side >> 1) & 1, self.din)   # stall, pins hold
                self.osr = self.fifo.pop(0); self.cnt = 0
            n = (arg & 31) or 32
            self.din = (self.osr >> 31) & 1; self.osr = (self.osr << 1) & 0xffffffff; self.cnt += n
        elif op == 0b111:                                 # SET x, v
            if (arg >> 5) == 1: self.x = arg & 31
        elif op == 0b000:                                 # JMP x--
            cond = (arg >> 5) & 7
            if cond == 2:
                jump = self.x != 0; self.x = (self.x - 1) & 0xffffffff
                if jump: nxt = arg & 31
        self.side = side; self.pc = nxt
        return (side & 1, (side >> 1) & 1, self.din)

def run(words, wt, wr, pc0, frames, sb=2):
    """feed 32-bit words (L<<16|R), return per-cycle (bck, lrck, din) trace"""
    p = PIO(words, wt, wr, pc0, sb)
    for f in frames: p.feed(f)
    tr = []; 
    while p.fifo or p.cnt < 32:
        tr.append(p.cycle())
    return np.array(tr, dtype=np.uint8), p

def i2s_decode(trace, slot_bits=32):
    """Receive like a standard I2S DAC: sample DIN on BCK rising edges, LRCK edge + 1 BCK delay, MSB first.
    Returns list of (channel, [bits...]) for each LRCK half-period, and the 32-bit signed word."""
    bck, lrck, din = trace[:, 0].astype(int), trace[:, 1].astype(int), trace[:, 2].astype(int)
    rise = np.where((bck[1:] == 1) & (bck[:-1] == 0))[0] + 1
    samples = [(lrck[i], din[i]) for i in rise]
    # LRCK edges (as seen at rising BCK edges)
    out = []; cur = None; bits = []
    for i, (l, d) in enumerate(samples):
        if cur is None: cur = l; continue
        if l != cur:                       # first rising edge after the LRCK edge: previous channel's LSB (I2S delay)
            bits.append(d); out.append((cur, bits)); cur = l; bits = []
        else: bits.append(d)
    return out

def word_from_bits(bits, n=32):
    b = (bits + [0] * n)[:n]; v = 0
    for x in b: v = (v << 1) | x
    return v - (1 << n) if v >> (n - 1) else v
