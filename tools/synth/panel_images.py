"""Builds docs/panel.png and docs/panel_cc.png from the emulator render (host_test/panel_clean.rgb).  Run from the repo root."""
import json, sys
from PIL import Image, ImageDraw, ImageFont
GEN = sys.argv[1] if len(sys.argv) > 1 else 'tools/.gen'
im = Image.frombytes('RGB', (240, 135), open('host_test/panel_clean.rgb', 'rb').read())
Z = 4; big = im.resize((240*Z, 135*Z), Image.NEAREST); big.save('docs/panel.png')
J = json.load(open(GEN + '/params.json')); P = J['P']; akai = {pid: cc for cc, pid in J['akai']}
B = '/usr/share/fonts/truetype/dejavu/DejaVuSansMono-Bold.ttf'
f = ImageFont.truetype(B, 13); fs = ImageFont.truetype(B, 11)
can = big.copy(); dr = ImageDraw.Draw(can)
cellX = lambda c: c*29 + (8 if c >= 4 else 0); cellY = lambda r: 19 + r*38
for p in P:
    if p['col'] == 255: continue
    x0 = cellX(p['col'])*Z; y0 = cellY(p['row'])*Z; cx = x0 + 29*Z//2; t = "CC%d" % p['cc']; w = dr.textlength(t, font=f); ty = y0 + int(34.2*Z) - 8
    dr.rounded_rectangle([cx-w/2-4, ty-2, cx+w/2+4, ty+15], radius=4, fill=(0, 0, 0)); dr.text((cx-w/2, ty), t, fill=(255, 255, 255), font=f)
    if p['id'] in akai:
        k = "K%d" % akai[p['id']]; kw = dr.textlength(k, font=fs); kx = x0 + 29*Z - kw - 6; ky = y0 + int(9.5*Z)
        dr.rounded_rectangle([kx-3, ky-1, kx+kw+3, ky+13], radius=3, fill=(255, 200, 0)); dr.text((kx, ky), k, fill=(0, 0, 0), font=fs)
gx = (cellX(5)+3+32)*Z + 8; gy = (cellY(2)+25)*Z - 1
dr.text((gx, gy), "on/off: CC65 \u00b7 button B", fill=(255, 200, 0), font=fs)
can.save('docs/panel_cc.png')
# second state (playing): plain panel, no annotations
Image.frombytes('RGB', (240, 135), open('host_test/panel_played.rgb', 'rb').read()).resize((240*Z, 135*Z), Image.NEAREST).save('docs/panel_played.png')
