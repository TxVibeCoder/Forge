"""
forge_ui.py — a small ezdxf toolkit for drawing to-scale CAD mockups of the Forge DAW UI.

Design intent
-------------
* 1 drawing unit == 1 logical UI pixel. The canonical window is 1600 x 1000.
* Y is DOWN in UI space (top-left origin). A Canvas translates UI (x, y) into
  modelspace (Y-up) by  mx = ox + x ,  my = oy - y  — a pure translation per point,
  so nothing is mirrored; rectangles and text keep their natural orientation.
* Every Forge palette colour is a CAD LAYER carrying that colour as its true-colour.
  This mirrors ForgeLookAndFeel's "all colours via colour IDs" philosophy: in CAD you
  can re-theme by editing a layer colour, or FREEZE a category (e.g. all *-BG fills)
  to drop to clean wireframe for tweaking geometry.
"""
import math
import random
import ezdxf
from ezdxf.enums import TextEntityAlignment

# ----------------------------------------------------------------------------- palette
# Exact ForgeLookAndFeel palette (src/ui/ForgeLookAndFeel.h) + a few mockup-only tints.
RGB = {
    "shell":      (0x1a, 0x1c, 0x1e),  # window / shell background
    "panel":      (0x23, 0x26, 0x29),  # docked panels (control bar, headers)
    "raised":     (0x2d, 0x31, 0x35),  # raised controls (buttons)
    "textPrim":   (0xd6, 0xd9, 0xdc),  # primary text
    "textSec":    (0x8a, 0x90, 0x95),  # secondary text / labels
    "hairline":   (0x34, 0x38, 0x3c),  # separators
    "accent":     (0xe0, 0x90, 0x2f),  # warm amber: playhead, arm, selection, focus
    "onAccent":   (0x24, 0x16, 0x00),  # text on the accent fill
    "record":     (0xe2, 0x4b, 0x4a),  # active recording / clipping
    # --- mockup-only expressive tints (kept in the family) ---
    "wave":       (0x70, 0x7b, 0x84),  # audio waveform (cool grey)
    "selectWash": (0x3a, 0x31, 0x1d),  # amber-tinted selection wash
    "mGreen":     (0x57, 0xa9, 0x63),  # meter nominal
    "mAmber":     (0xe0, 0x90, 0x2f),  # meter hot
    "mRed":       (0xe2, 0x4b, 0x4a),  # meter clip
    "annot":      (0x4f, 0xa3, 0xc7),  # CAD annotation / callouts (NOT a UI colour)
    "dim":        (0x9a, 0xa2, 0xa8),  # CAD dimension / sheet text
    "ghost":      (0x55, 0x5c, 0x62),  # ghost / placeholder strokes
}

# Saturated-but-muted per-track / per-clip colours (tint over the dark base).
TRACK_COLORS = [
    (0xC8, 0x6E, 0x3C),  # amber-orange
    (0x4E, 0x9A, 0xA6),  # teal
    (0x9A, 0x6F, 0xB0),  # violet
    (0x6F, 0xA8, 0x55),  # green
    (0xC9, 0x5B, 0x6B),  # rose
    (0x5A, 0x86, 0xC4),  # blue
    (0xB7, 0xA0, 0x4A),  # gold
    (0x7C, 0x8A, 0x95),  # slate
]

# ----------------------------------------------------------------------------- layers
# (layer name, palette key for its true-colour, lineweight in 1/100 mm or None)
LAYERS = [
    ("FORGE-SHEET",   "dim",      35),   # sheet border + title block
    ("FORGE-TITLE",   "textPrim", None),
    ("FORGE-DIM",     "dim",      9),    # dimension lines / measurements
    ("FORGE-NOTE",    "annot",    13),   # callout leaders + annotation text
    ("UI-SHELL-BG",   "shell",    None),
    ("UI-PANEL-BG",   "panel",    None),
    ("UI-RAISED",     "raised",   None),
    ("UI-HAIRLINE",   "hairline", 9),
    ("UI-TEXT",       "textPrim", None),
    ("UI-TEXT-SEC",   "textSec",  None),
    ("UI-ACCENT",     "accent",   18),
    ("UI-RECORD",     "record",   18),
    ("UI-WAVEFORM",   "wave",     None),
    ("UI-MIDI-NOTE",  "accent",   None),
    ("UI-METER",      "mGreen",   None),
    ("UI-SELECT",     "accent",   25),
    ("UI-CLIP",       "raised",   13),
    ("UI-GHOST",      "ghost",    9),
]

# text heights (UI px)
H_TITLE   = 26
H_SECTION = 15
H_LABEL   = 13
H_META    = 11
H_TINY    = 9


def new_doc():
    doc = ezdxf.new("R2010", setup=True)
    doc.header["$INSUNITS"] = 0  # unitless (we treat units as px)
    for name, key, lw in LAYERS:
        lay = doc.layers.add(name)
        lay.rgb = RGB[key]
        if lw is not None:
            lay.dxf.lineweight = lw
    return doc


_ALIGN = {
    "tl": TextEntityAlignment.TOP_LEFT,    "tc": TextEntityAlignment.TOP_CENTER,    "tr": TextEntityAlignment.TOP_RIGHT,
    "ml": TextEntityAlignment.MIDDLE_LEFT, "mc": TextEntityAlignment.MIDDLE_CENTER, "mr": TextEntityAlignment.MIDDLE_RIGHT,
    "bl": TextEntityAlignment.BOTTOM_LEFT, "bc": TextEntityAlignment.BOTTOM_CENTER, "br": TextEntityAlignment.BOTTOM_RIGHT,
}


class Canvas:
    """A screen-local drawing frame. UI coords are top-left origin, y-down."""

    def __init__(self, doc, ox, oy, w, h):
        self.doc = doc
        self.msp = doc.modelspace()
        self.ox, self.oy = ox, oy          # modelspace position of the screen's TOP-LEFT
        self.w, self.h = w, h

    # -- coordinate transform -------------------------------------------------
    def p(self, x, y):
        return (self.ox + x, self.oy - y)

    def child(self, x, y, w, h):
        """A nested canvas whose (0,0) is at this canvas's (x,y)."""
        return Canvas(self.doc, self.ox + x, self.oy - y, w, h)

    # -- primitives -----------------------------------------------------------
    def fill(self, x, y, w, h, layer, rgb=None):
        ha = self.msp.add_hatch(dxfattribs={"layer": layer})
        ha.set_solid_fill()
        if rgb is not None:
            ha.rgb = rgb
        else:
            ha.dxf.color = 256  # BYLAYER
        pts = [self.p(x, y), self.p(x + w, y), self.p(x + w, y + h), self.p(x, y + h)]
        ha.paths.add_polyline_path(pts, is_closed=True)
        return ha

    def outline(self, x, y, w, h, layer="UI-HAIRLINE", rgb=None):
        pts = [self.p(x, y), self.p(x + w, y), self.p(x + w, y + h), self.p(x, y + h)]
        attr = {"layer": layer}
        if rgb is not None:
            attr["true_color"] = ezdxf.rgb2int(rgb)
        return self.msp.add_lwpolyline(pts, close=True, dxfattribs=attr)

    def rect(self, x, y, w, h, fill_layer=None, fill_rgb=None,
             outline_layer="UI-HAIRLINE", outline_rgb=None):
        if fill_layer is not None or fill_rgb is not None:
            self.fill(x, y, w, h, fill_layer or "UI-PANEL-BG", rgb=fill_rgb)
        if outline_layer is not None or outline_rgb is not None:
            self.outline(x, y, w, h, layer=outline_layer or "UI-HAIRLINE", rgb=outline_rgb)

    def line(self, x1, y1, x2, y2, layer="UI-HAIRLINE", rgb=None):
        attr = {"layer": layer}
        if rgb is not None:
            attr["true_color"] = ezdxf.rgb2int(rgb)
        return self.msp.add_line(self.p(x1, y1), self.p(x2, y2), dxfattribs=attr)

    def polyline(self, ui_pts, layer="UI-HAIRLINE", close=False, rgb=None):
        attr = {"layer": layer}
        if rgb is not None:
            attr["true_color"] = ezdxf.rgb2int(rgb)
        return self.msp.add_lwpolyline([self.p(x, y) for x, y in ui_pts],
                                       close=close, dxfattribs=attr)

    def text(self, s, x, y, height=H_LABEL, layer="UI-TEXT", align="ml", rgb=None):
        attr = {"layer": layer, "style": "Standard"}
        if rgb is not None:
            attr["true_color"] = ezdxf.rgb2int(rgb)
        t = self.msp.add_text(str(s), height=height, dxfattribs=attr)
        t.set_placement(self.p(x, y), align=_ALIGN[align])
        return t

    def circle(self, cx, cy, r, layer="UI-HAIRLINE", rgb=None):
        attr = {"layer": layer}
        if rgb is not None:
            attr["true_color"] = ezdxf.rgb2int(rgb)
        return self.msp.add_circle(self.p(cx, cy), r, dxfattribs=attr)

    def disc(self, cx, cy, r, layer, rgb=None):
        ha = self.msp.add_hatch(dxfattribs={"layer": layer})
        ha.set_solid_fill()
        if rgb is not None:
            ha.rgb = rgb
        else:
            ha.dxf.color = 256
        ep = ha.paths.add_edge_path()
        ep.add_arc(self.p(cx, cy), r, 0, 360)
        return ha

    def arc(self, cx, cy, r, a0, a1, layer="UI-ACCENT", rgb=None):
        # a0,a1 in UI degrees measured clockwise from 12 o'clock; convert to CCW-from-east.
        def conv(a):
            return (90 - a) % 360
        attr = {"layer": layer}
        if rgb is not None:
            attr["true_color"] = ezdxf.rgb2int(rgb)
        # ezdxf arc goes CCW from start to end; we want CW sweep, so swap.
        return self.msp.add_arc(self.p(cx, cy), r, conv(a1), conv(a0), dxfattribs=attr)

    # -- compound widgets -----------------------------------------------------
    def panel(self, x, y, w, h, fill="UI-PANEL-BG", border=True):
        self.fill(x, y, w, h, fill)
        if border:
            self.outline(x, y, w, h, "UI-HAIRLINE")

    def button(self, x, y, w, h, label="", on=False, glyph=False,
               text_layer=None, height=H_LABEL, accent_text=False):
        if on:
            self.fill(x, y, w, h, "UI-ACCENT")
            tl = text_layer or "UI-TEXT"
            trgb = RGB["onAccent"]
        else:
            self.fill(x, y, w, h, "UI-RAISED")
            tl = text_layer or "UI-TEXT"
            trgb = None
        self.outline(x, y, w, h, "UI-HAIRLINE")
        if label:
            self.text(label, x + w / 2, y + h / 2, height=height,
                      layer=tl, align="mc", rgb=trgb)
        return self

    def swatch(self, x, y, w, h, rgb):
        self.fill(x, y, w, h, "UI-CLIP", rgb=rgb)
        self.outline(x, y, w, h, "UI-HAIRLINE")

    def vfader(self, x, y, w, h, pos=0.72, label_db=None):
        """Vertical fader. pos 0..1 from bottom. Cap is a small raised tab."""
        cx = x + w / 2
        # track
        self.line(cx, y + 6, cx, y + h - 6, "UI-HAIRLINE")
        # ticks
        for f in (0.0, 0.25, 0.5, 0.75, 1.0):
            ty = y + 6 + (1 - f) * (h - 12)
            self.line(cx + 4, ty, cx + 7, ty, "UI-HAIRLINE")
        cap_h = 14
        capy = y + 6 + (1 - pos) * (h - 12) - cap_h / 2
        self.fill(x + 2, capy, w - 4, cap_h, "UI-RAISED")
        self.outline(x + 2, capy, w - 4, cap_h, "UI-ACCENT")
        self.line(x + 3, capy + cap_h / 2, x + w - 3, capy + cap_h / 2, "UI-ACCENT")
        if label_db is not None:
            self.text(label_db, cx, y + h + 11, height=H_TINY, layer="UI-TEXT-SEC", align="mc")

    def knob(self, cx, cy, r, value=0.5, label=None, sweep=270):
        """Rotary knob. value 0..1 over a `sweep`-degree arc centred at 12 o'clock."""
        self.disc(cx, cy, r, "UI-RAISED")
        self.circle(cx, cy, r, "UI-HAIRLINE")
        a = (value - 0.5) * sweep  # degrees CW from 12 o'clock
        rad = math.radians(a)
        px = cx + math.sin(rad) * (r - 2)
        py = cy - math.cos(rad) * (r - 2)
        self.line(cx, cy, px, py, "UI-ACCENT")
        # value arc
        self.arc(cx, cy, r + 3, -sweep / 2, a, "UI-ACCENT")
        if label:
            self.text(label, cx, cy + r + 9, height=H_TINY, layer="UI-TEXT-SEC", align="mc")

    def meter(self, x, y, w, h, level=0.6, peak=None):
        """Vertical peak meter. level 0..1; green→amber→red zones."""
        self.fill(x, y, w, h, "UI-PANEL-BG")
        self.outline(x, y, w, h, "UI-HAIRLINE")
        filled = h * level
        # zones by absolute fraction
        zones = [(0.0, 0.75, "mGreen"), (0.75, 0.9, "mAmber"), (0.9, 1.0, "mRed")]
        for z0, z1, col in zones:
            seg0 = max(z0, 1 - level)  # remember y is down; bottom is high y
            # compute the lit portion of this zone (measured from bottom)
            lo = z0
            hi = min(z1, level)
            if hi <= lo:
                continue
            seg_y = y + h - hi * h
            seg_h = (hi - lo) * h
            self.fill(x + 1, seg_y, w - 2, seg_h, "UI-METER", rgb=RGB[col])
        if peak is not None:
            py = y + h - peak * h
            self.line(x, py, x + w, py, "UI-ACCENT")

    def waveform(self, x, y, w, h, seed=1, density=2.0, layer="UI-WAVEFORM"):
        """Filled symmetric audio waveform inside the rect."""
        rnd = random.Random(seed)
        mid = y + h / 2
        n = max(20, int(w / 3))
        top = []
        bot = []
        env = 0.0
        for i in range(n + 1):
            t = i / n
            target = (0.25 + 0.6 * abs(math.sin(t * math.pi * density + seed))
                      * (0.6 + 0.4 * rnd.random()))
            env += (target - env) * 0.5
            a = env * (h / 2 - 2)
            xx = x + t * w
            top.append((xx, mid - a))
            bot.append((xx, mid + a))
        pts = top + bot[::-1]
        ha = self.msp.add_hatch(dxfattribs={"layer": layer})
        ha.set_solid_fill()
        ha.dxf.color = 256
        ha.paths.add_polyline_path([self.p(px, py) for px, py in pts], is_closed=True)
        self.line(x, mid, x + w, mid, layer)

    def playhead(self, x, top, bottom, head=True):
        self.line(x, top, x, bottom, "UI-ACCENT")
        if head:
            self.polyline([(x - 5, top), (x + 5, top), (x, top + 7)],
                          layer="UI-ACCENT", close=True)

    # -- annotation -----------------------------------------------------------
    def callout(self, x, y, tx, ty, text, align="ml", height=H_META):
        self.line(x, y, tx, ty, "FORGE-NOTE")
        self.disc(x, y, 2.2, "FORGE-NOTE")
        self.text(text, tx + (4 if "l" in align else -4), ty, height=height,
                  layer="FORGE-NOTE", align=align)

    def dim_h(self, x1, x2, y, text, above=True):
        self.line(x1, y, x2, y, "FORGE-DIM")
        for xx in (x1, x2):
            self.line(xx, y - 4, xx, y + 4, "FORGE-DIM")
        self.text(text, (x1 + x2) / 2, y - 5 if above else y + 5,
                  height=H_TINY, layer="FORGE-DIM",
                  align="bc" if above else "tc")

    def dim_v(self, y1, y2, x, text, left=True):
        self.line(x, y1, x, y2, "FORGE-DIM")
        for yy in (y1, y2):
            self.line(x - 4, yy, x + 4, yy, "FORGE-DIM")
        self.text(text, x - 5 if left else x + 5, (y1 + y2) / 2,
                  height=H_TINY, layer="FORGE-DIM",
                  align="mr" if left else "ml")


def sheet(doc, ox, oy, w, h, number, title, caption=""):
    """Draw a titled sheet border and return a Canvas for its content area."""
    msp = doc.modelspace()
    pad = 16
    title_h = 40
    # outer border
    msp.add_lwpolyline(
        [(ox - pad, oy + pad), (ox + w + pad, oy + pad),
         (ox + w + pad, oy - h - pad - title_h), (ox - pad, oy - h - pad - title_h)],
        close=True, dxfattribs={"layer": "FORGE-SHEET"})
    # title block under the screen
    ty = oy - h - pad
    msp.add_line((ox - pad, ty), (ox + w + pad, ty), dxfattribs={"layer": "FORGE-SHEET"})
    tt = msp.add_text(f"{number}", height=H_TITLE,
                      dxfattribs={"layer": "FORGE-TITLE", "style": "Standard"})
    tt.set_placement((ox - pad + 8, ty - title_h / 2), align=TextEntityAlignment.MIDDLE_LEFT)
    tt2 = msp.add_text(title.upper(), height=H_SECTION,
                       dxfattribs={"layer": "FORGE-TITLE", "style": "Standard"})
    tt2.set_placement((ox - pad + 64, ty - 14), align=TextEntityAlignment.MIDDLE_LEFT)
    if caption:
        tc = msp.add_text(caption, height=H_META,
                          dxfattribs={"layer": "FORGE-DIM", "style": "Standard"})
        tc.set_placement((ox - pad + 64, ty - 30), align=TextEntityAlignment.MIDDLE_LEFT)
    return Canvas(doc, ox, oy, w, h)
