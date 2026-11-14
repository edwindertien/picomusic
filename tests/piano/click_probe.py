import sys, subprocess, os; sys.path.insert(0, '.')
import numpy as np
FS = 44100
NEUTRAL = "set vol 110\nset tone 64\nset hard 64\nset width 64\nset tune 64\nset soft 0\nset decay 127\nset rel 64\n"
def build(extra, out):
    r = subprocess.run(['g++','-std=gnu++17','-O1',*extra,'-I','mock','-I','stage','probe.cpp','stage/piano_data.cpp','mock/mock_globals.cpp','mock/mock_globals2.cpp','-o',out],capture_output=True,text=True); assert r.returncode==0, r.stderr[:500]
def render(exe, script):
    out = subprocess.run([exe], input=script + "trace\n", capture_output=True, text=True).stdout
    steals = [int(l.split()[1]) for l in out.split('\n') if l.startswith('T ') and int(l.split()[2]) == 2]
    return np.fromfile('/tmp/click.raw', dtype=np.int16).astype(float).reshape(-1,2).sum(1)/2/32768, steals
def click_ratio(x, steals):
    """at each steal: the largest second difference within 3 samples, relative to the median second difference in the +-10 ms around it"""
    d2 = np.abs(np.diff(x, 2)); out = []
    for t in steals:
        a = d2[max(0, t - 3):t + 4].max(); ref = np.median(d2[max(0, t - 441):t + 441]) + 1e-7; out.append(a / ref)
    return out
def metric(x):
    d = np.abs(np.diff(x)); w = 441; rms = np.sqrt(np.convolve(x**2, np.ones(w)/w, 'same')) + 1e-4
    return float((d / rms[1:]).max()), int(np.argmax(d / rms[1:]))
def scenario(extra_notes=40):
    s = NEUTRAL + "pedal 1\n"
    for i in range(extra_notes): s += f"on {36 + (i * 7) % 60} {70 + (i % 5) * 6}\nrun 2205\n"
    return s + "write /tmp/click.raw\n"
if __name__ == "__main__":
    build([], '/tmp/click_new'); 
    x, st = render('/tmp/click_new', scenario()); r = click_ratio(x, st); print(f"{len(st)} steals; click ratio per steal (max |d2| at the steal / median |d2| around it): " + " ".join(f"{v:.1f}" for v in r)); print(f"worst {max(r):.1f}   median {np.median(r):.1f}")
    # for scale: the same ratio at 15 random points with no steal
    rng = np.random.RandomState(1); ts = rng.randint(2000, len(x) - 2000, 40); rr = click_ratio(x, list(ts)); print(f"reference (random points, no steal): worst {max(rr):.1f}  median {np.median(rr):.1f}")
