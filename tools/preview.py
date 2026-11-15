#!/usr/bin/env python3
# Mock-up picture of the panel with knobs, switches, sliders, jacks and lights
# drawn in, so the layout can be checked without Rack.
#   python3 tools/preview.py out.png
import os, sys, math, random, subprocess
HERE = os.path.dirname(os.path.abspath(__file__))
svg = open(os.path.join(HERE, "..", "res", "Kleener.svg")).read()
L = {}
src = open(os.path.join(HERE, "..", "res", "make_panel.py")).read()
exec(src.split("# ---- LAYOUT")[1].split("CONTROL =")[0].split("\n", 1)[1], L)
g = []
def jack(x, y):
    g.append(f'<circle cx="{x}" cy="{y}" r="4.1" fill="#C9CCD1"/><circle cx="{x}" cy="{y}" r="3.1" fill="#7C8088"/><circle cx="{x}" cy="{y}" r="1.9" fill="#0B0B0C"/>')
def knob(x, y, a, r=4.8):
    g.append(f'<circle cx="{x}" cy="{y}" r="{r}" fill="#26272B" stroke="#3A3C42" stroke-width="0.5"/>')
    ang = math.radians(-135 + 270 * a)
    g.append(f'<line x1="{x}" y1="{y}" x2="{x + (r-0.8)*math.sin(ang):.2f}" y2="{y - (r-0.8)*math.cos(ang):.2f}" stroke="#EEE" stroke-width="0.7" stroke-linecap="round"/>')
def light(x, y, b, col="#FFD23C", r=1.0):
    g.append(f'<circle cx="{x}" cy="{y}" r="{r}" fill="#3a3320"/><circle cx="{x}" cy="{y}" r="{r}" fill="{col}" fill-opacity="{b}"/>')
def button(x, y, on):
    g.append(f'<circle cx="{x}" cy="{y}" r="3.05" fill="#2B2D33"/><circle cx="{x}" cy="{y}" r="2.2" fill="{"#F4F1EA" if on else "#55565c"}"/>')
def switch3(x, y, v):
    g.append(f'<rect x="{x-2.3}" y="{y-4.8}" width="4.6" height="9.6" rx="0.5" fill="#202020"/>')
    hy = y + 3.0 - v * 3.0
    g.append(f'<rect x="{x-1.85}" y="{hy-1.5}" width="3.7" height="3.0" fill="#BBBBBB"/>')
def slider(x, y, v):
    g.append(f'<rect x="{x-3.35}" y="{y-12.95}" width="6.7" height="25.9" rx="0.6" fill="#2a2a2a"/><rect x="{x-0.5}" y="{y-11}" width="1" height="22" fill="#111"/>')
    hy = y + 11 - v * 22
    g.append(f'<rect x="{x-3.3}" y="{hy-2}" width="6.6" height="4" rx="0.4" fill="#d8d8d8"/><rect x="{x-3.3}" y="{hy-0.2}" width="6.6" height="0.4" fill="#333"/>')
random.seed(3)
pattern = [1,0,0,1,0,1,1,0, 1,0,1,0,0,1,0,0]
for i in range(16):
    x = L["COL_X0"] + (i % 8) * L["COL_DX"]; y = L["ROW_Y"][i // 8]
    button(x + L["BTN_DX"], y, pattern[i])
    switch3(x + L["SW_DX"], y, random.randint(0, 2))
    light(x + L["BTN_DX"], y + L["LIGHT_DY"], 1.0 if pattern[i] else 0.06)
    light(x + L["SW_DX"], y + L["LIGHT_DY"], 1.0 if i == 5 else 0.0, "#FFFFFF", 0.7)
for i, v in enumerate([3, 7, 2, 5, 12, 10, 5, 0]):
    slider(L["COL_X0"] + i * L["COL_DX"], L["SLIDER_Y"], v / 12)
ky = L["KNOB_Y"]
knob(L["X_LEN"], ky, 1.0); knob(L["X_MUT"], ky, 0.1); knob(L["X_RANGE"], ky, 0.5); knob(L["X_GLIDE"], ky, 0.0); knob(L["X_SCALE"], ky, 0.4)
for k in range(6):
    light(L["SCALE_LX"], L["SCALE_Y0"] + k * L["SCALE_DY"], 1.0 if k == 2 else 0.0, "#FFD23C", 0.8)
g.append(f'<circle cx="{L["X_LOAD"]}" cy="{ky}" r="3.6" fill="#2B2D33"/><circle cx="{L["X_LOAD"]}" cy="{ky}" r="2.6" fill="#4a4126"/>')
for x in (L["X_LENCV"], L["X_MUTCV"], L["X_RANGECV"]):
    jack(x, ky)
for x in (L["X_CLOCK"], L["X_RESET"], L["X_LOADIN"], L["X_FLIP"], L["X_A"], L["X_B"], L["X_BUS1"], L["X_BUS2"], L["X_BUS3"], L["X_ANY"]):
    jack(x, L["JACK_Y"])
W = 152.4
for x in (7.6, W - 15.2):
    for y in (2.5, 126.0): g.append(f'<circle cx="{x}" cy="{y}" r="1.6" fill="#8A8D93"/>')
svg = svg.replace("</svg>", "\n".join(g) + "\n</svg>")
out = os.path.abspath(sys.argv[1] if len(sys.argv) > 1 else "preview.png")
tmp = out + ".svg"; open(tmp, "w").write(svg)
pw, ph = 762, 643
open(tmp + ".html", "w").write(f'<html><body style="margin:0;background:#111"><img src="{os.path.basename(tmp)}" style="width:{pw}px;height:{ph}px"></body></html>')
js = f"""const {{chromium}} = require('playwright');
(async () => {{ const b = await chromium.launch(); const p = await b.newPage({{viewport: {{width: {pw}, height: {ph}}}, deviceScaleFactor: 2}});
await p.goto('file://{tmp}.html'); await p.waitForTimeout(300);
await p.screenshot({{path: '{out}'}}); await b.close(); }})();"""
open(out + ".js", "w").write(js)
subprocess.run(["node", out + ".js"], check=True, cwd="/tmp")
os.remove(out + ".js"); os.remove(tmp); os.remove(tmp + ".html")
print("wrote", out)
