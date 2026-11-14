import sys; sys.path.insert(0,'.')
import numpy as np, dsp_common as d
from drum_names import NAMES
FS=44100
def render(i, n=FS, vel=127, setup=""):
    d.probe(setup + f"hit {i} {vel}\nrun {n}\nwrite /tmp/i.raw\n")
    x = d.read_stereo('/tmp/i.raw'); return (x[:,0]+x[:,1])/2/32768.0
def t_to(x, db, win=64):
    e = np.sqrt(np.convolve(x**2, np.ones(win)/win, 'same')); pk = e.max()
    idx = np.where(e > pk*10**(db/20))[0]; return idx[-1]/FS if len(idx) else 0
def centroid(x):
    s = np.abs(np.fft.rfft(x*np.hanning(len(x))))**2; f = np.fft.rfftfreq(len(x),1/FS); return (s*f).sum()/s.sum()
def peakfreq(x, lo=20, hi=6000):
    s = np.abs(np.fft.rfft(x*np.hanning(len(x)), 1<<17)); f = np.fft.rfftfreq(1<<17,1/FS); m=(f>lo)&(f<hi); return f[m][np.argmax(s[m])]
if __name__ == "__main__":
    d.build()
    print("inst   peak   rms300ms  centroid  t-20dB  t-40dB  peakHz")
    for i in range(16):
        x = render(i); r = np.sqrt(np.mean(x[:int(.3*FS)]**2))
        print(f"{NAMES[i]}  {np.abs(x).max():6.3f}  {20*np.log10(r+1e-9):7.1f}  {centroid(x[:int(.3*FS)]):8.0f}  {t_to(x,-20):6.3f}  {t_to(x,-40):6.3f}  {peakfreq(x[:int(.3*FS)]):6.0f}")
