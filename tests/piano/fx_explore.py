import sys; sys.path.insert(0,'.')
import numpy as np, dsp_common as d
FS=44100
def fx(sets, cmd):
    d.probe("".join(f"set {k} {v}\n" for k,v in sets.items()) + cmd + "write /tmp/fx.raw\n"); return np.fromfile('/tmp/fx.raw',dtype=np.int16).astype(float).reshape(-1,2)/32768
def gain(sets, hz, n=22050):
    x=fx(sets, f"fxsine {hz} 0.1 {n}\n"); return np.sqrt((x[n//2:,0]**2).mean())/(0.1/np.sqrt(2))
d.build()
print("WAH (mix 127): gain vs frequency, peak position per WPOS")
fr=np.geomspace(150,5000,36)
for pos in (0,64,127):
    g=[gain(dict(wah=127,wpos=pos),f) for f in fr]; i=int(np.argmax(g)); exp=350*2**(pos/127*3.0)
    print(f"  wpos {pos:3d}: peak {fr[i]:7.0f} Hz (expected {exp:6.0f}), peak gain {g[i]:.2f}, gain at 100 Hz {gain(dict(wah=127,wpos=pos),100):.3f}")
print("WAH mix 64 at wpos 64, 1 kHz:", round(gain(dict(wah=64,wpos=64),1000),3), "   wah 0 dry:", round(gain(dict(wah=0),1000),4))
print("TREMOLO (trem 0 = full depth, trate 64):")
x=fx(dict(trem=0,trate=64),"fxsine 1000 0.1 88200\n"); e=np.abs(x[:,0]).reshape(-1,441).max(1); print(f"  envelope min/max {e.min()/e.max():.3f}; rate: ", end="")
spec=np.abs(np.fft.rfft(e-e.mean())); f=np.fft.rfftfreq(len(e),441/FS); print(f"{f[np.argmax(spec)]:.2f} Hz (expected {0.5*2**(64*4.3/127):.2f})")
x=fx(dict(trem=127,trate=64),"fxsine 1000 0.1 88200\n"); eL=np.abs(x[:,0]).reshape(-1,441).max(1); eR=np.abs(x[:,1]).reshape(-1,441).max(1)
print(f"AUTO-PAN: L/R envelope correlation {np.corrcoef(eL,eR)[0,1]:.3f}; L min/max {eL.min()/eL.max():.3f}")
print("CHORUS (chor 127), 1 kHz sine: envelope min/max", end=" ")
x=fx(dict(chor=127),"fxsine 1000 0.1 132300\n"); e=np.abs(x[:,0]).reshape(-1,441).max(1)[10:]; print(f"{e.min()/e.max():.3f}")
print("REVERB impulse response, RT60 (time for the Schroeder-integrated energy to fall 60 dB; extrapolated from -5..-35 dB):")
for rs in (0,32,64,96,127):
    x=fx(dict(rev=127,rsize=rs),"fximp 441000\n"); ir=x[:,0].copy(); ir[0]=0; e=np.cumsum((ir**2)[::-1])[::-1]; edc=10*np.log10(e/e[0]+1e-30)
    t5=np.argmax(edc<-5)/FS; t35=np.argmax(edc<-35)/FS; rt=(t35-t5)*2 if t35>t5 else float('nan')
    print(f"  rsize {rs:3d}: RT60 ~ {rt:5.2f} s   wet peak {np.abs(ir).max():.3f}   L/R correlation {np.corrcoef(x[:,0][1:],x[:,1][1:])[0,1]:+.2f}")
