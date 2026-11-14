import sys; sys.path.insert(0,'.')
import numpy as np, dsp_common as d
FS=44100
def render(script, n=FS): d.probe(script + f"run {n}\nwrite /tmp/b.raw\n"); return d.read_stereo('/tmp/b.raw')[:,0] if False else np.fromfile('/tmp/b.raw', dtype=np.int16).astype(float)/32768
def peakfreq(x, lo=20, hi=6000, n=1<<17):
    s=np.abs(np.fft.rfft(x*np.hanning(len(x)), n)); f=np.fft.rfftfreq(n,1/FS); m=(f>lo)&(f<hi); return f[m][np.argmax(s[m])]
def centroid(x):
    s=np.abs(np.fft.rfft(x*np.hanning(len(x))))**2; f=np.fft.rfftfreq(len(x),1/FS); return (s*f).sum()/s.sum()
if __name__=="__main__":
    d.build()
    print("patch        note  peak    rms(dBFS)  centroid  f0(peak)")
    for p,name in enumerate(["Moog Bass","Sub","Acid","Pluck"]):
        for note in (36,48):
            x=render(f"patch {p}\non {note} 100\n", FS)[int(.3*FS):]
            print(f"{name:11s} {note:4d}  {np.abs(x).max():5.3f}  {20*np.log10(np.sqrt((x**2).mean())+1e-9):8.1f}  {centroid(x):8.0f}  {peakfreq(x,30,400):7.1f}")
