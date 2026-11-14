# Equalise the loudness of the four patches: each plays the same 4-bar groove, RMS over the whole render is matched.
#   python3 calibrate_patches.py          report
#   python3 calibrate_patches.py --apply  rewrite the VOL value of every patch in src/bass_params.h
import sys, re; sys.path.insert(0, '.')
import numpy as np, dsp_common as d
FS = 44100; TARGET_DB = -19.5; NAMES = ["Moog Bass", "Sub", "Acid", "Pluck"]
def level(p, vol):
    bar = 16 * int(FS * 15 / 120)
    d.probe(f"patch {p}\nset vol {vol}\nclear 0\npat 0\n" + "".join(f"step {s} {n} {f}\n" for s, (n, f) in enumerate(GROOVE)) + f"play\nrun {4*bar}\nwrite /tmp/c.raw\n")
    x = np.fromfile('/tmp/c.raw', dtype=np.int16).astype(float) / 32768; return 20 * np.log10(np.sqrt((x ** 2).mean()) + 1e-9), float(np.abs(x).max())
GROOVE = [(36,1),(36,0),(36,1),(36,0),(36,0),(48,5),(36,0),(36,1),(39,1),(36,0),(39,1),(36,0),(41,1),(36,0),(43,3),(31,1)]      # the 'Groove' pattern
if __name__ == '__main__':
    d.build(); vols = []
    for p in range(4):
        v = int(re.findall(r'\d+', re.split(r'\n', open('../src/bass_params.h').read().split('static const Patch PATCHES')[1].split('};')[0])[1 + 2 * p].split('}')[0] if False else '0')[0]) if False else None
        import subprocess; cur = int(d.probe(f"patch {p}\nparam? 14\n").split()[1])
        for _ in range(3):
            db, pk = level(p, cur); cur = int(np.clip(round(cur * 10 ** ((TARGET_DB - db) / 40)), 1, 127))     # RMS ~ vol^2: dB = 40 log10(vol)
        db, pk = level(p, cur); vols.append(cur); print(f"{NAMES[p]:10s}  VOL {cur:3d}   rms {db:6.1f} dBFS   peak {pk:.2f}")
    if '--apply' in sys.argv:
        path = '../src/bass_params.h'; s = open(path).read(); a = s.index('static const Patch PATCHES'); b = s.index('};', a); blk = s[a:b]; lines = blk.split('\n'); k = 0
        for i, ln in enumerate(lines):
            m = re.match(r'(\s*\{ "[^"]+",\s*\{.*?)(\d+),\s*$', ln)
            if m and k < 4: lines[i] = f"{m.group(1)}{vols[k]:3d},"; k += 1
        assert k == 4; open(path, 'w').write(s[:a] + '\n'.join(lines) + s[b:]); print("applied")
