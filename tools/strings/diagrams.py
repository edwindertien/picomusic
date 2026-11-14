"""Draws docs/pipeline.png, docs/cores.png, docs/voices.png.  Run from the repo root."""
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
fig,ax=canvas2(14,8.8,'Signal pipeline  (voices -> shared registration filters -> ensemble)',140,88)
ax.add_patch(FancyBboxPatch((3,40),44,40,boxstyle="round,pad=0.02,rounding_size=0.6",fc='#141a28',ec='#2a3350',lw=1.2))
ax.text(5,77.5,'VOICE   x24 in the pool',fontsize=9,color=C['line'],va='center')
box(ax,6,62,18,9,'one phase\naccumulator',C['osc'],fs=8.5)
box(ax,28,66,17,7,"saw 16'  (phase)",C['osc'],fs=8); box(ax,28,57.5,17,7,"saw 8'  (phase << 1)",C['osc'],fs=8); box(ax,28,49,17,7,"saw 4'  (phase << 2)",C['osc'],fs=8)
arrow(ax,24,66.5,28,69.5); arrow(ax,24,66.5,28,61); arrow(ax,24,66.5,28,52.5)
box(ax,6,49,18,8,'2nd 8\' saw, detuned\n(unison)',C['osc'],fs=7.5); arrow(ax,24,53,28,60.5,ls='--')
box(ax,6,41,39,6,"saws x note envelope (attack / hold / release, squared)",C['amp'],fs=7.2)
box(ax,52,66,16,7,"BUS 16'",C['line'],fs=8.5); box(ax,52,57.5,16,7,"BUS 8'",C['line'],fs=8.5); box(ax,52,49,16,7,"BUS 4'",C['line'],fs=8.5); box(ax,52,40.5,16,7,"BUS pizz",C['line'],fs=8.5)
arrow(ax,45,69.5,52,69.5); arrow(ax,45,61,52,61); arrow(ax,45,52.5,52,52.5)
box(ax,6,28,39,8,"PIZZICATO:  (8' + 4') x exponential decay x key gate\n(decay time = PDEC)",C['fenv'],fs=7.6); arrow(ax,45,32,59,40.5,C['fenv'])
box(ax,76,66,22,7,"BASS    2-pole low-pass 260 Hz",C['flt'],fs=7.6); box(ax,76,57.5,22,7,"CELLO   low-pass 720 Hz, Q 1",C['flt'],fs=7.6)
box(ax,76,49,22,7,"VIOLA   band-pass 1.1 kHz",C['flt'],fs=7.6); box(ax,76,40.5,22,7,"VIOLIN  band-pass 2.4 kHz",C['flt'],fs=7.6)
box(ax,76,32,22,7,"PIZZ    band-pass 1.6 kHz",C['flt'],fs=7.6)
arrow(ax,68,69.5,76,69.5); arrow(ax,68,61,76,61); arrow(ax,68,61,76,52.5,ls='--'); arrow(ax,68,52.5,76,44); arrow(ax,60,40.5,76,35.5)
for y in (69.5,61,52.5,44,35.5): arrow(ax,98,y,104,y,C['line'])
box(ax,104,50,16,22,'x  registration\nlevels\n(BASS CELL VLA\nVLN PIZZ)\n\nΣ',C['amp'],fs=8)
box(ax,104,36,16,9,'TONE\nlow-pass',C['fx'],fs=8.5); arrow(ax,112,50,112,45)
box(ax,98,20,28,11,"ENSEMBLE  (3 taps, 120° apart)\nslow LFO + fast LFO\nmono in -> stereo out",C['fx'],fs=7.8); arrow(ax,112,36,112,31)
box(ax,98,6,28,9,'VOLUME  ->  soft limiter\n-> I²S, 44.1 kHz stereo',C['core1'],fs=7.8); arrow(ax,112,20,112,15)
box(ax,52,6,40,10,'PITCH:  note  x  bend  x  vibrato(LFO)\nvibrato depth VIB, rate VRT; bend +-2 st',C['fx'],fs=7.6)
line(ax,52,11,4.4,11,C['fx'],ls='--'); line(ax,4.4,11,4.4,66.5,C['fx'],ls='--'); arrow(ax,4.4,66.5,6,66.5,C['fx']); ax.text(2.6,38,'pitch to every voice',color=C['fx'],fontsize=7,rotation=90,va='center')
ax.text(3,2,'Only the voices differ per note; the filters and the ensemble are shared, so CPU cost hardly grows with the number of notes.',fontsize=8,color=C['line'])
fig.savefig('docs/pipeline.png',facecolor=C['bg'],bbox_inches='tight',pad_inches=0.15); plt.close(fig)

# ============================ 2. cores / ownership ============================
fig,ax=canvas2(13,8.4,'Two cores, no locks:  who owns what',130,84)
ax.add_patch(Rectangle((2,4),38,72,fc='#14202f',ec=C['core0'],lw=1.6)); ax.text(21,72.3,'CORE 0   loop()',color=C['core0'],ha='center',fontsize=11,weight='bold')
ax.add_patch(Rectangle((90,4),38,72,fc='#2a1716',ec=C['core1'],lw=1.6)); ax.text(109,72.3,'CORE 1   loop1()  (SRAM)',color=C['core1'],ha='center',fontsize=11,weight='bold')
for i,t in enumerate(['midi_handler_poll()   USB device + UART','usb_host_poll()   PIO-USB host','ui_handle_buttons()   joystick, A, B','ui_flush()   at most 1 LCD rectangle','serial commands + event log']):
    box(ax,4,60-i*7,34,5.2,t,C['core0'],fs=7.6)
box(ax,4,16,34,12,'StringsManager  (core-0 API)\nnoteOn · noteOff · setParam · pedal\npitchBend · allocation · stealing',C['core0'],fs=8,bold=True)
box(ax,4,6,34,7,'owns: pv[]  ep.*  note map\nheld / dying / age bookkeeping',C['core0'],fs=7.6)
box(ax,92,60,34,5.2,'256 samples:  tick()  →  put()  (FIFO paces)',C['core1'],fs=7.6,bold=True)
box(ax,92,50,34,7.2,'every 16 samples (global):\nLFO + pitch bend  →  modRatio',C['core1'],fs=7.6)
box(ax,92,37,34,10,'for each LIVE voice:\nrender()  per sample: saws -> buses\ncontrol() every 16: envelope,\npitch, pizz decay, requests',C['core1'],fs=7.4)
box(ax,92,28,34,6,'filters → tone → ensemble → limiter',C['core1'],fs=7.6)
box(ax,92,19,34,6,'DWT cycles → CPU %  (g_cpu_load_pct)',C['core1'],fs=7.6)
box(ax,92,6,34,10,'owns while live = 1:\nsaw phases, note envelope,\npizz decay, amp ramps',C['core1'],fs=7.6)
def bus(y,txt,col,rev=False,ls='-'):
    if rev: arrow(ax,89.5,y,40.5,y,col,lw=1.8,ls=ls)
    else:   arrow(ax,40.5,y,89.5,y,col,lw=1.8,ls=ls)
    ax.text(65,y+1.6,txt,ha='center',fontsize=7.8,color=col)
bus(62,'ep.*  parameter words  (one float / byte each)','#9ad0ff',ls='--')
bus(53,'init the voice while live = 0  →  __dmb()  →  live = 1','#7CFC9A')
bus(44,'relReq++ / fastReq++   (key up / steal fade)','#ffd166')
bus(34,'level   (read only: used to pick the quietest victim)','#b9c0cf',rev=True,ls='--')
bus(24,'live = 0   (core 1\'s LAST access: voice handed back)','#ff9a8a',rev=True)
ax.text(65,1.2,'Core 0 may write a voice only while live = 0, then publishes it with a barrier. While live = 1 it can only post request counters.',ha='center',fontsize=8,color=C['line'])
fig.savefig('docs/cores.png',facecolor=C['bg'],bbox_inches='tight',pad_inches=0.15); plt.close(fig)


# ============================ 3. voice pool with the pedal ======================
fig,ax=canvas2(13,8.6,'Voice pool: "the new note always wins" (with the sustain pedal)',130,86)
cols={'held':'#2ecc40','sust':'#8f7bff','rel':'#f2d600','fade':'#e8463a','free':'#2a2f3b','new':'#19d3da'}
W=5.0
def pool(y,cells,title,count):
    ax.text(3,y+11.5,title,fontsize=9.3,color=C['txt'],weight='bold')
    for i,(k,lab) in enumerate(cells):
        ax.add_patch(Rectangle((3+i*W,y),W-0.4,8,fc=cols[k],ec='#0b0e14',lw=0.8)); ax.text(3+i*W+(W-0.4)/2,y+4,lab,ha='center',va='center',fontsize=5.6,color='#06070a' if k!='free' else '#6a7080',weight='bold')
    ax.text(3,y-3.2,count,fontsize=8.3,color=C['line'],va='center')
H=lambda n:('held',str(n)); S=lambda n:('sust',str(n)); R=lambda n:('rel',str(n)); F=('free','')
row1=[H(60),H(64),H(67),H(72),H(55),H(59)]+[S(40),S(43),S(47),S(50),S(52),S(55)]+[R(36),R(38),R(41),R(45)]+[F]*8
pool(62,row1,'1.   pedal down: 6 keys held, 6 voices sustained by the pedal, 4 release tails = 16 (the cap)  |  pool of 24 slots','counted 16 / 16')
row2=row1[:12]+[R(36),('fade','38'),R(41),R(45)]+[('new','76')]+[F]*7
pool(36,row2,'2.   note 76 arrives: the QUIETEST key-up voice (release tail 38) fades out in 6 ms; 76 starts at once in a free slot','counted 16 / 16  (+1 fading)')
row3=row1[:12]+[R(36),F,R(41),R(45)]+[H(76)]+[F]*7
pool(10,row3,'3.   6 ms later: the slot is back in the pool','counted 16 / 16')
for i,(k,t) in enumerate([('held','held (key down)'),('sust','sustained (key up, pedal down)'),('rel','release tail'),('fade','fading (stolen)'),('new','new note')]):
    x=3+i*24; ax.add_patch(Rectangle((x,-2.5 if False else 1.2),3,2.4,fc=cols[k],ec='#555',lw=0.5)); ax.text(x+4.2,2.4,t,fontsize=7.2,color=C['line'],va='center')
ax.text(98,76,'victim priority:\n1  quietest key-up voice\n    (tails, then sustained)\n2  oldest held key',fontsize=8,color=C['line'],va='top',ha='right',linespacing=1.4,bbox=dict(fc=C['box'],ec=C['line'],boxstyle='round,pad=0.4'))
fig.savefig('docs/voices.png',facecolor=C['bg'],bbox_inches='tight',pad_inches=0.15); plt.close(fig)
print('diagrams ok')
