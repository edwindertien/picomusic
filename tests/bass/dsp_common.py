import subprocess, os, numpy as np
HERE = os.path.dirname(os.path.abspath(__file__)); FS = 44100
def build():
    r = subprocess.run(['bash', os.path.join(HERE, 'stage.sh'), os.path.join(HERE, '..', 'src')], capture_output=True, text=True); assert r.returncode == 0, r.stderr
    r = subprocess.run(['g++', '-std=gnu++17', '-O2', '-Wall', '-Wextra', '-Wno-unused-function', '-Wno-unused-parameter', '-fsanitize=address,undefined,float-cast-overflow', '-fno-sanitize-recover=float-cast-overflow',
                        '-I', os.path.join(HERE, 'mock'), '-I', os.path.join(HERE, 'stage'), os.path.join(HERE, 'probe.cpp'), os.path.join(HERE, 'stage/bass_tables.cpp'),
                        os.path.join(HERE, 'mock/mock_globals.cpp'), os.path.join(HERE, 'mock/mock_globals2.cpp'), '-o', os.path.join(HERE, 'build_probe')], capture_output=True, text=True)
    if r.returncode or 'warning' in r.stderr: print(r.stderr[:2000])
    assert r.returncode == 0
def probe(script):
    r = subprocess.run([os.path.join(HERE, 'build_probe')], input=script, capture_output=True, text=True)
    assert r.returncode == 0, r.stderr[-1500:]
    return r.stdout
def read_stereo(path): return np.fromfile(path, dtype=np.int16).reshape(-1, 2).astype(float)
def sustained_rms(setup, note, skip=44100, run=22050, tmp='/tmp/p.raw', ch=0):
    probe(setup + f"on {note}\nskip {skip}\nrun {run}\nwrite {tmp}\n")
    d = read_stereo(tmp); return float(np.sqrt(np.mean(d[:, ch] ** 2)))
