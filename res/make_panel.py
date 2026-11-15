#!/usr/bin/env python3
# Draws res/Kleener.svg. VCV Rack can't draw SVG <text>, so every label is
# turned into outline paths with fontTools. Positions are in mm and must match
# src/Kleener.cpp (the numbers in the LAYOUT block are the same in both).
#   pip install fonttools
#   python3 res/make_panel.py
import os
from fontTools.ttLib import TTFont
from fontTools.pens.svgPathPen import SVGPathPen
from fontTools.pens.transformPen import TransformPen

HERE = os.path.dirname(os.path.abspath(__file__))
FONT_BOLD = os.environ.get("PANEL_FONT_BOLD", "/usr/share/fonts/opentype/inter/Inter-Bold.otf")
FONT_MED = os.environ.get("PANEL_FONT_MED", "/usr/share/fonts/opentype/inter/Inter-Medium.otf")

W, H = 152.4, 128.5  # 30 HP

# Paul Klee-ish palette: warm paper, soft colour blocks
BG = "#EFE6D2"
INK = "#2A2420"
SOFT = "#6E6458"
LINE = "#C9BC9F"
BLOCKS = ["#E3C7A0", "#D9B9A6", "#C9CDB0", "#D6C3A2", "#CFC2B4", "#E0CFA4", "#CDBFA9", "#D8C6AE"]
BUS_COL = ["#B4532A", "#2F6F73", "#C0902A"]   # bus 1 rust, bus 2 teal, bus 3 ochre
OUT_BG = "#3A322C"
OUT_INK = "#F3EBDA"

# ---- LAYOUT (same numbers in src/Kleener.cpp) ----
COL_X0, COL_DX = 16.7, 17.0
ROW_Y = [25.0, 45.0]
BTN_DX, SW_DX, LIGHT_DY = -3.2, 3.6, 5.8
SLIDER_Y = 70.5
KNOB_Y = 95.5
X_LEN, X_LENCV, X_MUT, X_MUTCV, X_RANGE, X_RANGECV, X_GLIDE, X_SCALE, X_LOAD = 12.0, 23.5, 39.0, 50.5, 66.0, 77.5, 92.0, 106.0, 141.0
SCALE_LX, SCALE_Y0, SCALE_DY = 113.0, 86.6, 2.7
JACK_Y = 115.6
X_CLOCK, X_RESET, X_LOADIN, X_FLIP = 11.0, 25.0, 39.0, 53.0
X_A, X_B, X_BUS1, X_BUS2, X_BUS3, X_ANY = 71.0, 84.0, 100.0, 113.0, 126.0, 141.0

SCALES = ["OFF", "CHROM", "MINOR", "DORIAN", "PHRYG", "PENTA"]

CONTROL = 3.2   # control label size (project standard)
JACK = 2.9      # jack label size (project standard)

fonts = {}


def text(s, x, y, size, font=FONT_BOLD, fill=INK, anchor="middle", spacing=0.04):
    """SVG path for string s; (x, y) is the baseline point in mm."""
    if font not in fonts:
        f = TTFont(font)
        fonts[font] = (f, f.getGlyphSet(), f.getBestCmap(), f["head"].unitsPerEm)
    f, gs, cmap, upm = fonts[font]
    scale = size / upm
    track = spacing * upm
    names = [cmap[ord(c)] for c in s]
    width = sum(gs[n].width + track for n in names) - track
    x0 = {"middle": x - width * scale / 2, "end": x - width * scale}.get(anchor, x)
    pen = SVGPathPen(gs)
    cx = 0.0
    for n in names:
        gs[n].draw(TransformPen(pen, (scale, 0, 0, -scale, x0 + cx * scale, y)))
        cx += gs[n].width + track
    return f'<path fill="{fill}" d="{pen.getCommands()}"/>'


parts = []
add = parts.append

add(f'<rect width="{W}" height="{H}" fill="{BG}"/>')

# title
add(text("Kleener", 6.0, 11.2, 5.2, FONT_BOLD, INK, anchor="start", spacing=0.05))
# little Klee-style block motif after the title
bx = 31.5
for i, (c, h) in enumerate(zip(["#B4532A", "#C0902A", "#2F6F73", "#7A4E6B", "#B4532A"], [3.2, 4.4, 2.4, 3.8, 1.8])):
    add(f'<rect x="{bx + i * 2.6}" y="{11.2 - h}" width="2.2" height="{h}" fill="{c}"/>')
add(text("16-bit loop sequencer", 47.5, 10.8, 2.6, FONT_MED, SOFT, anchor="start"))

# bit cells: a soft colour block per column, step number, bus legend on the right
for r, y in enumerate(ROW_Y):
    for i in range(8):
        x = COL_X0 + i * COL_DX
        col = BLOCKS[(i + r * 3) % len(BLOCKS)]
        add(f'<rect x="{x - 8.1}" y="{y - 9.6}" width="16.2" height="{LIGHT_DY + 13.0}" rx="0.8" fill="{col}"/>')
        add(text(str(r * 8 + i + 1), x + BTN_DX, y - 4.5, CONTROL, FONT_BOLD, INK))
# bus legend: switch down = 1, middle = 2, up = 3
lx = W - 3.0
for r, y in enumerate(ROW_Y):
    for k, dy in enumerate([3.2, 0.0, -3.2]):
        add(text(str(k + 1), lx, y + dy + 1.0, 2.6, FONT_BOLD, BUS_COL[k], anchor="end"))
add(text("BUS", W - 3.0, ROW_Y[0] - 10.6 + 3.0, 2.4, FONT_MED, SOFT, anchor="end"))

# sliders: pitch weight for each of the first 8 bits of the loop window
add(f'<line x1="3.5" y1="{SLIDER_Y - 16.6}" x2="{W - 3.5}" y2="{SLIDER_Y - 16.6}" stroke="{LINE}" stroke-width="0.35"/>')
for i in range(8):
    x = COL_X0 + i * COL_DX
    add(text(f"+{i + 1}", x, SLIDER_Y - 14.0 + 0.1, CONTROL, FONT_BOLD, INK))
    # scale ticks beside the slider
    for t in range(13):
        ty = SLIDER_Y + 11.0 - t * (22.0 / 12)
        w = 1.6 if t % 12 == 0 else (1.1 if t % 6 == 0 else 0.6)
        add(f'<line x1="{x + 4.1}" y1="{ty:.2f}" x2="{x + 4.1 + w}" y2="{ty:.2f}" stroke="{SOFT}" stroke-width="0.25"/>')

# knob row
add(f'<line x1="3.5" y1="{KNOB_Y - 11.6}" x2="{W - 3.5}" y2="{KNOB_Y - 11.6}" stroke="{LINE}" stroke-width="0.35"/>')
LBL_Y = KNOB_Y - 7.2
add(text("LENGTH", X_LEN, LBL_Y, CONTROL))
add(text("CV", X_LENCV, LBL_Y + 1.4, JACK))
add(text("MUTATE", X_MUT, LBL_Y, CONTROL))
add(text("CV", X_MUTCV, LBL_Y + 1.4, JACK))
add(text("RANGE", X_RANGE, LBL_Y, CONTROL))
add(text("CV", X_RANGECV, LBL_Y + 1.4, JACK))
add(text("GLIDE", X_GLIDE, LBL_Y, CONTROL))
add(text("SCALE", X_SCALE, LBL_Y, CONTROL))
add(text("LOAD", X_LOAD, LBL_Y, CONTROL))
for k, name in enumerate(SCALES):
    add(text(name, SCALE_LX + 2.0, SCALE_Y0 + k * SCALE_DY + 0.95, 2.5, FONT_BOLD, SOFT, anchor="start"))
# knob range marks
add(text("2", X_LEN - 5.4, KNOB_Y + 7.4, 2.4, FONT_MED, SOFT))
add(text("16", X_LEN + 5.4, KNOB_Y + 7.4, 2.4, FONT_MED, SOFT))
add(text("lock", X_MUT - 5.6, KNOB_Y + 7.4, 2.4, FONT_MED, SOFT))
add(text("flip", X_MUT + 5.6, KNOB_Y + 7.4, 2.4, FONT_MED, SOFT))

# jack row: inputs on paper, outputs on a dark band
add(f'<rect x="{X_A - 8.0}" y="{JACK_Y - 13.8}" width="{W - 3.5 - (X_A - 8.0)}" height="20.0" rx="1.2" fill="{OUT_BG}"/>')
JL_Y = JACK_Y - 6.4
for x, s in [(X_CLOCK, "CLOCK"), (X_RESET, "RESET"), (X_LOADIN, "LOAD"), (X_FLIP, "FLIP")]:
    add(text(s, x, JL_Y, JACK))
add(text("PITCH", (X_A + X_B) / 2, JACK_Y - 10.6, 2.5, FONT_MED, OUT_INK))
add(text("A", X_A, JL_Y, JACK, fill=OUT_INK))
add(text("B", X_B, JL_Y, JACK, fill=OUT_INK))
add(text("BUS GATES", X_BUS2, JACK_Y - 10.6, 2.5, FONT_MED, OUT_INK))
for k, x in enumerate([X_BUS1, X_BUS2, X_BUS3]):
    add(text(str(k + 1), x, JL_Y, JACK, fill=BUS_COL[k] if k else "#E07A4F"))
add(text("ANY", X_ANY, JL_Y, JACK, fill=OUT_INK))
add(f'<line x1="{(X_B + X_BUS1) / 2}" y1="{JACK_Y - 12.6}" x2="{(X_B + X_BUS1) / 2}" y2="{JACK_Y + 4.0}" stroke="#5A4F45" stroke-width="0.35"/>')

svg = (f'<svg xmlns="http://www.w3.org/2000/svg" width="{W}mm" height="{H}mm" viewBox="0 0 {W} {H}">\n'
       + "\n".join(parts) + "\n</svg>\n")
open(os.path.join(HERE, "Kleener.svg"), "w").write(svg)
print("ok")

# --- Haer maker logo (res/haer_logo.py) on the bottom rail
import sys as _sys
_sys.path.insert(0, HERE)
import haer_logo
haer_logo.add_to_file(os.path.join(HERE, "Kleener.svg"), x=W / 2, y=126.0, cap=3.0, bg=BG)
