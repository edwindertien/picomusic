"""Draws docs/pipeline.png, docs/cores.png, docs/voices.png.  Run from the repo root."""
import matplotlib; matplotlib.use('Agg')
import matplotlib.pyplot as plt
from matplotlib.patches import FancyBboxPatch, FancyArrowPatch, Rectangle
plt.rcParams['font.family']='DejaVu Sans'
C=dict(osc='#2ecc40',flt='#19d3da',amp='#ff8c00',fenv='#e83ee8',fx='#f2d600',core0='#3a6ea5',core1='#b5483a',grey='#555a63',bg='#10131a',box='#1b2030',line='#9aa3b5',txt='#eef1f7')
def box(ax,x,y,w,h,label,col=None,fc=None,fs=9,tc=None,lw=1.6,bold=False):
    col=col or C['line']; fc=fc or C['box']
    ax.add_patch(FancyBboxPatch((x,y),w,h,boxstyle="round,pad=0.02,rounding_size=0.12",fc=fc,ec=col,lw=lw))
    ax.text(x+w/2,y+h/2,label,ha='center',va='center',fontsize=fs,color=tc or C['txt'],weight='bold' if bold else 'normal',linespacing=1.25)
def arrow(ax,x0,y0,x1,y1,col=None,style='-|>',lw=1.6,ls='-',cs="arc3,rad=0"):
    ax.add_patch(FancyArrowPatch((x0,y0),(x1,y1),arrowstyle=style,mutation_scale=12,color=col or C['line'],lw=lw,ls=ls,connectionstyle=cs))
def canvas(w,h,title):
    fig,ax=plt.subplots(figsize=(w,h),dpi=150); fig.patch.set_facecolor(C['bg']); ax.set_facecolor(C['bg']); ax.set_xlim(0,w*10); ax.set_ylim(0,h*10); ax.axis('off')
    ax.text(w*5,h*10-2.2,title,ha='center',va='center',fontsize=13,color=C['txt'],weight='bold'); return fig,ax

import matplotlib.patches as mp
def canvas2(w,h,title,xl,yl):
    fig,ax=plt.subplots(figsize=(w,h),dpi=150); fig.patch.set_facecolor(C['bg']); ax.set_facecolor(C['bg']); ax.set_xlim(0,xl); ax.set_ylim(0,yl); ax.axis('off')
    ax.text(xl/2,yl-2.5,title,ha='center',va='center',fontsize=13,color=C['txt'],weight='bold'); return fig,ax
def line(ax,x0,y0,x1,y1,col,lw=1.6,ls='-'): ax.plot([x0,x1],[y0,y1],color=col,lw=lw,ls=ls,solid_capstyle='butt')

# ============================ 1. signal pipeline ===============================
fig,ax=canvas2(14,8.6,'Signal pipeline  (one voice, then the master section)',140,86)
ax.add_patch(FancyBboxPatch((3,28),96,48,boxstyle="round,pad=0.02,rounding_size=0.6",fc='#141a28',ec='#2a3350',lw=1.2))
ax.text(5.5,73,'VOICE   ×20 in the pool, rendered on core 1',fontsize=9,color=C['line'],va='center')
box(ax,7,52,15,10,'OSC 1\nwavetable\n4096 points',C['osc'],fs=8.5); box(ax,7,38,15,10,'OSC 2\ndetune ±50 cents',C['osc'],fs=8.5)
box(ax,30,44,12,10,'MIX\nosc 1 ↔ osc 2',C['osc'],fs=8.5); box(ax,30,31,12,7,'noise',C['osc'],fs=8.5)
box(ax,50,44,22,10,'4-pole ladder filter\n24 dB/oct, resonance\nfeedback (Huovilainen)',C['flt'],fs=8)
box(ax,80,44,12,10,'× AMP\nlinear ramp',C['amp'],fs=8.5)
arrow(ax,22,57,30,51); arrow(ax,22,43,30,47); arrow(ax,36,38,36,44,C['osc']); arrow(ax,42,49,50,49); arrow(ax,72,49,80,49)
ax.text(46,51.2,'×1.2',fontsize=7.5,color=C['line'],ha='center')
box(ax,50,60,22,8,'FILTER ADSR\ncutoff = 30·2^(cc/18) + env·amount·12 kHz',C['fenv'],fs=7.2); arrow(ax,61,60,61,54,C['fenv'])
box(ax,76,60,20,8,'AMP ADSR\n(ramped between ticks)',C['amp'],fs=7.5); arrow(ax,86,60,86,54,C['amp'])
box(ax,7,12,42,11,'PITCH  =  note  ×  bend  ×  vibrato(LFO)  ×  glide\none ratio per 16 samples, applied to both oscillators',C['fx'],fs=8)
line(ax,5,23,5,57,C['fx'],ls='--'); arrow(ax,5,57,7,57,C['fx'],ls='--'); line(ax,5,43,7,43,C['fx'],ls='--'); arrow(ax,5,43,7,43,C['fx'],ls='--'); line(ax,5,17.5,7,17.5,C['fx'],ls='--')
box(ax,58,12,34,11,'pitch bend  ±2 semitones\nmod depth × LFO  (0.1–20 Hz, sine | sample & hold)',C['fx'],fs=7.1); arrow(ax,58,17.5,49,17.5,C['fx'])
box(ax,106,52,14,9,'Σ voices\nint32',C['line'],fs=9); arrow(ax,92,49,106,55.5)
box(ax,104,38,18,9,'OVERDRIVE\nasymmetric tube\n(effects.h)',C['fx'],fs=8.3); arrow(ax,113,52,113,47)
box(ax,104,24,18,9,'MASTER VOLUME\nthen clip to int16',C['line'],fs=8.3); arrow(ax,113,38,113,33)
box(ax,100,8,26,11,'PIO2 I²S  →  DAC\nGP22 DIN · GP26 BCK · GP27 LRCK\n44.1 kHz, 16 bit',C['core1'],fs=7.8); arrow(ax,113,24,113,19)
ax.text(3,3,'Control rate: envelopes, filter coefficient, pitch ratio and LFO update every 16 samples (2.76 kHz); the voice interpolates linearly in between.',fontsize=8,color=C['line'])
fig.savefig('docs/pipeline.png',facecolor=C['bg'],bbox_inches='tight',pad_inches=0.15); plt.close(fig)

# ============================ 2. cores / ownership ============================
fig,ax=canvas2(13,8.4,'Two cores, no locks:  who owns what',130,84)
ax.add_patch(Rectangle((2,4),38,72,fc='#14202f',ec=C['core0'],lw=1.6)); ax.text(21,72.3,'CORE 0   loop()',color=C['core0'],ha='center',fontsize=11,weight='bold')
ax.add_patch(Rectangle((90,4),38,72,fc='#2a1716',ec=C['core1'],lw=1.6)); ax.text(109,72.3,'CORE 1   loop1()  (SRAM)',color=C['core1'],ha='center',fontsize=11,weight='bold')
for i,t in enumerate(['midi_handler_poll()   USB device + UART','usb_host_poll()   PIO-USB host','ui_handle_buttons()   joystick, A, B','ui_flush()   at most 1 LCD rectangle','serial commands + event log']):
    box(ax,4,60-i*7,34,5.2,t,C['core0'],fs=7.6)
box(ax,4,16,34,12,'SynthManager  (core-0 API)\nnoteOn · noteOff · setParam\npitchBend · allocation · stealing',C['core0'],fs=8,bold=True)
box(ax,4,6,34,7,'owns: pv[]  ep.*  note map\nheld / dying / age bookkeeping',C['core0'],fs=7.6)
box(ax,92,60,34,5.2,'256 samples:  tick()  →  put()  (FIFO paces)',C['core1'],fs=7.6,bold=True)
box(ax,92,50,34,7.2,'every 16 samples (global):\nLFO + pitch bend  →  modRatio',C['core1'],fs=7.6)
box(ax,92,37,34,10,'for each LIVE voice:\nrender()  per sample\ncontrol() every 16: envelopes,\nfilter g, pitch, tables, requests',C['core1'],fs=7.4)
box(ax,92,28,34,6,'Σ → overdrive → volume → clip',C['core1'],fs=7.6)
box(ax,92,19,34,6,'DWT cycles → CPU %  (g_cpu_load_pct)',C['core1'],fs=7.6)
box(ax,92,6,34,10,'owns while live = 1:\noscillator phase, filter state,\nenvelopes, amp ramp',C['core1'],fs=7.6)
def bus(y,txt,col,rev=False,ls='-'):
    if rev: arrow(ax,89.5,y,40.5,y,col,lw=1.8,ls=ls)
    else:   arrow(ax,40.5,y,89.5,y,col,lw=1.8,ls=ls)
    ax.text(65,y+1.6,txt,ha='center',fontsize=7.8,color=col)
bus(62,'ep.*  parameter words  (one float / byte each)','#9ad0ff',ls='--')
bus(53,'init the voice while live = 0  →  __dmb()  →  live = 1','#7CFC9A')
bus(44,'relReq++ / fastReq++   (key up / steal fade)','#ffd166')
bus(36,'tgtInc   (glide retarget)','#ffd166')
bus(28,'level   (read only: used to pick the quietest victim)','#b9c0cf',rev=True,ls='--')
bus(20,'live = 0   (core 1\'s LAST access: voice handed back)','#ff9a8a',rev=True)
ax.text(65,1.2,'Core 0 may write a voice only while live = 0, then publishes it with a barrier. While live = 1 it can only post request counters.',ha='center',fontsize=8,color=C['line'])
fig.savefig('docs/cores.png',facecolor=C['bg'],bbox_inches='tight',pad_inches=0.15); plt.close(fig)

# ============================ 3. voice pool ===================================
fig,ax=canvas2(13,8.6,'Voice pool: "the new note always wins"',130,86)
cols={'held':'#2ecc40','rel':'#f2d600','fade':'#e8463a','free':'#2a2f3b','new':'#19d3da'}
W=5.0
def pool(y,cells,title,count):
    ax.text(3,y+11.5,title,fontsize=9.5,color=C['txt'],weight='bold')
    for i,(k,lab,sub) in enumerate(cells):
        ax.add_patch(Rectangle((3+i*W,y),W-0.5,8,fc=cols[k],ec='#0b0e14',lw=1))
        ax.text(3+i*W+(W-0.5)/2,y+5,lab,ha='center',va='center',fontsize=7,color='#06070a' if k!='free' else '#6a7080',weight='bold')
        if sub: ax.text(3+i*W+(W-0.5)/2,y+2,sub,ha='center',va='center',fontsize=5.2,color='#06070a')
    ax.text(3+20*W+2,y+4,count,fontsize=8.5,color=C['txt'],va='center',linespacing=1.4)
H=lambda n:('held',str(n),'')
R=lambda n,l:('rel',str(n),'rel '+l)
F=('free','','')
row1=[H(60),H(64),H(67),R(55,'0.4'),H(72),H(59),R(62,'0.1'),H(65),H(69),R(71,'0.7'),H(74),H(76)]+[F]*8
pool(62,row1,'1.   12 voices sounding (cap) — 9 keys down, 3 releasing — pool of 20 slots','counted 12 / 12')
row2=row1[:6]+[('fade','62','fading')]+row1[7:12]+[('new','78','new')]+[F]*7
pool(40,row2,'2.   note 78 arrives:  the QUIETEST releasing voice (62) is faded out in 4 ms; 78 starts at once in a free slot','counted 12 / 12\n(+1 fading)')
row3=row1[:6]+[F]+row1[7:12]+[H(78)]+[F]*7
pool(18,row3,'3.   4 ms later:  slot 62 is silent and back in the pool','counted 12 / 12')
for i,(k,t) in enumerate([('held','held (key down)'),('rel','releasing (key up)'),('fade','fading (stolen)'),('new','new note'),('free','free slot')]):
    x=3+i*24; ax.add_patch(Rectangle((x,6),3,2.6,fc=cols[k],ec='#555',lw=0.5)); ax.text(x+4.2,7.3,t,fontsize=7.6,color=C['line'],va='center')
ax.text(3,1.6,'If no voice is releasing, the OLDEST held voice is the victim.  If every slot is still occupied by fading voices (burst of 9+ steals) core 0 waits ≤ 8 ms for one.',fontsize=7.8,color=C['line'])
fig.savefig('docs/voices.png',facecolor=C['bg'],bbox_inches='tight',pad_inches=0.15); plt.close(fig)
print('ok')
