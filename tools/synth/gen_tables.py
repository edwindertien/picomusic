"""Generate the README tables (CC map, AKAI knobs, patches) from src/config.h + src/synth_params.h.
Usage: python3 tools/gen_tables.py [src_dir] [out_dir]"""
import re, math, json
import sys, os
S=(sys.argv[1] if len(sys.argv)>1 else '../src').rstrip('/')+'/'
OUT=sys.argv[2] if len(sys.argv)>2 else '.'
os.makedirs(OUT,exist_ok=True)
cfg=open(S+'config.h').read(); par=open(S+'synth_params.h').read()
defs=dict(re.findall(r'#define\s+(\w+)\s+([-\w.]+)',cfg))
def val(x):
    x=x.strip()
    return int(defs[x]) if x in defs else int(x)
# --- PARAM_INFO -----------------------------------------------------------
rows=re.findall(r'\{\s*"([^"]+)",\s*"([^"]+)",\s*"([^"]+)",\s*(\w+),\s*(G_\w+),\s*(0x[0-9A-Fa-f]+|\d+),\s*(0x[0-9A-Fa-f]+|\d+)\s*\}',par)
enum=re.search(r'enum ParamId : uint8_t \{(.*?)\};',par,re.S).group(1)
ids=[x for x in re.findall(r'\bP_[A-Z0-9]+\b',enum) if x!='N_PARAMS']
assert len(rows)==len(ids)==21,(len(rows),len(ids))
P=[dict(id=ids[i],label=r[0],name=r[1],cmd=r[2],cc=val(r[3]),group=r[4],col=int(r[5],0),row=int(r[6],0)) for i,r in enumerate(rows)]
# --- patches --------------------------------------------------------------
pm=re.search(r'PATCHES\[N_PATCHES\]\[N_PARAMS\] = \{(.*?)\n\};',par,re.S).group(1)
patch_rows=[[int(v) for v in re.findall(r'\d+',m.split('//')[0])] for m in re.findall(r'\{([^{}]*)\}',pm)]
patch_names=re.findall(r'PATCH_NAMES\[N_PATCHES\] = \{(.*?)\}',par)[0]; patch_names=re.findall(r'"(\w+)"',patch_names)
assert all(len(r)==21 for r in patch_rows) and len(patch_rows)==3
# --- AKAI -----------------------------------------------------------------
akai=re.findall(r'case (AKAI_K\d): return (P_\w+);',par)
akai=[(int(defs[k]),p) for k,p in akai]
json.dump(dict(P=P,patches=patch_rows,names=patch_names,akai=akai),open(os.path.join(OUT,'params.json'),'w'),indent=1)

# ============== markdown ====================================================
grp={'G_OSC':'Oscillators','G_FLT':'Filter','G_AENV':'Amp envelope','G_FENV':'Filter envelope','G_FX':'Master / FX / mod'}
rng={ # human description of the 0..127 range, formulas taken from SynthManager::_derive
 'P_WAVE':'sine / triangle / saw / square (0-31 / 32-63 / 64-95 / 96-127)',
 'P_DETUNE':'osc 2 detune, 64 = 0, ±50 cents',
 'P_OSCMIX':'0 = osc 1 only … 127 = osc 2 only',
 'P_NOISE':'white noise, 0 … 40 % of full scale',
 'P_CUTOFF':'base cutoff, 30 Hz × 2^(v/18)  →  30 Hz … ~4 kHz (nominal)',
 'P_RES':'resonance 0 … 0.95 (self-oscillation near the top)',
 'P_FENV':'how far the filter envelope opens the filter, 0 … +12 kHz (nominal)',
 'P_AATK':'3 ms … 3 s (exponential)','P_ADEC':'3 ms … 3 s (exponential)','P_ASUS':'sustain level 0 … 100 %','P_AREL':'3 ms … 3 s (exponential)',
 'P_FATK':'3 ms … 3 s (exponential)','P_FDEC':'3 ms … 3 s (exponential)','P_FSUS':'sustain level 0 … 100 %','P_FREL':'3 ms … 3 s (exponential)',
 'P_VOL':'master volume (applied after the overdrive)',
 'P_DRIVE':'0 = bypass, 1…127 = input gain ×1 … ×5 (asymmetric tube shaper)',
 'P_LFORATE':'0.1 Hz … 20 Hz (exponential)',
 'P_MOD':'vibrato depth, 0 … ±%.0f semitones'%float(re.search(r'MOD_MAX_SEMITONES\s+([\d.]+)f',cfg).group(1)),
 'P_GLIDE':'glide time, 5 ms … 3 s',
 'P_LFOWAVE':'< 64 sine, ≥ 64 sample & hold',
}
out=[]
out.append('| Parameter | Serial name | Knob label | **MIDI CC** | Range / meaning |\n|---|---|---|---|---|')
for g in ['G_OSC','G_FLT','G_AENV','G_FENV','G_FX']:
    out.append(f'| **{grp[g]}** | | | | |')
    for p in P:
        if p['group']!=g: continue
        lab=p['label'] if p['col']!=255 else '*(no knob)*'
        out.append(f"| {p['name']} | `{p['cmd']}` | {lab} | {p['cc']} | {rng[p['id']]} |")
open(os.path.join(OUT,'t_cc.md'),'w').write('\n'.join(out)+'\n')
# AKAI
pn={p['id']:p for p in P}
o=['| Knob | CC | Controls |\n|---|---|---|']
for cc,pid in akai: o.append(f"| K{cc} | {cc} | {pn[pid]['name']} |")
open(os.path.join(OUT,'t_akai.md'),'w').write('\n'.join(o)+'\n')
# patches
labs=[p['label'] if p['id'] not in('P_AATK','P_ADEC','P_ASUS','P_AREL','P_FATK','P_FDEC','P_FSUS','P_FREL') else ('A.' if p['group']=='G_AENV' else 'F.')+p['label'] for p in P]
o=['| Parameter | '+' | '.join(patch_names)+' |\n|---|'+'---|'*len(patch_names)]
for i,p in enumerate(P):
    v=[r[i] for r in patch_rows]
    if p['id']=='P_WAVE': v=['%s'%['sin','tri','saw','sqr'][x>>5] for x in v]
    o.append(f"| {p['name']} | "+' | '.join(str(x) for x in v)+' |')
open(os.path.join(OUT,'t_patches.md'),'w').write('\n'.join(o)+'\n')
# special CCs
print('special CCs: glide on/off',defs['CC_GLIDE_ONOFF'],' all sound off',defs['CC_ALL_SOUND_OFF'],' all notes off',defs['CC_ALL_NOTES_OFF'])
# --- numeric facts from the firmware formulas ------------------------------
t=lambda cc:0.003*1000**(cc/127)
print('env time cc0/32/64/96/127: ',[round(t(c)*1000,1) for c in (0,32,64,96,127)],'ms')
print('cutoff nominal cc0/60/90/127:',[round(30*2**(c/18)) for c in (0,60,90,127)],'Hz')
print('lfo hz 0/64/127:',[round(0.1*200**(c/127),2) for c in (0,64,127)])
print('glide T 0/20/127:',[round(0.005+c/127*3,3) for c in (0,20,127)])
print('steal fade ms:',defs['STEAL_RELEASE_MS'],' pool',defs['SYNTH_POOL'],' cap',defs['MAX_ACTIVE_VOICES'],' control',defs['CONTROL_DIV'], 'voice scale',defs['VOICE_SCALE'])
print('master vol default patch: ',patch_rows[0][15],'->',patch_rows[0][15]*255//127,'/255')
