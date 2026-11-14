# Sequencer + keyboard tests against the REAL engine: timing, gate, swing, slide, accent, transport, queue, transpose, step-write, clock.
import sys; sys.path.insert(0, '.')
import numpy as np, dsp_common as d
FS = 44100; L120 = FS * 15 / 120
ok = True
def check(name, cond, detail=""):
    global ok; ok &= bool(cond); print(("PASS " if cond else "FAIL ") + name + (("  " + detail) if detail else ""))
def events(script):
    out = d.probe(script + "trace\n"); ev = []
    for ln in out.split('\n'):
        if ln.startswith('T '): t, k, n, v, a, p, s = map(int, ln.split()[1:]); ev.append((t, k, n, v, a, p, s))
    return ev, out
def ons(ev): return [e for e in ev if e[1] in (1, 2)]
d.build()
EVERY = "clearall\n" + "".join(f"step {s} 36 1\n" for s in range(16)) + "set gate 64\n"          # a C2 on every step
MARK = "clearall\n" + "".join(f"step {s} {36+s} 1\n" for s in range(16))                         # step s plays note 36+s

# ---- timing ----
ev, _ = events(EVERY + "play\nskip %d\n" % int(2 * 16 * L120 + 100)); on = ons(ev); t = np.array([e[0] for e in on], float)
check("note-ons are within 1 sample of the ideal time for 33 steps (no drift)", np.abs(t - (t[0] + np.arange(len(t)) * L120)).max() <= 1.0, f"{len(on)} notes, max deviation {np.abs(t-(t[0]+np.arange(len(t))*L120)).max():.2f} samples")
ev, _ = events(MARK + "play\nskip %d\n" % int(2.5 * 16 * L120)); check("each step plays its own note, in order, looping", [e[2] for e in ons(ev)][:35] == [36 + i % 16 for i in range(35)])
# ---- gate length ----
frac = 0.10 + 0.85 * 64 / 127
ev, _ = events(EVERY + "play\nskip %d\n" % int(3 * L120)); dur = [e[0] for e in ev if e[1] == 0][0] - ev[0][0]
check(f"gate length = GATE percent of the step (64 -> {frac*100:.1f} %)", abs(dur - frac * L120) <= 2, f"{dur} samples, expected {frac*L120:.0f}")
ev, _ = events(EVERY + "set gate 0\nplay\nskip %d\n" % int(2 * L120)); dur = [e[0] for e in ev if e[1] == 0][0] - ev[0][0]; check("GATE 0 = 10 % (staccato, never zero)", abs(dur - 0.1 * L120) <= 2, f"{dur}")
ev, _ = events(EVERY + "set gate 127\nplay\nskip %d\n" % int(2 * L120)); dur = [e[0] for e in ev if e[1] == 0][0] - ev[0][0]; check("GATE 127 = 95 % (gap before the next note)", abs(dur - 0.95 * L120) <= 2, f"{dur}")
# ---- swing ----
v = 85; s = 0.5 + 0.25 * v / 127
ev, _ = events(EVERY + f"set swing {v}\nplay\nskip %d\n" % int(4 * L120 * 2 + 50)); t = np.array([e[0] for e in ons(ev)], float)
exp = [0, 2 * s * L120, 2 * L120, 2 * L120 + 2 * s * L120]
check(f"swing {s*100:.1f} %: odd steps delayed, pairs keep the beat", np.abs((t[:4] - t[0]) - exp).max() <= 1.5, f"{np.round(t[:4]-t[0],1)} vs {np.round(exp,1)}")
ev, _ = events(EVERY + f"set swing {v}\nset gate 64\nplay\nskip %d\n" % int(3 * L120)); offs = [e[0] for e in ev if e[1] == 0]; onst = [e[0] for e in ons(ev)]
check("under swing the gate scales with its own (longer / shorter) step", abs((offs[0] - onst[0]) - frac * 2 * s * L120) <= 2 and abs((offs[1] - onst[1]) - frac * 2 * (1 - s) * L120) <= 2, f"{offs[0]-onst[0]} / {offs[1]-onst[1]}")
ev, _ = events(EVERY + "play\nskip 1000\nbpm 90\nskip %d\n" % int(5 * 7350)); gaps = np.diff([e[0] for e in ons(ev)]); check("tempo change mid-play: spacing follows", abs(gaps[0] - L120) < 2 and abs(gaps[-1] - 44100 * 15 / 90) < 2, f"{gaps[0]:.1f} -> {gaps[-1]:.1f}")
# ---- slide, accent, rests ----
SL = "clearall\nstep 0 36 3\nstep 1 43 1\nstep 2 36 1\nset gate 64\nset glide 40\n"                  # C2 slides into G2, then a detached C2
ev, _ = events(SL + "play\nskip %d\n" % int(4 * L120)); kk = [(e[1], e[2]) for e in ev]
check("slide: the next note is LEGATO (no gate-off in between), the gate is held through the step", kk[:4] == [(1, 36), (2, 43), (0, 43), (1, 36)], str(kk))
ev, _ = events(SL + "play\nskip %d\n" % int(4 * L120)); check("slide: legato note arrives exactly on the step, the held gate ends by the gate length of the LAST note", abs(ev[1][0] - ev[0][0] - L120) <= 1 and abs((ev[2][0] - ev[1][0]) - frac * L120) <= 2)
ev, _ = events("clearall\nstep 0 36 3\nstep 1 0 0\nstep 2 40 1\nplay\nskip %d\n" % int(4 * L120)); kk = [(e[1], e[2]) for e in ev]
check("slide into a REST just ends the note on the rest (nothing hangs)", kk[0] == (1, 36) and kk[1][0] == 0 and abs(ev[1][0] - ev[0][0] - L120) <= 2, str(kk))
ev, _ = events("clearall\nstep 0 36 5\nstep 1 36 1\nplay\nskip %d\n" % int(3 * L120)); on = ons(ev); check("accent step: velocity 127 + accent flag; normal step 100", on[0][3] == 127 and on[0][4] == 1 and on[1][3] == 100 and on[1][4] == 0, str([(e[3], e[4]) for e in on]))
ev, _ = events("clearall\nstep 0 36 1\nstep 2 38 1\nplay\nskip %d\n" % int(4 * L120)); on = ons(ev); check("rests are silent (steps 2 and 4 of the pattern play nothing)", [round((e[0] - on[0][0]) / L120) for e in on] == [0, 2], str([round((e[0]-on[0][0])/L120) for e in on]))
# ---- transport ----
ev, _ = events(EVERY + "play\nskip 20000\nstop\nskip 100\ntraceclear\nskip 30000\n"); check("after STOP no step fires", len(ev) == 0)
ev, _ = events("clearall\nstep 0 36 3\nstep 1 40 1\nplay\nskip %d\nstop\nskip 100\n" % int(1.3 * L120)); kk = [e[1] for e in ev]
check("STOP releases a held (sliding) note: nothing hangs", kk[-1] == 0, str(kk))
pre = int(5.5 * L120) + 100
ev, _ = events(MARK + "play\nskip %d\nstop\nskip 100\ntraceclear\nplay\nskip 100\n" % int(5.5 * L120)); check("START restarts from step 1 within one control tick (16 samples)", ev[0][2] == 36 and 0 <= ev[0][0] - pre <= 16, f"note {ev[0][2]}, {ev[0][0]-pre} samples after the command")
ev, _ = events(MARK + "play\nskip %d\nrt FC\nskip 100\ntraceclear\nrt FB\nskip %d\n" % (int(5.5 * L120), int(3 * L120))); check("MIDI Stop then Continue resumes at the next step (7)", ons(ev)[0][2] == 36 + 6, f"note {ons(ev)[0][2]}")
# ---- pattern queue, length ----
two = "clearall\n" + "".join(f"step {s} 36 1\n" for s in range(16)) + "pat 1\n" + "".join(f"step {s} 48 1\n" for s in range(16)) + "pat 0\n"
ev, _ = events(two + "play\nskip %d\npat 1\nskip %d\n" % (int(7.5 * L120), int(20 * L120))); first = next(e for e in ons(ev) if e[2] == 48)
check("pattern change while playing waits for the bar end", all(e[2] == 36 for e in ons(ev) if e[0] < first[0]) and abs(first[0] - ons(ev)[0][0] - 16 * L120) <= 2, f"{first[0]-ons(ev)[0][0]:.0f} vs {16*L120:.0f}")
ev, _ = events(two + "pat 1\nplay\nskip %d\n" % int(3 * L120)); check("pattern chosen while stopped plays immediately", ons(ev)[0][2] == 48)
ev, _ = events(EVERY + "len 12\nplay\nskip %d\n" % int(24.5 * L120)); t = np.array([e[0] for e in ons(ev)], float); check("length 12: the bar lasts 12 steps", abs((t[12] - t[0]) - 12 * L120) < 2 and len(ons(ev)) == 25)

# ---- keyboard: transpose / play-over / step-write ----
ev, _ = events(MARK + "play\nskip %d\non 43 100\nskip %d\n" % (int(1.5 * L120), int(3 * L120))); n = [e[2] for e in ons(ev)]
check("keys TRANSPOSE the running sequence (G2 = +7 semitones), no extra voice event", n[:2] == [36, 37] and n[2:4] == [38 + 7, 39 + 7] and 43 not in n[:2], str(n[:5]))
ev, _ = events("keymode 0\n" + MARK + "play\nskip %d\non 60 100\nskip 100\n" % int(1.5 * L120)); check("keymode 0: keys play OVER the sequence", any(e[2] == 60 for e in ons(ev)))
ev, _ = events("on 43 100\nskip 100\n"); check("stopped: keys always play", ons(ev)[0][2] == 43)
st = d.probe(MARK + "play\nskip %d\non 43 100\nstatus\n" % int(1.5 * L120)); check("transposition is latched (stays after the key is released)", "transpose=7" in st, st.split('\n')[-2][-50:])
st = d.probe(MARK + "play\nskip %d\non 43 100\nskip 100\non 36 100\nstatus\n" % int(1.5 * L120)); check("playing the root key (C2) clears the transposition", "transpose=0" in st)
out = d.probe("clearall\nwrite_on 1\non 40 100\non 43 127\npedal 1\npedal 0\non 36 100\ncursor 0\ngrid\n"); g = out.strip().split('\n')[-1].split()
check("step-write: keys enter notes at the cursor and advance; velocity >= 112 = accent; the pedal enters a rest", g[0] == "E2" and g[1] == "G2a" and g[2] == "." and g[3] == "C2", " ".join(g[:5]))
out = d.probe("clearall\nlen 3\nwrite_on 1\non 40 100\non 41 100\non 42 100\non 43 100\ngrid\n"); g = out.strip().split('\n')[-1].split(); check("step-write wraps at the pattern length", g[0] == "G2" and g[1] == "F2" and g[2] == "F#2", " ".join(g[:4]))
st = d.probe("clearall\nwrite_on 1\ncursor 5\non 40 100\nstatus\n"); check("step-write moves the cursor", "cursor=6" in st)

# ---- MIDI clock ----
ev, _ = events(EVERY + "sync 1\nplay\nclkrun 60 1000\n"); t = np.array([e[0] for e in ons(ev)], float); check("external clock: one step per 6 ticks", len(ons(ev)) == 10 and np.allclose(np.diff(t), 6000, atol=16), f"{len(ons(ev))} steps, gaps {np.unique(np.diff(t))}")
check("external clock: the first tick after START fires step 1", ons(ev)[0][0] <= 16)
offs = [e[0] for e in ev if e[1] == 0]; g = [o - n for n, o in zip([e[0] for e in ons(ev)][1:], offs[1:])]
check("external clock: gate length follows the measured tick interval (64 percent of 6 ticks)", all(abs(x - frac * 6000) < 40 for x in g[2:]), f"{g[2:5]} vs {frac*6000:.0f}")
ev, _ = events(EVERY + "sync 0\nplay\nclkrun 50 100\n"); check("internal sync ignores clock ticks", len(ons(ev)) == 1)
ev, _ = events(EVERY + "sync 1\nplay\nclkrun 12 1000\nrt FC\ntraceclear\nclkrun 30 1000\n"); check("external clock stops on MIDI Stop", len(ev) == 0)
ev, _ = events(MARK + "sync 1\nrt FA\nclkrun 20 1000\nrt FA\ntraceclear\nclkrun 7 1000\n"); check("MIDI Start restarts at step 1", ons(ev)[0][2] == 36)

# ---- CCs, panic, patches ----
out = d.probe("cc 1 100\nparam? 15\ncc 2 90\nparam? 16\ncc 5 77\nparam? 13\ncc 7 55\nparam? 14\ncc 74 33\nparam? 15\ncc 99 1\n")
check("AKAI knobs: K1 cutoff, K2 resonance, K5 glide, K7 volume; CC74 is cutoff too", "PV 100" in out and "PV 90" in out and "PV 77" in out and "PV 55" in out and "PV 33" in out and "CC 0" in out, " ".join(out.split()))
d.probe("on 36 100\nskip 8000\npanic\nrun 6000\nwrite /tmp/pn.raw\n"); x = np.fromfile('/tmp/pn.raw', dtype=np.int16).astype(float) / 32768
d.probe("on 36 100\nskip 8000\nrun 6000\nwrite /tmp/pn0.raw\n"); x0 = np.fromfile('/tmp/pn0.raw', dtype=np.int16).astype(float) / 32768
check("panic: -40 dB within 20 ms and exact silence soon after", 20 * np.log10(np.abs(x[int(.02 * FS):int(.03 * FS)]).max() / np.abs(x0[:500]).max() + 1e-12) < -40 and np.abs(x[-500:]).max() == 0, f"{20*np.log10(np.abs(x[int(.02*FS):int(.03*FS)]).max()/np.abs(x0[:500]).max()+1e-12):.0f} dB at 20-30 ms")
out = d.probe("set tempo 100\nset swing 50\npatch 2\nparam? 31\nparam? 32\nparam? 15\nparam? 16\n"); check("loading a patch keeps tempo and swing, sets everything else", "PV 100" in out and "PV 50" in out and "PV 30" in out and "PV 108" in out, " ".join(out.split()))
out = d.probe("cc 64 127\non 36 100\nskip 100\noff 36\nskip 100\nstatus\n"); check("CC64 is the sustain pedal", "gate=1" in out)
out = d.probe("on 36 100\ncc 123 0\nskip 100\nstatus\n"); check("CC123 releases the note", "gate=0" in out)
print("\nALL SEQUENCER TESTS PASS" if ok else "\nSEQUENCER TESTS FAILED"); sys.exit(0 if ok else 1)
