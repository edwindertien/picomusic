"""Audio driver end-to-end test: runs the REAL src/audio_driver.h against a mocked pico-sdk PIO API, replays what it
programmed through a PIO emulator and decodes it with an I2S receiver model (rising-edge sampling, per TI SLAS859C)."""
import subprocess, sys, os, tempfile, numpy as np
HERE = os.path.dirname(os.path.abspath(__file__)); sys.path.insert(0, HERE)
from pio_emu import *
fs = 44100
# Source directories (os.pathsep separated). Default: ../src.  Tonewheel layout:  AUDIO_SRC=../include:../src
DIRS = [d for d in os.environ.get('AUDIO_SRC', os.path.join(HERE, '..', 'src')).split(os.pathsep) if d]
def find(name):
    for d in DIRS:
        if os.path.exists(os.path.join(d, name)): return os.path.join(d, name)
    raise FileNotFoundError(name + ' not found in ' + str(DIRS))
ORGAN = parse_header(find('audio_pio.pio.h'))[0]
def build_and_run(board):
    exe = os.path.join(tempfile.gettempdir(), f'test_audio_{board}')
    r = subprocess.run(['g++', '-std=gnu++17', '-O1', '-Wall', '-Wextra', '-Wno-unused-function', f'-DAUDIO_BOARD_REV2_1={board}', '-I', os.path.join(HERE, 'mock'), *sum((['-I', d] for d in DIRS), []),
                        os.path.join(HERE, 'test_audio.cpp'), os.path.join(HERE, 'mock/mock_globals.cpp'), os.path.join(HERE, 'mock/mock_globals2.cpp'), os.path.join(HERE, 'mock/mock_pio.cpp'), '-o', exe], capture_output=True, text=True)
    if r.returncode or r.stderr.strip(): print(r.stderr[:1500])
    assert r.returncode == 0
    o = subprocess.run([exe], capture_output=True, text=True); return o.stdout, o.stderr
def parse(out):
    d = {}; words = []
    for line in out.split('\n'):
        t = line.split()
        if not t: continue
        if t[0] == 'W': words.append(int(t[1]))
        else: d[t[0]] = t[1:]
    d['words'] = words; return d
def sndr(ref, got):
    best = -99
    for lag in range(0, 8):
        a = ref[:len(got)-lag].astype(float); b = got[lag:lag+len(a)]; m = min(len(a), len(b)); a, b = a[:m], b[:m]
        if b @ b == 0: continue
        g = (a @ b) / (b @ b); e = a - g * b; best = max(best, 10*np.log10((a @ a) / (e @ e + 1e-12)))
    return best
t = np.arange(3000); xl = np.array([int(14000*np.sin(2*np.pi*1000.0*i/44100.0)) for i in t]); xr = np.array([int(9000*np.sin(2*np.pi*1700.0*i/44100.0+1.0)) for i in t])
ok = True
def check(name, cond, detail=''):
    global ok; ok &= bool(cond); print(f"  [{'ok' if cond else 'FAIL'}] {name} {detail}")
for board in (1, 0):
    out, err = build_and_run(board); d = parse(out); prog = [int(x) for x in d['PROGRAM']]
    wt, wr = map(int, d['WRAP']); sb, sbase = map(int, d['SIDE']); jmp = int(d['EXEC'][0]); clkdiv = float(d['CLKDIV'][0]); obase = int(d['OUT'][0])
    dirv, dirm = map(int, d['DIR']); gpio = [int(x) for x in d['GPIO']]
    print(f"\n=========== AUDIO_BOARD_REV2_1={board}  ({'Rev2.1 / CS4344' if board else 'Initial Version / PCM5101A'}) ===========")
    pins = (22, 26, 27) if board else (26, 27, 28)
    check("pins DIN/BCK/LRCK", (obase, sbase) == (pins[0], pins[1]) and set(gpio) == set(pins), f"out base GP{obase}, side-set base GP{sbase}, gpio_init {sorted(gpio)}")
    check("only the 3 audio pins are made outputs", dirm == sum(1 << p for p in pins) and dirv == dirm)
    check("starts at entry_point via jmp", jmp == (11 if board else 7), f"(jmp {jmp})")
    check("autopull 32, MSB first, 2 side-set bits", (d['SHIFT'] == ['0', '1', '32']) and sb == 2)
    # replay
    tr, p = run(prog, wt, wr, jmp, d['words'][:3000], sb)
    cyc_frame = len(tr) / 3000.0
    fs_real = 240e6 / clkdiv / cyc_frame; bck = 240e6 / clkdiv / 2
    check("sample rate", abs(fs_real / fs - 1) < 1e-4, f"fs = {fs_real:.1f} Hz, BCK = {bck/1e3:.1f} kHz  ({bck/fs:.0f} x fs), clkdiv {clkdiv:.3f}")
    if board:
        check("program is byte-identical to the organ's proven one (unchanged Rev2.1 path)", prog == ORGAN)
        # what the Rev2.1 evidently accepts: one sample per two BCK edges
        bck_, lrck_, din_ = tr[:,0].astype(int), tr[:,1].astype(int), tr[:,2].astype(int)
        rise = np.where((bck_[1:] == 1) & (bck_[:-1] == 0))[0] + 1; L = lrck_[rise][0::2]; D = din_[rise][0::2]
        e = np.where(L[1:] != L[:-1])[0] + 1; vals = [word_from_bits(list(D[i+1:i+17]), 16) for i in e if L[i] == 0 and len(D[i+1:i+17]) == 16]
        v = np.array(vals, float); s_ = max(sndr(xl, v), sndr(xr, v))
        check("Rev2.1 receiver model (every 2nd BCK edge) recovers the audio", s_ > 100, f"SNDR {s_:.0f} dB")
        slots = i2s_decode(tr); d32 = np.array([word_from_bits(b, 32) for c, b in slots if c == 0], float)
        print(f"      (for reference, a standard PCM5101A-style receiver on THIS program: SNDR {max(sndr(xl,d32),sndr(xr,d32)):.1f} dB  -> why it cannot be used on the PCM5101A)")
    else:
        check("I2S program is the validated one", prog == [0x7001,0x1840,0x6001,0xe82e,0x6001,0x0844,0x7001,0xf82e])
        slots = i2s_decode(tr)
        dl = np.array([word_from_bits(b, 16) for c, b in slots if c == 0], float); dr = np.array([word_from_bits(b, 16) for c, b in slots if c == 1], float)
        # the first slot of each channel is partial (the receiver, like a real DAC, starts mid-stream): look at samples from #4 on
        K = 4
        check("the ONLY corrupt sample is the partial first slot", int(np.sum(dr[:8] != xr[:8])) <= 1 and int(np.sum(dl[:8] != xl[:8])) == 0,
              f"(right mismatches in first 8 decoded: {int(np.sum(dr[:8] != xr[:8]))}, left: {int(np.sum(dl[:8] != xl[:8]))})")
        check("standard I2S receiver, LRCK=0 (LEFT) slot  == left input ", sndr(xl[K:], dl[K:]) > 100, f"SNDR {sndr(xl[K:],dl[K:]):.0f} dB  (vs right: {sndr(xr[K:],dl[K:]):.1f} dB)")
        check("standard I2S receiver, LRCK=1 (RIGHT) slot == right input", sndr(xr[K:], dr[K:]) > 100, f"SNDR {sndr(xr[K:],dr[K:]):.0f} dB  (vs left: {sndr(xl[K:],dr[K:]):.1f} dB)")
        check("bit-exact, both channels", np.array_equal(dl[K:2900], xl[K:2900]) and np.array_equal(dr[K:2900], xr[K:2900]), "(2896 samples each, lag 0, no differing bit)")
    print("      driver debug output:", ' | '.join(l.strip() for l in err.strip().split('\n') if 'BCK  ' in l or 'LRCK (fs)' in l or 'Board' in l or 'clkdiv' in l))
print("\nAUDIO DRIVER TESTS:", "PASS" if ok else "FAIL"); sys.exit(0 if ok else 1)
