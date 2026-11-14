"""Generate the README tables (CC map, AKAI knobs, patches) from src/config.h + src/strings_params.h.
Usage: python3 tools/gen_tables.py [src_dir] [out_dir]"""
import re, json, sys, os
S = (sys.argv[1] if len(sys.argv) > 1 else '../src').rstrip('/') + '/'
OUT = sys.argv[2] if len(sys.argv) > 2 else '.'; os.makedirs(OUT, exist_ok=True)
cfg = open(S + 'config.h').read(); par = open(S + 'strings_params.h').read()
defs = dict(re.findall(r'#define\s+(\w+)\s+([-\w.]+)', cfg))
rows = re.findall(r'\{\s*"([^"]+)",\s*"([^"]+)",\s*"([^"]+)",\s*(\w+),\s*(G_\w+),\s*(\d+),\s*(\d+),\s*(F_\w+)\s*\}', par)
ids = [x for x in re.findall(r'\bP_[A-Z0-9_]+\b', re.search(r'enum ParamId : uint8_t \{(.*?)\};', par, re.S).group(1)) if x != 'N_PARAMS']
assert len(rows) == len(ids), (len(rows), len(ids))
P = [dict(id=ids[i], label=r[0], name=r[1], cmd=r[2], cc=int(defs[r[3]]), group=r[4], col=int(r[5]), row=int(r[6]), fmt=r[7]) for i, r in enumerate(rows)]
pm = re.search(r'PATCHES\[N_PATCHES\]\[N_PARAMS\] = \{(.*?)\n\};', par, re.S).group(1)
patch_rows = [[int(v) for v in re.findall(r'\d+', m.split('//')[0])] for m in re.findall(r'\{([^{}]*)\}', pm)]
names = re.findall(r'"(\w+)"', re.search(r'PATCH_NAMES\[N_PATCHES\] = \{(.*?)\}', par).group(1))
assert all(len(r) == len(P) for r in patch_rows) and len(patch_rows) == len(names)
akai = [(int(defs[k]), p) for k, p in re.findall(r'case (AKAI_K\d): return (P_\w+);', par)]
json.dump(dict(P=P, patches=patch_rows, names=names, akai=akai), open(os.path.join(OUT, 'params.json'), 'w'), indent=1)
GRP = {'G_REG': 'Registrations', 'G_ENV': 'Envelope', 'G_MOD': 'Vibrato', 'G_ENS': 'Ensemble', 'G_OUT': 'Tone / output'}
RNG = {
 'P_BASS': "16' saws → 2-pole low-pass, 260 Hz", 'P_CELLO': "8' saws → 2-pole low-pass, 720 Hz, Q 1", 'P_VIOLA': "8' saws → band-pass, 1.1 kHz",
 'P_VIOLIN': "4' saws → band-pass, 2.4 kHz", 'P_PIZZ': "8' + 4' with its own decay → band-pass, 1.6 kHz",
 'P_ATTACK': '4 ms … 2.4 s (exponential)', 'P_RELEASE': '20 ms … 6 s (exponential)', 'P_DETUNE': 'second 8\' saw per note, detuned 0 … 18 cents (0 = off)',
 'P_VIB_DEPTH': 'vibrato depth 0 … ±' + defs['VIB_MAX_SEMITONES'].rstrip('f') + ' semitone', 'P_VIB_RATE': '0.5 … 8 Hz', 'P_PIZ_DECAY': 'pizzicato decay time constant 50 ms … 1 s',
 'P_TONE': 'low-pass on the sum, 600 Hz … 19 kHz', 'P_ENS_DEPTH': 'sweep of the three delay taps (0 = static)', 'P_ENS_RATE': 'slow LFO 0.15 … 1.2 Hz',
 'P_ENS_MIX': 'dry … wet', 'P_ENS_SHIMMER': 'amount of the fast (6.2 Hz) LFO', 'P_VOL': 'master volume (squared law)'}
o = ['| Parameter | Serial name | Knob | **MIDI CC** | Range / meaning |\n|---|---|---|---|---|']
for g in ['G_REG', 'G_ENV', 'G_MOD', 'G_ENS', 'G_OUT']:
    o.append(f'| **{GRP[g]}** | | | | |')
    for p in P:
        if p['group'] == g: o.append(f"| {p['name']} | `{p['cmd']}` | {p['label']} | {p['cc']} | {RNG[p['id']]} |")
open(os.path.join(OUT, 't_cc.md'), 'w').write('\n'.join(o) + '\n')
pn = {p['id']: p for p in P}
o = ['| Knob | CC | Controls |\n|---|---|---|'] + [f"| K{cc} | {cc} | {pn[pid]['name']} |" for cc, pid in akai]
open(os.path.join(OUT, 't_akai.md'), 'w').write('\n'.join(o) + '\n')
o = ['| Parameter | ' + ' | '.join(names) + ' |\n|---|' + '---|' * len(names)] + [f"| {p['name']} | " + ' | '.join(str(r[i]) for r in patch_rows) + ' |' for i, p in enumerate(P)]
open(os.path.join(OUT, 't_patches.md'), 'w').write('\n'.join(o) + '\n')
