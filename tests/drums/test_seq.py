# Sequencer tests against the REAL engine: timing, swing, tempo, transport, pattern queue, song, record, MIDI clock, pads, CCs.
import sys; sys.path.insert(0, '.')
import numpy as np, dsp_common as d
FS = 44100; L120 = FS * 15 / 120          # one 16th note at 120 BPM = 5512.5 samples
ok = True
def check(name, cond, detail=""):
    global ok; ok &= bool(cond); print(("PASS " if cond else "FAIL ") + name + (("  " + detail) if detail else ""))
def events(script):
    out = d.probe(script + "trace\n"); ev = []
    for ln in out.split('\n'):
        if ln.startswith('T '): t, i, v, p, s = map(int, ln.split()[1:]); ev.append((t, i, v, p, s))
    return ev, out
def field(out, key): return [l for l in out.split('\n') if l.startswith(key)]
d.build()
ALLBD = "clearall\n" + "".join(f"step 0 {s} 2\n" for s in range(16))          # pattern 0: a kick on every step

# ---- S1 sample-exact timing ----
ev, _ = events(ALLBD + "play\nrun 1\nskip %d\n" % int(2 * 16 * L120 + 100))
t = np.array([e[0] for e in ev]); k = np.arange(len(t)); dev = t - (t[0] + k * L120)
check("120 BPM: 32 steps in 2 bars", len(ev) == 33 or len(ev) == 32, f"{len(ev)} events")
check("every step within 1 sample of the ideal time (no drift)", np.abs(dev).max() <= 1.0, f"max deviation {np.abs(dev).max():.2f} samples over {len(t)} steps")
# marker pattern: track s fires only on step s, so the order of tracks IS the order of steps
mark = "clearall\n" + "".join(f"step {s} {s} 2\n" for s in range(16))             # track s fires only on step s
ev, _ = events(mark + "play\nskip %d\n" % int(2.5 * 16 * L120))
check("each step fires its own track, in order, repeating", [e[1] for e in ev[:35]] == [i % 16 for i in range(35)], str([e[1] for e in ev[:20]]))

# ---- S2 swing ----
v = 85; s = 0.5 + 0.25 * v / 127
ev, _ = events(ALLBD + f"set swing {v}\nplay\nskip %d\n" % int(4 * L120 * 2 + 50)); t = np.array([e[0] for e in ev], float)
exp = [0, 2 * s * L120, 2 * L120, 2 * L120 + 2 * s * L120, 4 * L120, 4 * L120 + 2 * s * L120]
check("swing %.1f%%: odd steps delayed, pairs keep the beat" % (s * 100), np.abs((t[:6] - t[0]) - exp).max() <= 1.5, f"measured {np.round(t[:6]-t[0],1)}  expected {np.round(exp,1)}")
ev, _ = events(ALLBD + "play\nskip 1000\nbpm 90\nskip %d\n" % int(5 * 7350))
t = np.array([e[0] for e in ev], float); gaps = np.diff(t)
check("tempo change mid-play: spacing follows the new tempo", abs(gaps[-1] - 44100 * 15 / 90) < 2 and abs(gaps[0] - L120) < 2, f"{gaps[0]:.1f} -> {gaps[-1]:.1f}")

# ---- S4 transport ----
ev, out = events(ALLBD + "play\nskip 20000\nstop\nskip 5000\ntraceclear\nskip 30000\n")
check("after STOP no step fires", len(ev) == 0)
ev, out = events(ALLBD + "play\nskip 20000\nstop\nskip 100\ntraceclear\nplay\nskip 100\n")
check("START fires step 0 within one control tick", len(ev) >= 1 and ev[0][0] - 20100 <= 24, f"{ev[0][0]-20100 if ev else None} samples")
ev, out = events(mark + "play\nskip %d\nstop\nskip 100\ntraceclear\nplay\nskip 100\n" % int(5.5 * L120))
check("START always restarts from step 0", ev[0][1] == 0, f"track {ev[0][1]}")
ev, out = events(mark + "play\nskip %d\nrt FC\nskip 100\ntraceclear\nrt FB\nskip %d\n" % (int(5.5 * L120), int(3 * L120)))
check("MIDI Stop then Continue resumes at the next step (6)", ev[0][1] == 6, f"track {ev[0][1]}")

# ---- S5 pattern queue ----
two = "clearall\n" + "".join(f"step 0 {s} 2\n" for s in range(16)) + "pat 1\n" + "".join(f"step 1 {s} 2\n" for s in range(16)) + "pat 0\n"
ev, _ = events(two + "play\nskip %d\npat 1\nskip %d\n" % (int(7.5 * L120), int(20 * L120)))
tb = [e for e in ev if e[0] >= 0]; first1 = next(e for e in ev if e[1] == 1); bar = 16 * L120
check("pattern change while playing waits for the bar end", all(e[1] == 0 for e in ev if e[0] < first1[0]) and abs(first1[0] - ev[0][0] - bar) <= 2, f"first pattern-1 hit at {first1[0]-ev[0][0]:.0f}, bar end {bar:.0f}")
ev, _ = events(two + "pat 1\nplay\nskip %d\n" % int(3 * L120))
check("pattern chosen while stopped plays immediately", ev[0][1] == 1)

# ---- S6 song ----
three = "clearall\n" + "".join(f"step 0 {s} 2\n" for s in range(16)) + "pat 1\n" + "".join(f"step 1 {s} 2\n" for s in range(16)) + "pat 2\n" + "".join(f"step 2 {s} 2\n" for s in range(16)) + "pat 0\n"
ev, _ = events(three + "song 0 1 1 2\nsongmode 1\nplay\nskip %d\n" % int(8.5 * bar))
seq = [ev[i * 16][1] for i in range(8)]
check("song chain 0,1,1,2 loops", seq == [0, 1, 1, 2, 0, 1, 1, 2], str(seq))
ev, _ = events(three + "song 0 1\nsongmode 1\npat 2\nplay\nskip %d\n" % int(3.5 * bar))
check("song mode ignores the queued pattern", [ev[i * 16][1] for i in range(3)] == [0, 1, 0])

# ---- S7 length ----
ev, _ = events(ALLBD + "len 12\nplay\nskip %d\n" % int(24.5 * L120)); t = np.array([e[0] for e in ev], float)
check("length 12: bar lasts 12 steps", abs((t[12] - t[0]) - 12 * L120) < 2 and len(ev) == 25, f"bar {(t[12]-t[0]):.0f} samples, {len(ev)} hits in 24.5 steps")

# ---- S8 levels ----
ev, _ = events("clearall\nstep 0 0 1\nstep 0 1 2\nstep 0 2 3\nplay\nskip %d\n" % int(3.5 * L120))
check("ghost / normal / accent = velocity 55 / 100 / 127", [e[2] for e in ev] == [55, 100, 127], str([e[2] for e in ev]))

# ---- S9 record quantise ----
def rec_script(times, insts, vels, extra=""):
    s = "clearall\nrec 1\nplay\nskip 1\n"; now = 1
    for tt, i, v in zip(times, insts, vels): s += f"skip {int(tt - now)}\nhit {i} {v}\n"; now = tt
    return s + f"skip 200\nserv\n{extra}grid\n"
t0 = 0
times = [4.3 * L120, 5.7 * L120, 15.8 * L120, 8.5 * L120 + 16 * L120 + 100]
out = d.probe(rec_script(times[:3], [0, 1, 6], [127, 90, 40]))
g = {l.split()[0]: l.split()[1] for l in out.split('\n') if len(l.split()) == 2 and len(l.split()[1]) == 16}
check("hit 30% into step 4 -> step 4", g['BD'][4] == '3', g['BD'])
check("hit 70% into step 5 -> step 6", g['SD'][6] == '2' and g['SD'][5] == '.', g['SD'])
check("hit late in the last step wraps to step 0", g['CP'][0] == '1', g['CP'])
check("velocity -> level (127->3, 90->2, 40->1)", g['BD'][4] == '3' and g['SD'][6] == '2' and g['CP'][0] == '1')
out = d.probe("clearall\nplay\nskip 1\nskip 20000\nhit 0 127\nskip 100\nserv\ngrid\n"); g = [l for l in out.split('\n') if l.startswith('BD')][0]
check("nothing is recorded unless REC is on", set(g.split()[1]) == {'.'})
out = d.probe("clearall\nrec 1\nskip 20000\nhit 0 127\nskip 100\nserv\ngrid\n"); g = [l for l in out.split('\n') if l.startswith('BD')][0]
check("nothing is recorded while stopped", set(g.split()[1]) == {'.'})

# ---- S11 MIDI clock ----
ev, _ = events(ALLBD + "sync 1\nplay\nclkrun 60 1000\n"); t = np.array([e[0] for e in ev], float)
check("external clock: one step per 6 ticks", len(ev) == 10 and np.allclose(np.diff(t), 6000, atol=16), f"{len(ev)} steps, gaps {np.unique(np.diff(t))}")
check("external clock: first tick after START fires step 0", ev[0][0] <= 16, f"t={ev[0][0]}")
ev, _ = events(ALLBD + "sync 0\nplay\nclkrun 50 100\n")
check("internal sync ignores clock ticks", len(ev) == 1, f"{len(ev)} events")
ev, _ = events(ALLBD + "sync 1\nplay\nclkrun 12 1000\nrt FC\ntraceclear\nclkrun 30 1000\n")
check("external clock stops on MIDI Stop", len(ev) == 0)
ev, _ = events(mark + "sync 1\nrt FA\nclkrun 20 1000\nrt FA\ntraceclear\nclkrun 7 1000\n")
check("MIDI Start restarts at step 0", ev[0][1] == 0, f"track {ev[0][1]}")
ev, _ = events("clearall\nstep 0 0 2\nstep 0 1 2\nstep 0 2 2\nstep 0 3 2\nsync 1\nrec 1\nplay\nclkrun 2 1000\nhit 5 100\nclkrun 2 1000\nhit 6 100\nclkrun 30 1000\nserv\ngrid\n")
g = {l.split()[0]: l.split()[1] for l in _.split('\n') if len(l.split()) == 2 and len(l.split()[1]) == 16}
check("record under external clock quantises by ticks (2/6 -> same step, 4/6 -> next)", g['RS'][0] == '2' and g['CP'][1] == '2', f"RS {g['RS']} CP {g['CP']}")

# ---- S14 pads and CCs ----
out = d.probe("pad 36 100\npad 51 100\npad 52 100\npad 38 100\npads 1\npad 38 100\npad 46 100\npad 36 100\npad 63 100\npad 35 100\nstatus\n")
pads = [int(l.split()[1]) for l in out.split('\n') if l.startswith('PAD')]
check("linear pad map 36..51 -> 0..15, others rejected", pads[:4] == [0, 15, -1, 2], str(pads[:4]))
check("GM map: 38 -> SD, 46 -> OH, 36 -> BD, 63 -> MC, 35 -> BD", pads[4:] == [1, 9, 0, 14, 0], str(pads[4:]))
out = d.probe("pad 40 100\ncc 1 100\nparam? 2\ncc 4 77\nparam? 1\ncc 5 40\nparam? 6\nstatus\ncc 27 56\nstatus\ncc 99 1\n")
check("AKAI knobs: K1 tune, K4 level, K5 tempo (bpm = 60 + v)", "PV 100" in out and "PV 77" in out and "bpm=100.00" in out, " ".join(out.split()))
check("a pad hit selects its instrument (pad 40 -> sel 4)", "PAD 4" in out and "sel=4" in out)
check("CC27 selects instrument value/8", "sel=7" in out)
check("unknown CC is reported unhandled", "CC 0" in out)
x = d.read_stereo((d.probe("hit 10 127\nrun 2000\ncc 120 0\nrun 4000\nrun 100\nwrite /tmp/pn.raw\n"), '/tmp/pn.raw')[1]) / 32768.0
pre = np.abs(x[:2000]).max(); post = np.abs(x[2000 + 1500:2000 + 4000]).max()
check("CC120 panic chokes everything fast (>= 40 dB in ~30 ms)", 20 * np.log10(post / pre + 1e-12) < -40, f"{20*np.log10(post/pre+1e-12):.1f} dB")
x = d.read_stereo((d.probe("set vol 0\nhit 0 127\nrun 2000\nwrite /tmp/v.raw\n"), '/tmp/v.raw')[1]); check("volume 0 is silent", np.abs(x).max() == 0)
lo = np.abs(d.read_stereo((d.probe("param 0 0 40\nset drive 0\nhit 0 127\nrun 4000\nwrite /tmp/v.raw\n"), '/tmp/v.raw')[1])).max(); hi = np.abs(d.read_stereo((d.probe("param 0 0 40\nset drive 127\nhit 0 127\nrun 4000\nwrite /tmp/v.raw\n"), '/tmp/v.raw')[1])).max()
check("drive raises the level into the limiter (~+10..14 dB at 127)", 8 < 20 * np.log10(hi / lo) < 14, f"{20*np.log10(hi/lo):.1f} dB")
ev, _ = events("clearall\nstep 0 0 2\nsongmode 1\nsong 255\nplay\nskip 1000\n")
check("song mode with an empty chain falls back to the selected pattern", len(ev) >= 1 and ev[0][3] == 0)
print("\nALL SEQUENCER TESTS PASS" if ok else "\nSEQUENCER TESTS FAILED"); sys.exit(0 if ok else 1)
