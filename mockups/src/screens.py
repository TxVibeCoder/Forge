"""
screens.py — composes faithful, to-scale Forge DAW UI states using forge_ui primitives.
Coordinates are UI pixels (top-left origin, y-down) inside each screen's Canvas.
Dimensions trace the real code (ForgeShell / ArrangeView / MixerView / PianoRollView /
DetailView / BrowserView); vision elements (automation, sends, markers, device-chain
drawer, preferences) are drawn as the envisioned final product and flagged in the plan doc.
"""
import math
from forge_ui import Canvas, RGB, TRACK_COLORS, H_TITLE, H_SECTION, H_LABEL, H_META, H_TINY

# ---- canonical window + faithful region metrics -----------------------------
WIN_W, WIN_H = 1600, 1000
CB_H   = 46     # control bar
ST_H   = 24     # status strip
HEADER_W = 150  # arrange track header
LANE_H   = 76
GAP      = 4
RULER_H  = 22
HINT_H   = 20
PPB      = 104  # px per bar (mockup time scale, 4/4)
PPBEAT   = PPB / 4

# ============================================================ shared glyphs ====

def g_play(c, cx, cy, s, layer="UI-TEXT", rgb=None):
    c.polyline([(cx - s*0.5, cy - s*0.6), (cx + s*0.7, cy), (cx - s*0.5, cy + s*0.6)],
               layer=layer, close=True, rgb=rgb)

def g_pause(c, cx, cy, s, layer="UI-TEXT", rgb=None):
    c.rect(cx - s*0.5, cy - s*0.6, s*0.32, s*1.2, fill_layer=layer, outline_layer=None, fill_rgb=rgb)
    c.rect(cx + s*0.18, cy - s*0.6, s*0.32, s*1.2, fill_layer=layer, outline_layer=None, fill_rgb=rgb)

def g_stop(c, cx, cy, s, layer="UI-TEXT", rgb=None):
    c.rect(cx - s*0.55, cy - s*0.55, s*1.1, s*1.1, fill_layer=layer, outline_layer=None, fill_rgb=rgb)

def g_record(c, cx, cy, s, layer="UI-RECORD", rgb=None):
    c.disc(cx, cy, s*0.62, layer, rgb=rgb)

def g_loop(c, cx, cy, s, layer="UI-TEXT", rgb=None):
    r = s*0.62
    c.arc(cx, cy, r, 35, 320, layer=layer, rgb=rgb)
    # arrowhead at the open end (~ top-right)
    a = math.radians(35)
    ax, ay = cx + math.sin(a)*r, cy - math.cos(a)*r
    c.polyline([(ax-3, ay-1), (ax+3, ay-3), (ax+1, ay+3)], layer=layer, close=True, rgb=rgb)

def g_x(c, cx, cy, s, layer="UI-TEXT-SEC"):
    c.line(cx-s, cy-s, cx+s, cy+s, layer)
    c.line(cx-s, cy+s, cx+s, cy-s, layer)

def g_tri(c, cx, cy, s, up=True, layer="UI-TEXT-SEC", rgb=None):
    if up:
        c.polyline([(cx-s, cy+s*0.6), (cx+s, cy+s*0.6), (cx, cy-s*0.7)], layer=layer, close=True, rgb=rgb)
    else:
        c.polyline([(cx-s, cy-s*0.6), (cx+s, cy-s*0.6), (cx, cy+s*0.7)], layer=layer, close=True, rgb=rgb)

# ============================================================ shell pieces =====

def control_bar(c, *, view="arrange", playing=False, recording=False, looping=False,
                browser_on=False, drawer_on=False, timecode="00:00:00.000", bbt="1 . 1 . 1",
                rec_label=None):
    """The merged top Control Bar: file commands + transport + view-switch + toggles."""
    c.fill(0, 0, WIN_W, CB_H, "UI-PANEL-BG")
    c.line(0, CB_H, WIN_W, CB_H, "UI-HAIRLINE")
    bh = CB_H - 12
    by = 6
    # -- left: Browser toggle + file commands
    x = 6
    c.button(x, by, 66, bh, "Browser", on=browser_on, height=H_META); x += 66 + 8
    for name in ["New", "Open", "Save", "Save As", "Import", "Export", "Plugins", "Audio"]:
        c.button(x, by, 64, bh, name, height=H_META); x += 64 + 4
    left_end = x
    # -- right: [Editor]  ...  [Session|Arrange|Mix]  (Session is the primary surface)
    ed_x = WIN_W - 6 - 62
    c.button(ed_x, by, 62, bh, "Editor", on=drawer_on)
    mix_x = ed_x - 12 - 52
    arr_x = mix_x - 66
    sess_x = arr_x - 72
    c.button(sess_x, by, 72, bh, "Session", on=(view == "session"))
    c.button(arr_x, by, 66, bh, "Arrange", on=(view == "arrange"))
    c.button(mix_x, by, 52, bh, "Mix", on=(view == "mixer"))
    right_start = sess_x
    # -- center: transport cluster (glyph buttons) + readout
    tb_x = left_end + 18
    specs = [("play", g_pause if playing else g_play),
             ("stop", g_stop),
             ("rec",  g_record),
             ("loop", g_loop)]
    for key, gl in specs:
        on = (key == "rec" and recording) or (key == "loop" and looping)
        if key == "rec" and recording:
            c.fill(tb_x, by, 64, bh, "UI-RECORD")
            c.outline(tb_x, by, 64, bh, "UI-HAIRLINE")
            g_record(c, tb_x + 32, by + bh/2, 11, layer="UI-TEXT", rgb=RGB["onAccent"])
        elif key == "loop" and looping:
            c.button(tb_x, by, 64, bh, on=True)
            g_loop(c, tb_x + 32, by + bh/2, 11, layer="UI-TEXT", rgb=RGB["onAccent"])
        else:
            c.button(tb_x, by, 64, bh)
            col = "UI-RECORD" if key == "rec" else "UI-TEXT"
            gl(c, tb_x + 32, by + bh/2, 11, layer=col)
        tb_x += 64 + 4
    # readout (monospaced feel) right-aligned before the right section
    ro_w = 250
    ro_x = right_start - 12 - ro_w
    c.fill(ro_x, by, ro_w, bh, "UI-RAISED")
    c.outline(ro_x, by, ro_w, bh, "UI-HAIRLINE")
    if rec_label:
        c.disc(ro_x + 16, by + bh/2, 4, "UI-RECORD")
        c.text(rec_label, ro_x + 26, by + bh/2, height=H_LABEL, layer="UI-RECORD", align="ml")
    else:
        c.text(timecode, ro_x + 12, by + bh/2, height=15, layer="UI-TEXT", align="ml")
        c.text(bbt, ro_x + ro_w - 12, by + bh/2, height=15, layer="UI-ACCENT", align="mr")


def status_strip(c, text, right=""):
    y = WIN_H - ST_H
    c.fill(0, y, WIN_W, ST_H, "UI-PANEL-BG")
    c.line(0, y, WIN_W, y, "UI-HAIRLINE")
    c.text(text, 10, y + ST_H/2, height=H_META, layer="UI-TEXT-SEC", align="ml")
    if right:
        c.text(right, WIN_W - 10, y + ST_H/2, height=H_META, layer="UI-TEXT-SEC", align="mr")


def browser_region(c, x, y, w, h, *, tab="browse"):
    c.fill(x, y, w, h, "UI-PANEL-BG")
    c.outline(x, y, w, h, "UI-HAIRLINE")
    # tab header
    c.text("BROWSER", x + 10, y + 12, height=H_META, layer="UI-TEXT-SEC", align="ml")
    c.button(x + w - 118, y + 4, 54, 16, "Browse", on=(tab == "browse"), height=H_TINY)
    c.button(x + w - 60, y + 4, 54, 16, "Inspect", on=(tab == "inspect"), height=H_TINY)
    c.line(x, y + 24, x + w, y + 24, "UI-HAIRLINE")
    if tab == "browse":
        tree = [
            (0, "Places", True), (1, "Samples", True),
            (2, "Drums", True), (3, "Kick_808.wav", False), (3, "Snare_rim.wav", False),
            (3, "HatClosed.wav", False), (2, "Bass", True), (3, "SubBass_C.wav", False),
            (2, "Vocals", False), (1, "Instruments", True), (2, "4OSC", False),
            (2, "Sampler", False), (1, "Projects", False), (0, "Recent", False),
        ]
        ty = y + 24
        ih = 22
        for depth, label, is_dir in tree:
            if ty + ih > y + h - 4:
                break
            if label == "Drums":  # show a selected item
                c.fill(x + 1, ty, w - 2, ih, "UI-CLIP", rgb=RGB["selectWash"])
            ix = x + 8 + depth * 14
            if is_dir:
                g_tri(c, ix + 3, ty + ih/2, 3.2, up=False, layer="UI-TEXT-SEC")
                tx = ix + 12
            else:
                tx = ix + 12
            c.text(label, tx, ty + ih/2, height=H_META,
                   layer="UI-TEXT" if is_dir else "UI-TEXT-SEC", align="ml")
            ty += ih
    else:
        inspect_fields(c, x + 10, y + 34, w - 20)


def inspect_fields(c, x, y, w):
    rows = [("Track", "2 · Bass"), ("Type", "Audio"), ("Clip", "SubBass_C"),
            ("Color", None), ("Start", "5 . 1 . 1"), ("Length", "4 bars"),
            ("Gain", "-3.0 dB"), ("Warp", "Beats")]
    ry = y
    for label, val in rows:
        c.text(label, x, ry + 10, height=H_TINY, layer="UI-TEXT-SEC", align="ml")
        if label == "Color":
            c.swatch(x + 64, ry + 2, 60, 16, RGB_track(1))
        else:
            c.fill(x + 64, ry + 2, w - 64, 16, "UI-RAISED")
            c.outline(x + 64, ry + 2, w - 64, 16, "UI-HAIRLINE")
            c.text(val, x + 70, ry + 10, height=H_TINY, layer="UI-TEXT", align="ml")
        ry += 24


def RGB_track(i):
    return TRACK_COLORS[i % len(TRACK_COLORS)]

# ============================================================ arrange ==========

def arrange_region(c, x, y, w, h, tracks, *, playhead_bar=None, marker_lane=True,
                   automation_for=None, recording_track=None, rec_clip=None):
    """Draw the arrange surface (ruler + snap + lanes + clips + playhead) in [x,y,w,h]."""
    c.fill(x, y, w, h, "UI-SHELL-BG")
    clip_x = x + HEADER_W
    clip_w = w - HEADER_W

    top = y
    # optional marker lane (vision)
    mk_h = 16 if marker_lane else 0
    if marker_lane:
        c.fill(x, top, w, mk_h, "UI-PANEL-BG")
        c.line(clip_x, top, clip_x, top + mk_h, "UI-HAIRLINE")
        c.text("MARKERS", x + 8, top + mk_h/2, height=H_TINY, layer="UI-TEXT-SEC", align="ml")
        for bar, name in [(1, "Intro"), (5, "Verse"), (9, "Chorus"), (13, "Bridge")]:
            mx = clip_x + (bar - 1) * PPB
            c.polyline([(mx, top+2), (mx+7, top+2), (mx, top+mk_h-2)], layer="UI-ACCENT", close=True)
            c.text(name, mx + 10, top + mk_h/2, height=H_TINY, layer="UI-TEXT-SEC", align="ml")
        top += mk_h

    # ruler
    ruler_y = top
    c.fill(x, ruler_y, w, RULER_H, "UI-PANEL-BG")
    # snap selector over header corner
    c.fill(x + 2, ruler_y + 2, HEADER_W - 4, RULER_H - 4, "UI-RAISED")
    c.outline(x + 2, ruler_y + 2, HEADER_W - 4, RULER_H - 4, "UI-HAIRLINE")
    c.text("Snap  1/16", x + 8, ruler_y + RULER_H/2, height=H_TINY, layer="UI-TEXT", align="ml")
    g_tri(c, x + HEADER_W - 10, ruler_y + RULER_H/2, 3, up=False, layer="UI-TEXT-SEC")
    # bar numbers + lines
    nbars = int(clip_w / PPB) + 1
    for b in range(nbars + 1):
        bx = clip_x + b * PPB
        if bx > x + w:
            break
        c.line(bx, ruler_y + 4, bx, ruler_y + RULER_H, "UI-TEXT-SEC")
        c.text(str(b + 1), bx + 4, ruler_y + 9, height=H_TINY, layer="UI-TEXT-SEC", align="ml")
        for beat in range(1, 4):
            sx = bx + beat * PPBEAT
            c.line(sx, ruler_y + RULER_H*0.6, sx, ruler_y + RULER_H, "UI-HAIRLINE")
    c.line(x, ruler_y + RULER_H, x + w, ruler_y + RULER_H, "UI-HAIRLINE")

    # lanes
    lane_y = ruler_y + RULER_H
    for i, tr in enumerate(tracks):
        ly = lane_y + i * (LANE_H + GAP)
        if ly + LANE_H > y + h - HINT_H:
            break
        draw_lane(c, x, ly, w, clip_x, tr, i,
                  armed=(tr.get("arm")), recording=(recording_track == i),
                  rec_clip=rec_clip if recording_track == i else None)
        # vertical bar grid faint across lane body
        for b in range(nbars + 1):
            bx = clip_x + b * PPB
            if bx <= x + w:
                c.line(bx, ly, bx, ly + LANE_H, "UI-GHOST")
        # automation lane (vision) under a chosen track
        if automation_for == i:
            ay = ly + LANE_H + GAP
            draw_automation(c, x, ay, w, clip_x, tr)

    # hint strip
    hy = y + h - HINT_H
    c.fill(x, hy, w, HINT_H, "UI-PANEL-BG")
    c.line(x, hy, x + w, hy, "UI-HAIRLINE")
    c.text("Drag clip to move · Ctrl bypasses snap · double-click to edit · right-click for menu",
           x + 8, hy + HINT_H/2, height=H_META, layer="UI-TEXT-SEC", align="ml")

    # playhead spanning ruler..lanes
    if playhead_bar is not None:
        px = clip_x + (playhead_bar - 1) * PPB
        c.playhead(px, ruler_y, hy)


def draw_lane(c, x, ly, w, clip_x, tr, idx, *, armed=False, recording=False, rec_clip=None):
    col = RGB_track(tr["color"])
    # lane body
    c.fill(clip_x, ly, w - HEADER_W, LANE_H, "UI-CLIP", rgb=(0x22, 0x25, 0x28))
    c.outline(clip_x, ly, w - HEADER_W, LANE_H, rgb=(0x14, 0x16, 0x18))
    # header
    c.fill(x, ly, HEADER_W, LANE_H, "UI-PANEL-BG")
    # colour swatch
    c.swatch(x, ly + 2, 10, LANE_H - 4, col)
    if armed:
        c.fill(x + 10, ly, 2, LANE_H, "UI-RECORD")
    # name (top half)
    c.text(tr["name"], x + 18, ly + LANE_H*0.27, height=H_LABEL, layer="UI-TEXT", align="ml")
    c.text(("MIDI" if tr["type"] == "midi" else "Audio"), x + HEADER_W - 8, ly + LANE_H*0.27,
           height=H_TINY, layer="UI-TEXT-SEC", align="mr")
    # M/S/R (bottom half)
    bw = (HEADER_W - 18 - 8 - 2*4) / 3
    bx = x + 18
    bbot = ly + LANE_H*0.55
    for lbl, st in [("M", tr.get("mute")), ("S", tr.get("solo")), ("R", armed)]:
        c.button(bx, bbot, bw, 20, lbl, on=bool(st), height=H_TINY)
        bx += bw + 4
    # selection outline
    if tr.get("selected"):
        c.outline(x, ly, HEADER_W, LANE_H, "UI-SELECT")
    # clips
    for clip in tr.get("clips", []):
        draw_clip(c, clip_x, ly, clip, tr, col)
    if recording and rec_clip:
        s, l = rec_clip
        cx = clip_x + (s - 1) * PPB
        cw = l * PPB
        c.fill(cx, ly + 3, cw, LANE_H - 6, "UI-CLIP", rgb=(0x52, 0x24, 0x24))
        c.outline(cx, ly + 3, cw, LANE_H - 6, "UI-RECORD")
        c.text("● rec", cx + 6, ly + 14, height=H_TINY, layer="UI-RECORD", align="ml")


def draw_clip(c, clip_x, ly, clip, tr, col):
    s, l, kind = clip["start"], clip["len"], clip.get("kind", tr["type"])
    name = clip.get("name", "")
    cx = clip_x + (s - 1) * PPB
    cw = l * PPB
    cy = ly + 3
    ch = LANE_H - 6
    fill = tuple(int(v*0.55 + 0x1a*0.45) for v in col)  # tint toward shell
    if kind == "midi":
        fill = tuple(int(v*0.5 + 0x2a*0.5) for v in (col[2], col[0], col[1]))  # hue-rotate-ish
    c.fill(cx, cy, cw, ch, "UI-CLIP", rgb=fill)
    c.outline(cx, cy, cw, ch, rgb=(0x10, 0x10, 0x10))
    # name header strip
    c.text(name, cx + 5, cy + 9, height=H_TINY, layer="UI-TEXT", align="ml")
    if kind == "audio":
        c.waveform(cx + 3, cy + 16, cw - 6, ch - 20, seed=int(s*7 + l), density=2.2)
    else:
        draw_mini_notes(c, cx + 3, cy + 18, cw - 6, ch - 22, seed=int(s*5 + l*3))
    if clip.get("selected"):
        c.outline(cx, cy, cw, ch, "UI-SELECT")


def draw_mini_notes(c, x, y, w, h, seed=1):
    import random
    rnd = random.Random(seed)
    rows = 7
    step = max(8, w / 22)
    n = int(w / step)
    for i in range(n):
        p = rnd.randint(0, rows - 1)
        ny = y + (rows - 1 - p) * (h / rows)
        nx = x + i * step
        nw = step * rnd.choice([0.6, 1.0, 1.4])
        c.fill(nx, ny, min(nw, x + w - nx), max(2, h / rows - 1), "UI-MIDI-NOTE",
               rgb=(0xea, 0xc6, 0x8a))


def draw_automation(c, x, ay, w, clip_x, tr):
    ah = 30
    c.fill(x, ay, w, ah, "UI-PANEL-BG")
    c.fill(x, ay, HEADER_W, ah, "UI-PANEL-BG")
    c.outline(x, ay, w, ah, "UI-HAIRLINE")
    c.text("Volume", x + 18, ay + ah/2, height=H_TINY, layer="UI-TEXT-SEC", align="ml")
    # an envelope polyline with breakpoints
    pts = [(clip_x + 0, ay + ah*0.7), (clip_x + 2.0*PPB, ay + ah*0.3),
           (clip_x + 5.0*PPB, ay + ah*0.3), (clip_x + 7.0*PPB, ay + ah*0.6),
           (clip_x + 11*PPB, ay + ah*0.2), (clip_x + (w - HEADER_W), ay + ah*0.2)]
    pts = [(min(px, x + w), py) for px, py in pts]
    c.polyline(pts, layer="UI-ACCENT")
    for px, py in pts[1:-1]:
        c.disc(px, py, 2.4, "UI-ACCENT")

# ============================================================ piano-roll =======

def pianoroll_drawer(c, x, y, w, h):
    gutter = 28
    keyh = 12
    velh = 64
    grid_h = h - velh
    c.fill(x, y, w, h, "UI-PANEL-BG")
    c.line(x, y, x + w, y, "UI-HAIRLINE")
    # keybed gutter
    c.fill(x, y, gutter, grid_h, "UI-SHELL-BG")
    c.line(x + gutter - 1, y, x + gutter - 1, y + grid_h, "UI-HAIRLINE")
    # pitch rows — show a window of pitches (top = high)
    nrows = int(grid_h / keyh)
    base_pitch = 84  # top visible pitch
    blackset = {1, 3, 6, 8, 10}
    for r in range(nrows):
        ry = y + r * keyh
        pitch = base_pitch - r
        if pitch % 12 in blackset:
            c.fill(x + gutter, ry, w - gutter, keyh, "UI-RAISED")
        if pitch % 12 == 0:  # C row -> octave line + label
            c.line(x + gutter, ry, x + w, ry, "UI-HAIRLINE")
            c.text(f"C{pitch//12 - 1}", x + 3, ry + keyh/2, height=8, layer="UI-TEXT-SEC", align="ml")
    # beat lines
    nb = int((w - gutter) / PPBEAT)
    for b in range(nb + 1):
        bx = x + gutter + b * PPBEAT
        heavy = (b % 4 == 0)
        c.line(bx, y, bx, y + grid_h, "UI-HAIRLINE" if heavy else "UI-GHOST")
    # notes (pitch, startbeat, lenbeats, vel)
    notes = [(72, 0, 2, 110), (72, 2, 2, 96), (67, 0, 4, 80), (64, 4, 2, 120),
             (72, 4, 1, 100), (74, 5, 1, 90), (76, 6, 2, 127), (60, 0, 8, 70),
             (79, 8, 1, 64), (77, 9, 1, 72), (76, 10, 2, 100), (72, 8, 4, 88)]
    def py(pitch):
        return y + (base_pitch - pitch) * keyh
    def px(beat):
        return x + gutter + beat * PPBEAT
    for j, (pitch, sb, lb, vel) in enumerate(notes):
        nx, ny = px(sb), py(pitch)
        nw = lb * PPBEAT
        sel = (j == 6)
        c.fill(nx + 0.5, ny + 0.5, nw - 1, keyh - 1, "UI-MIDI-NOTE",
               rgb=RGB["accent"] if sel else (0xc0, 0x82, 0x3e))
        c.outline(nx + 0.5, ny + 0.5, nw - 1, keyh - 1,
                  rgb=RGB["textPrim"] if sel else (0x20, 0x14, 0x04))
    # velocity lane
    vy = y + grid_h
    c.fill(x, vy, w, velh, "UI-PANEL-BG")
    c.line(x, vy, x + w, vy, "UI-HAIRLINE")
    c.text("vel", x + 4, vy + 10, height=8, layer="UI-TEXT-SEC", align="ml")
    for j, (pitch, sb, lb, vel) in enumerate(notes):
        bx = px(sb)
        barh = (vel / 127) * (velh - 12)
        sel = (j == 6)
        c.fill(bx - 2.5, vy + velh - barh, 5, barh, "UI-MIDI-NOTE",
               rgb=RGB["accent"] if sel else (0xb6, 0x7d, 0x3c))
    # playhead
    c.playhead(px(4), y, y + h, head=False)

# ============================================================ inspector ========

def inspector_drawer(c, x, y, w, h):
    c.fill(x, y, w, h, "UI-PANEL-BG")
    c.line(x, y, x + w, y, "UI-HAIRLINE")
    m = 12
    ix, iy = x + m, y + 8
    wave_w = (w) / 2 - 6
    left_w = w - wave_w - 2*m - 12
    # mode tabs (Clip / Device — vision Shift+Tab)
    c.button(x + w - 150, y + 6, 70, 16, "Clip", on=True, height=H_TINY)
    c.button(x + w - 78, y + 6, 70, 16, "Device", height=H_TINY)
    rows = [("Name", "Vox_Lead_T2", "edit"),
            ("Gain", "-3.0 dB", "fader"),
            ("", "Mute", "toggle"),
            ("Fade In", "0.05 s", "fader"),
            ("Fade Out", "0.20 s", "fader")]
    ry = iy + 18
    for title, val, kind in rows:
        if title:
            c.text(title, ix, ry + 12, height=H_TINY, layer="UI-TEXT-SEC", align="ml")
        ex = ix + 64
        ew = left_w - 64
        if kind == "edit":
            c.fill(ex, ry + 2, ew, 20, "UI-RAISED"); c.outline(ex, ry+2, ew, 20, "UI-HAIRLINE")
            c.text(val, ex + 6, ry + 12, height=H_META, layer="UI-TEXT", align="ml")
        elif kind == "fader":
            c.fill(ex, ry + 2, ew, 20, "UI-RAISED"); c.outline(ex, ry+2, ew, 20, "UI-HAIRLINE")
            frac = 0.8 if title == "Gain" else (0.1 if "In" in title else 0.25)
            c.fill(ex, ry + 2, (ew - 60) * frac, 20, "UI-CLIP", rgb=tuple(int(v*0.5) for v in RGB["accent"]))
            c.disc(ex + (ew - 60) * frac, ry + 12, 4, "UI-ACCENT")
            c.fill(ex + ew - 58, ry + 3, 56, 18, "UI-RAISED"); c.outline(ex+ew-58, ry+3, 56, 18, "UI-HAIRLINE")
            c.text(val, ex + ew - 30, ry + 12, height=H_TINY, layer="UI-TEXT", align="mc")
        elif kind == "toggle":
            c.button(ex, ry + 2, 140, 20, "Mute", height=H_TINY)
        ry += 26
    c.text("Start 9 . 1 . 1     Length 4 bars     Offset 0.00 s", ix, ry + 8,
           height=H_TINY, layer="UI-TEXT-SEC", align="ml")
    # waveform panel
    wx = x + w - m - wave_w
    wy = y + 8
    wh = h - 16
    c.fill(wx, wy, wave_w, wh, "UI-SHELL-BG")
    c.outline(wx, wy, wave_w, wh, "UI-HAIRLINE")
    c.waveform(wx + 4, wy + 4, wave_w - 8, wh - 8, seed=42, density=3.0, layer="UI-WAVEFORM")
    # fade handles
    c.polyline([(wx+4, wy+wh-4), (wx+4, wy+4), (wx+40, wy+4)], layer="UI-ACCENT")
    c.polyline([(wx+wave_w-40, wy+4), (wx+wave_w-4, wy+4), (wx+wave_w-4, wy+wh-4)], layer="UI-ACCENT")

# ============================================================ mixer ============

def mixer_region(c, x, y, w, h, tracks, returns=None):
    c.fill(x, y, w, h, "UI-SHELL-BG")
    strip_w = 92
    master_w = 96
    sx = x + 6
    for i, tr in enumerate(tracks):
        channel_strip(c, sx, y, strip_w, h, tr, i)
        sx += strip_w
    if returns:
        sx += 7
        c.line(sx - 4, y + 6, sx - 4, y + h - 6, "UI-HAIRLINE")
        c.text("RETURNS", sx + 2, y - 2, height=H_TINY, layer="UI-TEXT-SEC", align="bl")
        for j, r in enumerate(returns):
            channel_strip(c, sx, y, strip_w, h, r, 100 + j)
            sx += strip_w
    sx += 7
    c.line(sx - 4, y + 6, sx - 4, y + h - 6, "UI-HAIRLINE")
    master_strip(c, sx, y, master_w, h)
    # trailing hint to fill empty board space
    if sx + master_w < x + w - 60:
        c.text("(strips scroll horizontally · master pinned right)",
               sx + master_w + 24, y + 14, height=H_TINY, layer="UI-TEXT-SEC", align="ml")


def channel_strip(c, x, y, w, h, tr, idx, master=False):
    col = RGB["accent"] if master else RGB_track(tr["color"])
    # swatch / accent band
    c.fill(x, y, w, 5, "UI-CLIP", rgb=col)
    # name
    c.fill(x, y + 5, w, 20, "UI-PANEL-BG")
    c.text(tr["name"], x + w/2, y + 15, height=H_TINY,
           layer="UI-ACCENT" if master else "UI-TEXT", align="mc")
    c.line(x + w - 1, y, x + w - 1, y + h, "UI-HAIRLINE")
    cury = y + 25
    # pan knob
    c.knob(x + w/2, cury + 26, 18, value=tr.get("pan", 0.5), label="pan")
    cury += 52
    # insert panel
    inserts = tr.get("inserts", [])
    for j, ins in enumerate(inserts):
        ry = cury + j * 16
        byp = ins.get("bypass")
        c.fill(x + 2, ry, w - 4, 15, "UI-PANEL-BG"); c.outline(x+2, ry, w-4, 15, "UI-HAIRLINE")
        c.circle(x + 8, ry + 7.5, 3, "UI-ACCENT" if not byp else "UI-TEXT-SEC")
        c.text(ins["name"], x + 16, ry + 7.5, height=8,
               layer="UI-TEXT-SEC" if byp else "UI-TEXT", align="ml")
        g_x(c, x + w - 8, ry + 7.5, 2.4, layer="UI-TEXT-SEC")
        if byp:
            c.line(x + 16, ry + 7.5, x + w - 14, ry + 7.5, "UI-TEXT-SEC")
    addy = cury + len(inserts) * 16
    c.fill(x + 2, addy, w - 4, 15, "UI-RAISED"); c.outline(x+2, addy, w-4, 15, "UI-HAIRLINE")
    c.text("+ insert", x + w/2, addy + 7.5, height=8, layer="UI-ACCENT", align="mc")
    # fader region
    fy = addy + 22
    fbot = y + h - 30
    fader_region(c, x, fy, w, fbot - fy, tr.get("vol", 0.72), tr.get("level", 0.55),
                 tr.get("peak", 0.7))
    # M/S
    my = y + h - 28
    bw = (w - 12 - 4) / 2
    c.button(x + 6, my, bw, 22, "M", on=tr.get("mute"), height=H_TINY)
    c.button(x + 6 + bw + 4, my, bw, 22, "S", on=tr.get("solo"), height=H_TINY)


def fader_region(c, x, y, w, h, vol, level, peak, meter_w=8):
    # meter on left, fader on right
    mx = x + 6
    c.meter(mx, y, meter_w, h, level=level, peak=peak)
    # dB ticks
    for f, lbl in [(1.0, "+6"), (0.83, "0"), (0.5, "-12"), (0.16, "-36")]:
        ty = y + (1 - f) * h
        c.text(lbl, mx + meter_w + 2, ty, height=7, layer="UI-TEXT-SEC", align="ml")
    # fader
    fx = x + w - 30
    c.vfader(fx, y, 20, h - 18, pos=vol)
    c.fill(x + 6, y + h - 16, w - 12, 16, "UI-RAISED")
    c.outline(x + 6, y + h - 16, w - 12, 16, "UI-HAIRLINE")
    db = round((vol - 0.83) * 36, 1)
    c.text(f"{db:+.1f} dB", x + w/2, y + h - 8, height=8, layer="UI-TEXT", align="mc")


def master_strip(c, x, y, w, h):
    c.fill(x, y, w, 5, "UI-CLIP", rgb=RGB["accent"])
    c.fill(x, y + 5, w, 20, "UI-PANEL-BG")
    c.text("MASTER", x + w/2, y + 15, height=H_TINY, layer="UI-ACCENT", align="mc")
    c.line(x, y, x, y + h, "UI-HAIRLINE")
    # LUFS readout (vision)
    ly = y + 30
    c.fill(x + 6, ly, w - 12, 30, "UI-PANEL-BG"); c.outline(x+6, ly, w-12, 30, "UI-HAIRLINE")
    c.text("-14.2 LUFS", x + w/2, ly + 10, height=H_TINY, layer="UI-TEXT", align="mc")
    c.text("integrated", x + w/2, ly + 22, height=7, layer="UI-TEXT-SEC", align="mc")
    fy = ly + 38
    fbot = y + h - 30
    # stereo meters
    c.meter(x + 8, fy, 10, fbot - fy, level=0.66, peak=0.8)
    c.meter(x + 20, fy, 10, fbot - fy, level=0.6, peak=0.74)
    c.vfader(x + w - 30, fy, 20, fbot - fy - 18, pos=0.83)
    c.fill(x + 6, fbot - 16, w - 12, 16, "UI-RAISED"); c.outline(x+6, fbot-16, w-12, 16, "UI-HAIRLINE")
    c.text("0.0 dB", x + w/2, fbot - 8, height=8, layer="UI-TEXT", align="mc")
    c.button(x + 6, y + h - 28, w - 12, 22, "Stereo Out", height=8)

# ============================================================ device chain =====

def device_chain_drawer(c, x, y, w, h):
    c.fill(x, y, w, h, "UI-PANEL-BG")
    c.line(x, y, x + w, y, "UI-HAIRLINE")
    c.button(x + w - 150, y + 6, 70, 16, "Clip", height=H_TINY)
    c.button(x + w - 78, y + 6, 70, 16, "Device", on=True, height=H_TINY)
    c.text("TRACK 1 · Drums — device chain", x + 12, y + 14, height=H_META, layer="UI-TEXT-SEC", align="ml")
    devs = [("4OSC", "instrument", ["Osc", "Filter", "Amp", "LFO"]),
            ("EQ Eight", "effect", ["Low", "LoMid", "HiMid", "High"]),
            ("Compressor", "effect", ["Thr", "Ratio", "Atk", "Rel"]),
            ("Reverb", "effect", ["Size", "Decay", "Mix", "HiCut"])]
    dx = x + 12
    dy = y + 30
    dh = h - 42
    for name, kind, knobs in devs:
        dw = 150
        is_inst = (kind == "instrument")
        c.fill(dx, dy, dw, dh, "UI-RAISED")
        c.outline(dx, dy, dw, dh, "UI-ACCENT" if is_inst else "UI-HAIRLINE")
        c.fill(dx, dy, dw, 18, "UI-PANEL-BG")
        c.text(name, dx + 6, dy + 9, height=H_TINY, layer="UI-ACCENT" if is_inst else "UI-TEXT", align="ml")
        c.circle(dx + dw - 9, dy + 9, 3, "UI-ACCENT")  # bypass dot
        # knob grid
        kx0 = dx + 14
        ky0 = dy + 30
        for k, kn in enumerate(knobs):
            kxx = kx0 + (k % 2) * 70
            kyy = ky0 + (k // 2) * 52
            c.knob(kxx + 14, kyy + 16, 14, value=0.3 + 0.5*((k+1)/len(knobs)), label=kn)
        dx += dw + 14
    # add-device slot
    c.fill(dx, dy, 40, dh, "UI-SHELL-BG"); c.outline(dx, dy, 40, dh, "UI-HAIRLINE")
    c.text("+", dx + 20, dy + dh/2, height=H_SECTION, layer="UI-ACCENT", align="mc")


def plugin_window(c, x, y, w, h):
    # floating built-in plugin editor (generated panel)
    c.fill(x - 3, y - 3, w + 6, h + 6, "UI-CLIP", rgb=(0x08, 0x09, 0x0a))  # drop shadow
    c.fill(x, y, w, h, "UI-PANEL-BG")
    c.outline(x, y, w, h, "UI-ACCENT")
    c.fill(x, y, w, 22, "UI-RAISED")
    c.text("Compressor — Track 1 Drums", x + 8, y + 11, height=H_TINY, layer="UI-TEXT", align="ml")
    g_x(c, x + w - 11, y + 11, 3, layer="UI-TEXT-SEC")
    knobs = ["Threshold", "Ratio", "Attack", "Release", "Knee", "Makeup"]
    for k, kn in enumerate(knobs):
        kx = x + 36 + (k % 3) * 80
        ky = y + 56 + (k // 3) * 70
        c.knob(kx, ky, 20, value=0.35 + 0.1*k, label=kn)
    # gain-reduction meter
    c.meter(x + w - 26, y + 32, 12, h - 70, level=0.4, peak=0.5)
    c.text("GR", x + w - 20, y + h - 26, height=7, layer="UI-TEXT-SEC", align="mc")

# ============================================================ session =========
# The PRIMARY surface: an Ableton-style clip grid. Columns = tracks, rows = scenes;
# every cell is a launchable clip slot. Maps to Tracktion's ClipSlot / getClipSlotList()
# / scenes / LaunchHandle. (Net-new UI; the engine support already exists.)

SCENE_NAMES = ["Intro", "Verse A", "Pre", "Chorus", "Verse B", "Bridge", "Drop", "Outro"]


def session_tracks():
    names = ["Drums", "Bass", "Keys", "Vox", "Pads", "Gtr", "Perc", "FX"]
    types = ["audio", "audio", "midi", "audio", "midi", "audio", "audio", "midi"]
    insts = ["", "", "4OSC", "", "4OSC", "", "", "Sampler"]
    # slot matrix: track index -> { scene index : clip name }. Scene 3 (Chorus) is full.
    grid = {
        0: {0: "Beat 1", 1: "Beat 1", 2: "Beat 1", 3: "Beat 2", 4: "Beat 1", 5: "Fill", 6: "Beat 2", 7: "Beat 1"},
        1: {1: "Bass A", 2: "Bass A", 3: "Bass B", 4: "Bass A", 6: "Bass B"},
        2: {1: "Keys A", 2: "Keys A", 3: "Keys B", 4: "Keys A", 5: "Keys C", 6: "Keys B"},
        3: {3: "Lead", 4: "Verse", 5: "Harm", 6: "Lead"},
        4: {0: "Pad", 2: "Pad", 3: "Pad Hi", 4: "Pad", 5: "Pad Lo", 6: "Pad Hi"},
        5: {3: "Riff", 4: "Mute", 6: "Riff"},
        6: {2: "Shaker", 3: "Tamb", 6: "Conga"},
        7: {3: "FX Rise", 5: "Impact"},
    }
    out = []
    for i, nm in enumerate(names):
        out.append({"name": nm, "color": i, "type": types[i], "inst": insts[i],
                    "slots": grid.get(i, {}), "mute": False, "solo": False, "arm": False})
    return out


def scene_clip(tr, s):
    if s in tr["slots"]:
        return {"name": tr["slots"][s], "kind": tr["type"]}
    return None


def session_region(c, x, y, w, h, tracks, *, playing_scene=None, queued_scene=None):
    c.fill(x, y, w, h, "UI-SHELL-BG")
    nT = len(tracks)
    scene_col = 168
    grid_w = w - scene_col
    col_w = grid_w / nT
    header_h = 78
    stop_h = 30
    n_scenes = 16

    # -- track header row --
    for i, tr in enumerate(tracks):
        session_track_header(c, x + i * col_w, y, col_w, header_h, tr)
    # master / scene-launch column header
    mx = x + grid_w
    c.fill(mx, y, scene_col, header_h, "UI-PANEL-BG")
    c.fill(mx, y, scene_col, 5, "UI-CLIP", rgb=RGB["accent"])
    c.text("MASTER", mx + scene_col / 2, y + 18, height=H_TINY, layer="UI-ACCENT", align="mc")
    c.button(mx + scene_col / 2 - 24, y + 30, 48, 22, height=H_TINY)
    g_stop(c, mx + scene_col / 2, y + 41, 6, layer="UI-TEXT-SEC")
    c.text("stop all", mx + scene_col / 2 + 14, y + 41, height=7, layer="UI-TEXT-SEC", align="ml")
    c.text("SCENES", mx + 10, y + header_h - 9, height=7, layer="UI-TEXT-SEC", align="ml")
    c.line(mx, y, mx, y + h, "UI-HAIRLINE")

    # -- scene grid --
    grid_top = y + header_h
    grid_bottom = y + h - stop_h
    sh = (grid_bottom - grid_top) / n_scenes
    for s in range(n_scenes):
        ry = grid_top + s * sh
        name = SCENE_NAMES[s] if s < len(SCENE_NAMES) else None
        is_play = (playing_scene == s)
        is_queue = (queued_scene == s)
        session_scene_launch(c, mx, ry, scene_col, sh, name, is_play, is_queue, s)
        for i, tr in enumerate(tracks):
            session_clip_slot(c, x + i * col_w, ry, col_w, sh, scene_clip(tr, s), tr,
                              playing=is_play, queued=is_queue)
    # faint column separators across the grid
    for i in range(nT + 1):
        cxs = x + i * col_w
        c.line(cxs, grid_top, cxs, grid_bottom, "UI-HAIRLINE")

    # -- clip-stop row (one ■ per track) --
    sy = grid_bottom
    c.fill(x, sy, w, stop_h, "UI-PANEL-BG")
    c.line(x, sy, x + w, sy, "UI-HAIRLINE")
    for i, tr in enumerate(tracks):
        cx = x + i * col_w
        plays = playing_scene is not None and scene_clip(tr, playing_scene) is not None
        c.rect(cx + col_w / 2 - 9, sy + 6, 18, stop_h - 12,
               fill_layer="UI-RAISED", outline_layer="UI-HAIRLINE")
        g_stop(c, cx + col_w / 2, sy + stop_h / 2, 4, layer="UI-ACCENT" if plays else "UI-TEXT-SEC")
        c.line(cx + col_w, sy, cx + col_w, sy + stop_h, "UI-HAIRLINE")
    c.text("Stop Clips", mx + 10, sy + stop_h / 2, height=H_TINY, layer="UI-TEXT-SEC", align="ml")


def session_track_header(c, x, y, w, h, tr):
    col = RGB_track(tr["color"])
    c.fill(x, y, w, h, "UI-PANEL-BG")
    c.fill(x, y, w, 5, "UI-CLIP", rgb=col)
    c.line(x + w, y, x + w, y + h, "UI-HAIRLINE")
    c.text(tr["name"], x + 10, y + 18, height=H_LABEL, layer="UI-TEXT", align="ml")
    c.text("MIDI" if tr["type"] == "midi" else "Audio", x + w - 8, y + 18,
           height=H_TINY, layer="UI-TEXT-SEC", align="mr")
    if tr["inst"]:
        c.fill(x + 10, y + 28, 58, 16, "UI-RAISED")
        c.outline(x + 10, y + 28, 58, 16, "UI-HAIRLINE")
        c.text(tr["inst"], x + 39, y + 36, height=8, layer="UI-ACCENT", align="mc")
    bw = (w - 20 - 2 * 4) / 3
    bx = x + 10
    for lbl, st in [("M", tr.get("mute")), ("S", tr.get("solo")), ("R", tr.get("arm"))]:
        c.button(bx, y + h - 26, bw, 20, lbl, on=bool(st), height=H_TINY)
        bx += bw + 4


def session_scene_launch(c, x, ry, w, h, name, is_play, is_queue, s):
    c.fill(x, ry, w, h, "UI-PANEL-BG")
    c.outline(x, ry, w, h, "UI-HAIRLINE")
    btn = 26
    bx = x + 10
    by = ry + h / 2 - 11
    if is_play:
        c.fill(bx, by, btn, 22, "UI-ACCENT")
        c.outline(bx, by, btn, 22, "UI-ACCENT")
        g_play(c, bx + btn / 2, by + 11, 7, layer="UI-TEXT", rgb=RGB["onAccent"])
    elif is_queue:
        c.fill(bx, by, btn, 22, "UI-RAISED")
        c.outline(bx, by, btn, 22, "UI-ACCENT")
        g_play(c, bx + btn / 2, by + 11, 6, layer="UI-ACCENT")
    else:
        c.fill(bx, by, btn, 22, "UI-RAISED")
        c.outline(bx, by, btn, 22, "UI-HAIRLINE")
        g_play(c, bx + btn / 2, by + 11, 6, layer="UI-TEXT-SEC")
    if name:
        c.text(name, bx + btn + 10, ry + h / 2, height=H_META,
               layer="UI-ACCENT" if (is_play or is_queue) else "UI-TEXT", align="ml")
    else:
        c.text(str(s + 1), x + w - 26, ry + h / 2, height=H_TINY, layer="UI-TEXT-SEC", align="mr")
    if is_queue:
        c.text("queued", x + w - 10, ry + h / 2, height=7, layer="UI-ACCENT", align="mr")


def session_clip_slot(c, x, ry, w, h, clip, tr, *, playing=False, queued=False):
    pad = 3
    sx, sy, sw, sh = x + pad, ry + pad, w - 2 * pad, h - 2 * pad
    if clip is None:
        c.outline(sx, sy, sw, sh, "UI-GHOST")
        c.circle(x + w / 2, ry + h / 2, 4, "UI-GHOST")  # ○ record-enable target
        return
    col = RGB_track(tr["color"])
    fill = tuple(int(v * 0.5 + 0x1a * 0.5) for v in col)
    c.fill(sx, sy, sw, sh, "UI-CLIP", rgb=fill)
    bsz = 18
    bx, by = sx + 4, ry + h / 2 - bsz / 2
    play = playing and clip is not None
    queue = queued and clip is not None
    if play:
        c.outline(sx, sy, sw, sh, "UI-ACCENT")
        c.disc(bx + bsz / 2, by + bsz / 2, 9, "UI-ACCENT", rgb=RGB["accent"])
        g_play(c, bx + bsz / 2 + 1, by + bsz / 2, 6, layer="UI-TEXT", rgb=RGB["onAccent"])
        c.fill(sx, sy + sh - 3, sw * 0.45, 3, "UI-CLIP", rgb=RGB["accent"])  # progress
    elif queue:
        c.outline(sx, sy, sw, sh, "UI-ACCENT")
        c.circle(bx + bsz / 2, by + bsz / 2, 9, "UI-ACCENT")
        g_play(c, bx + bsz / 2, by + bsz / 2, 6, layer="UI-ACCENT")
    else:
        c.outline(sx, sy, sw, sh, rgb=(0x10, 0x10, 0x10))
        g_play(c, bx + bsz / 2, by + bsz / 2, 6, layer="UI-TEXT")
    c.text(clip["name"], bx + bsz + 8, ry + h / 2, height=H_TINY, layer="UI-TEXT", align="ml")


# ============================================================ controllers =====
# How the class of Ableton-style grid controllers drives the Session grid. The SAME
# pad-colour/state model feeds the on-screen grid AND the hardware LEDs. Engine seam:
# tracktion ControlSurface (userLaunchedClip/Scene + padStateChanged LED feedback,
# faders/encoders/transport). Device-agnostic: one driver per controller.

def g_arrow(c, cx, cy, s, dirn, layer="UI-TEXT-SEC"):
    if dirn == "up":
        pts = [(cx - s, cy + s * 0.5), (cx + s, cy + s * 0.5), (cx, cy - s * 0.6)]
    elif dirn == "down":
        pts = [(cx - s, cy - s * 0.5), (cx + s, cy - s * 0.5), (cx, cy + s * 0.6)]
    elif dirn == "left":
        pts = [(cx + s * 0.5, cy - s), (cx + s * 0.5, cy + s), (cx - s * 0.6, cy)]
    else:
        pts = [(cx - s * 0.5, cy - s), (cx - s * 0.5, cy + s), (cx + s * 0.6, cy)]
    c.polyline(pts, layer=layer, close=True)


def cl_pad(c, x, y, w, h, state, col=None):
    """One clip pad in a controller diagram. state: empty/clip/playing/queued/rec."""
    if state == "empty":
        c.fill(x, y, w, h, "UI-CLIP", rgb=(0x1f, 0x21, 0x24))
        c.outline(x, y, w, h, "UI-GHOST")
        return
    if state == "rec":
        c.fill(x, y, w, h, "UI-RECORD")
        c.outline(x, y, w, h, "UI-RECORD")
        return
    base = col or (0x70, 0x78, 0x82)
    if state == "playing":
        c.fill(x, y, w, h, "UI-CLIP", rgb=tuple(min(255, int(v * 1.18)) for v in base))
        c.outline(x, y, w, h, "UI-ACCENT")
        c.outline(x + 2.5, y + 2.5, w - 5, h - 5, "UI-ACCENT")  # double ring ~ pulsing
    elif state == "queued":
        c.fill(x, y, w, h, "UI-CLIP", rgb=tuple(int(v * 0.55) for v in base))
        c.outline(x, y, w, h, "UI-ACCENT")
    else:  # clip present, stopped
        c.fill(x, y, w, h, "UI-CLIP", rgb=base)
        c.outline(x, y, w, h, rgb=(0x10, 0x10, 0x10))


def _grid_state(empties, play_row, queue_row, rec_cell):
    def f(col, row):
        if (col, row) in empties:
            return "empty"
        if rec_cell == (col, row):
            return "rec"
        if row == play_row:
            return "playing"
        if row == queue_row:
            return "queued"
        return "clip"
    return f


def pad_legend(c, x, y, w):
    c.text("PAD COLOUR / STATE  — drives both the on-screen grid and the hardware LEDs",
           x, y, height=H_META, layer="UI-TEXT-SEC", align="ml")
    items = [("empty", "Empty slot", None), ("clip", "Has clip", TRACK_COLORS[3]),
             ("playing", "Playing", TRACK_COLORS[3]), ("queued", "Queued", TRACK_COLORS[3]),
             ("rec", "Rec-arm", None)]
    sx = x
    sy = y + 16
    for state, label, col in items:
        cl_pad(c, sx, sy, 34, 24, state, col)
        c.text(label, sx + 42, sy + 12, height=H_TINY, layer="UI-TEXT-SEC", align="ml")
        sx += 42 + 8 + len(label) * 7 + 26
    # scene glyph
    c.circle(sx + 12, sy + 12, 11, "UI-HAIRLINE")
    g_play(c, sx + 12, sy + 12, 6, layer="UI-ACCENT")
    c.text("Scene launch", sx + 30, sy + 12, height=H_TINY, layer="UI-TEXT-SEC", align="ml")


def draw_launchpad(c, x, y):
    p, g = 42, 8
    grid = 8 * p + 7 * g
    pin, topb = 18, 38
    bodyw = pin + grid + g + p + pin
    bodyh = pin + 16 + topb + g + grid + pin
    c.fill(x, y, bodyw, bodyh, "UI-CLIP", rgb=(0x10, 0x11, 0x13))
    c.outline(x, y, bodyw, bodyh, "UI-HAIRLINE")
    c.text("NOVATION LAUNCHPAD", x + pin, y + 12, height=H_TINY, layer="UI-TEXT-SEC", align="ml")
    gx = x + pin
    gy = y + pin + 16 + topb + g
    # top round buttons: 4 arrows + 4 modes
    modes = ["Sess", "Usr1", "Usr2", "Mix"]
    for i in range(8):
        cxp = gx + i * (p + g) + p / 2
        cyp = y + pin + 16 + topb / 2
        on = (i == 4)  # Session mode lit
        if on:
            c.disc(cxp, cyp, 14, "UI-ACCENT")
        c.circle(cxp, cyp, 14, "UI-ACCENT" if on else "UI-HAIRLINE")
        if i < 4:
            g_arrow(c, cxp, cyp, 7, ["up", "down", "left", "right"][i],
                    layer="UI-TEXT" if i in (1, 3) else "UI-TEXT-SEC")
        else:
            c.text(modes[i - 4], cxp, cyp, height=7,
                   layer="UI-TEXT", align="mc", rgb=RGB["onAccent"] if on else None)
    # 8x8 clip grid
    st = _grid_state({(2, 0), (5, 0), (1, 1), (6, 2), (0, 4), (7, 4), (3, 5), (4, 6), (1, 7)},
                     3, 6, (3, 7))
    for col in range(8):
        for row in range(8):
            cl_pad(c, gx + col * (p + g), gy + row * (p + g), p, p,
                   st(col, row), TRACK_COLORS[col])
    # right scene-launch column
    rx = gx + grid + g + p / 2
    for row in range(8):
        cyp = gy + row * (p + g) + p / 2
        lit = row in (3, 6)
        if lit:
            c.disc(rx, cyp, 14, "UI-ACCENT")
        c.circle(rx, cyp, 14, "UI-ACCENT" if lit else "UI-HAIRLINE")
        g_play(c, rx + 1, cyp, 6, layer="UI-TEXT", rgb=RGB["onAccent"] if lit else None)
    return bodyw, bodyh


def draw_apc40(c, x, y, w):
    pin = 18
    body_h = 470
    c.fill(x, y, w, body_h, "UI-CLIP", rgb=(0x10, 0x11, 0x13))
    c.outline(x, y, w, body_h, "UI-HAIRLINE")
    c.text("AKAI APC40 mkII", x + pin, y + 12, height=H_TINY, layer="UI-TEXT-SEC", align="ml")
    # transport (top-right)
    tgx = x + w - pin - 3 * 26
    for k, gl in enumerate([g_play, g_stop, g_record]):
        cxp = tgx + k * 26 + 13
        c.circle(cxp, y + 16, 10, "UI-HAIRLINE")
        gl(c, cxp, y + 16, 6, layer="UI-RECORD" if gl is g_record else "UI-TEXT-SEC")
    # top: device + track encoder banks (two groups of 8 knobs)
    ky = y + 30
    c.text("DEVICE", x + pin, ky + 4, height=7, layer="UI-TEXT-SEC", align="ml")
    for i in range(8):
        c.knob(x + pin + 26 + i * 40 + 14, ky + 22, 13, value=0.3 + 0.55 * ((i % 4) / 3))
    c.text("TRACK", x + pin + 26 + 8 * 40 + 18, ky + 4, height=7, layer="UI-TEXT-SEC", align="ml")
    for i in range(8):
        c.knob(x + pin + 26 + 8 * 40 + 44 + i * 40 + 14, ky + 22, 13, value=0.4 + 0.4 * ((i % 3) / 2))
    c.text("encoders -> parameterChanged · userMovedAux (sends) · pan",
           x + pin, ky + 44, height=7, layer="UI-TEXT-SEC", align="ml")
    # 8x5 clip grid + scene-launch column
    p, g = 64, 8
    gw = 8 * p + 7 * g
    gx = x + pin
    gy = ky + 56
    ph = 32
    st = _grid_state({(2, 0), (6, 0), (0, 2), (7, 3), (4, 4)}, 1, 3, None)
    for col in range(8):
        for row in range(5):
            cl_pad(c, gx + col * (p + g), gy + row * (ph + g), p, ph,
                   st(col, row), TRACK_COLORS[col])
    # scene-launch column (5)
    rx = gx + gw + g + 14
    for row in range(5):
        cyp = gy + row * (ph + g) + ph / 2
        lit = row in (1, 3)
        if lit:
            c.disc(rx, cyp, 12, "UI-ACCENT")
        c.circle(rx, cyp, 12, "UI-ACCENT" if lit else "UI-HAIRLINE")
        g_play(c, rx + 1, cyp, 5, layer="UI-TEXT", rgb=RGB["onAccent"] if lit else None)
    c.text("scenes", rx - 6, gy - 8, height=7, layer="UI-TEXT-SEC", align="ml")
    c.text("8x5 clip grid -> userLaunchedClip ↔ padStateChanged (LED)",
           gx, gy + 5 * (ph + g) + 4, height=7, layer="UI-TEXT-SEC", align="ml")
    # track-button row
    tby = gy + 5 * (ph + g) + 18
    for col in range(8):
        c.rect(gx + col * (p + g) + p / 2 - 9, tby, 18, 14,
               fill_layer="UI-RAISED", outline_layer="UI-HAIRLINE")
        g_stop(c, gx + col * (p + g) + p / 2, tby + 7, 3.5, layer="UI-TEXT-SEC")
    c.text("clip-stop / mute / solo / rec -> userStoppedClip · userPressedMute/Solo/RecEnable",
           gx, tby + 24, height=7, layer="UI-TEXT-SEC", align="ml")
    # fader row (8 + master) + crossfader
    fy = tby + 36
    fh = body_h - (fy - y) - pin
    fw = 22
    for i in range(8):
        c.vfader(gx + i * (p + g) + p / 2 - fw / 2, fy, fw, fh, pos=0.45 + 0.12 * ((i % 4)))
    mxf = gx + gw + g
    c.vfader(mxf - 4, fy, fw, fh, pos=0.7)
    c.text("MST", mxf + 6, fy + fh + 2, height=7, layer="UI-ACCENT", align="ml")
    c.text("faders -> userMovedFader  (+ userMovedMasterLevelFader)",
           gx, fy + fh + 12, height=7, layer="UI-TEXT-SEC", align="ml")
    return body_h


def screen_controllers(c):
    c.fill(0, 0, WIN_W, WIN_H, "UI-SHELL-BG")
    c.text("REFERENCE — REAL hardware you connect over MIDI.  Forge does NOT draw a controller on screen;"
           "  the on-screen surface is the Session grid (sheet 00).  This sheet maps hardware → engine.",
           40, 16, height=H_META, layer="UI-ACCENT", align="ml")
    pad_legend(c, 40, 46, WIN_W - 80)
    lw, lh = draw_launchpad(c, 40, 126)
    draw_apc40(c, 40 + lw + 50, 166, WIN_W - (40 + lw + 50) - 40)
    # captions under the launchpad
    cap_y = 126 + lh + 16
    c.text("Pad grids (Launchpad): 8×8 pads = a scrollable clip-slot viewport.",
           40, cap_y, height=H_META, layer="UI-TEXT", align="ml")
    c.text("Pads → userLaunchedClip(track, scene)   ·   ▶ column → userLaunchedScene",
           40, cap_y + 20, height=H_TINY, layer="UI-TEXT-SEC", align="ml")
    c.text("▲▼◀▶ → userScrolledTracks* · userChangedPadBanks   ·   top row → Session/User/Mixer modes",
           40, cap_y + 36, height=H_TINY, layer="UI-TEXT-SEC", align="ml")
    # engine-seam band
    by = WIN_H - 96
    c.line(40, by, WIN_W - 40, by, "UI-HAIRLINE")
    c.text("ONE DEVICE-AGNOSTIC LAYER — Tracktion ControlSurface seam; a driver per controller",
           40, by + 16, height=H_META, layer="UI-ACCENT", align="ml")
    c.text("Pad grids use the pad/nav subset; full surfaces (APC40) add faders + encoders + transport on the SAME seam.",
           40, by + 36, height=H_TINY, layer="UI-TEXT-SEC", align="ml")
    c.text("device → engine:  userLaunchedClip/Scene · userStoppedClip · userScrolledTracks* · userMovedFader · userMovedPanPot/Aux · userPressedMute/Solo/RecEnable · userPressedPlay/Stop/Record",
           40, by + 54, height=H_TINY, layer="UI-TEXT-SEC", align="ml")
    c.text("engine → device (feedback):  padStateChanged(track, scene, colour, state) · clipsPlayingStateChanged · moveFader · parameterChanged · channelLevelChanged   +   MIDI-learn (MidiLearn) · clock / Ableton Link",
           40, by + 70, height=H_TINY, layer="UI-TEXT-SEC", align="ml")


# ============================================================ screens ==========

def demo_tracks(n=6, mixer=False):
    names = ["Drums", "Bass", "Keys", "Vox Lead", "Pads", "Gtr", "Perc", "FX"]
    out = []
    for i in range(n):
        t = {"name": names[i], "color": i, "type": "midi" if i in (2, 4) else "audio",
             "mute": False, "solo": False, "arm": False, "pan": 0.5 + (i-3)*0.08,
             "vol": 0.6 + 0.05*((i % 4)), "level": 0.3 + 0.12*(i % 4), "peak": 0.5 + 0.08*(i%4)}
        # clips
        clips = []
        if i == 0:
            clips = [{"start": 1, "len": 4, "name": "Drums A"}, {"start": 5, "len": 8, "name": "Drums B"}]
        elif i == 1:
            clips = [{"start": 3, "len": 6, "name": "Bass"}, {"start": 9, "len": 4, "name": "Bass2"}]
        elif i == 2:
            clips = [{"start": 1, "len": 4, "name": "Keys", "kind": "midi"},
                     {"start": 7, "len": 6, "name": "Keys2", "kind": "midi"}]
        elif i == 3:
            clips = [{"start": 5, "len": 4, "name": "Vox V1"}, {"start": 9, "len": 4, "name": "Vox V2"}]
        elif i == 4:
            clips = [{"start": 2, "len": 10, "name": "Pad", "kind": "midi"}]
        else:
            clips = [{"start": 1, "len": 5, "name": "Gtr"}, {"start": 8, "len": 5, "name": "Gtr2"}]
        t["clips"] = clips
        if mixer:
            t["inserts"] = []
        out.append(t)
    return out


def mixer_tracks():
    base = demo_tracks(8, mixer=True)
    base[0]["inserts"] = [{"name": "EQ Eight"}, {"name": "Comp"}, {"name": "Glue", "bypass": True}]
    base[1]["inserts"] = [{"name": "EQ Eight"}, {"name": "Saturator"}]
    base[2]["inserts"] = [{"name": "4OSC"}, {"name": "Reverb"}]
    base[3]["inserts"] = [{"name": "DeEsser"}, {"name": "Comp"}, {"name": "Delay"}]
    base[4]["inserts"] = [{"name": "4OSC"}, {"name": "Chorus"}]
    base[5]["inserts"] = [{"name": "Amp"}]
    base[6]["inserts"] = [{"name": "Gate"}]
    base[7]["inserts"] = [{"name": "Reverb"}]
    base[1]["mute"] = True
    base[3]["solo"] = True
    return base


# ---- the eight states -------------------------------------------------------

def screen_session(c):
    control_bar(c, view="session", playing=True, timecode="00:00:12.000", bbt="Chorus")
    session_region(c, 0, CB_H, WIN_W, WIN_H - CB_H - ST_H, session_tracks(),
                   playing_scene=3, queued_scene=6)
    status_strip(c, "Song_Idea_04.tracktionedit  ·  8 tracks  ·  Session  ·  scene 'Chorus' playing · 'Drop' queued",
                 "Launch Quantize  1 bar")


def screen_arrange_default(c):
    control_bar(c, view="arrange", timecode="00:00:00.000", bbt="1 . 1 . 1")
    arrange_region(c, 0, CB_H, WIN_W, WIN_H - CB_H - ST_H, demo_tracks(6),
                   playhead_bar=3.4, marker_lane=True)
    status_strip(c, "Untitled.tracktionedit  ·  6 tracks  ·  Focusrite 2i2  ·  48 kHz / 24-bit  ·  stopped",
                 "Snap 1/16   |   CPU 6%")


def screen_compose(c):
    control_bar(c, view="arrange", browser_on=True, drawer_on=True, timecode="00:00:05.812", bbt="3 . 2 . 1")
    bw = 240
    drawer_h = 300
    work_top = CB_H
    work_bot = WIN_H - ST_H
    browser_region(c, 0, work_top, bw, work_bot - work_top, tab="browse")
    arrange_region(c, bw, work_top, WIN_W - bw, (work_bot - work_top) - drawer_h,
                   demo_tracks(4), playhead_bar=3.4, marker_lane=True)
    # the live-edited MIDI clip is selected
    pianoroll_drawer(c, bw, work_bot - drawer_h, WIN_W - bw, drawer_h)
    status_strip(c, "Song_Idea_04.tracktionedit  ·  4 tracks  ·  editing MIDI clip 'Keys'  ·  playing",
                 "Snap 1/16   |   12 notes")


def screen_audio_edit(c):
    tr = demo_tracks(6)
    tr[3]["selected"] = True
    tr[3]["clips"][0]["selected"] = True
    control_bar(c, view="arrange", drawer_on=True, timecode="00:00:09.250", bbt="5 . 1 . 1")
    drawer_h = 200
    work_top = CB_H
    work_bot = WIN_H - ST_H
    arrange_region(c, 0, work_top, WIN_W, (work_bot - work_top) - drawer_h, tr,
                   playhead_bar=5.0, marker_lane=True)
    inspector_drawer(c, 0, work_bot - drawer_h, WIN_W, drawer_h)
    status_strip(c, "Song_Idea_04.tracktionedit  ·  6 tracks  ·  clip 'Vox V1' selected  ·  stopped",
                 "Inspector")


def screen_mixer(c):
    control_bar(c, view="mixer", timecode="00:00:00.000", bbt="1 . 1 . 1")
    returns = [
        {"name": "A · Reverb", "color": 7, "pan": 0.5, "vol": 0.70, "level": 0.30, "peak": 0.5,
         "inserts": [{"name": "Reverb"}]},
        {"name": "B · Delay", "color": 5, "pan": 0.5, "vol": 0.66, "level": 0.24, "peak": 0.42,
         "inserts": [{"name": "Delay"}, {"name": "Filter"}]},
    ]
    mixer_region(c, 0, CB_H, WIN_W, WIN_H - CB_H - ST_H, mixer_tracks(), returns=returns)
    status_strip(c, "Song_Idea_04.tracktionedit  ·  8 tracks + 2 returns  ·  Bass muted · Vox soloed  ·  stopped",
                 "Master  -14.2 LUFS")


def screen_recording(c):
    tr = demo_tracks(6)
    tr[3]["arm"] = True
    tr[3]["selected"] = True
    control_bar(c, view="arrange", playing=True, recording=True, timecode="00:00:11.480",
                bbt="6 . 3 . 2", rec_label="RECORDING    00:11.4")
    arrange_region(c, 0, CB_H, WIN_W, WIN_H - CB_H - ST_H, tr,
                   playhead_bar=6.7, marker_lane=True, recording_track=3, rec_clip=(5, 1.7))
    status_strip(c, "Song_Idea_04.tracktionedit  ·  6 tracks  ·  ● RECORDING to 'Vox Lead'  ·  Focusrite 2i2 In 1",
                 "REC 00:11.4")


def screen_device_chain(c):
    tr = demo_tracks(6)
    tr[0]["selected"] = True
    control_bar(c, view="arrange", drawer_on=True, timecode="00:00:00.000", bbt="1 . 1 . 1")
    drawer_h = 250
    work_top = CB_H
    work_bot = WIN_H - ST_H
    arrange_region(c, 0, work_top, WIN_W, (work_bot - work_top) - drawer_h, tr,
                   playhead_bar=1.0, marker_lane=True)
    device_chain_drawer(c, 0, work_bot - drawer_h, WIN_W, drawer_h)
    plugin_window(c, WIN_W - 360, CB_H + 60, 300, 230)
    status_strip(c, "Song_Idea_04.tracktionedit  ·  track 'Drums' device chain  ·  Compressor editor open",
                 "Device")


def screen_anatomy(c):
    from anatomy import draw_anatomy
    draw_anatomy(c, WIN_W, WIN_H)


def screen_overlays(c):
    from overlays import draw_overlays
    draw_overlays(c, WIN_W, WIN_H)


SCREENS = [
    ("00", "Session — Clip Grid (primary)", "tracks × scenes of launchable clips · the PRIMARY surface · 'Chorus' playing, 'Drop' queued", screen_session),
    ("01", "Arrange — default (lean)", "Browser + Drawer collapsed · the clean default surface", screen_arrange_default),
    ("02", "Compose — Browser + Piano-roll", "left Browser open · MIDI clip live-edited in the bottom drawer", screen_compose),
    ("03", "Audio clip — Inspector drawer", "audio clip selected · name/gain/mute/fades/waveform", screen_audio_edit),
    ("04", "Mixer view", "channel strips · inserts · pan/fader/meter · master + LUFS", screen_mixer),
    ("05", "Recording", "armed track · red transport · take growing under the playhead", screen_recording),
    ("06", "Device chain + plugin window", "drawer Device mode · 4OSC→EQ→Comp→Reverb · floating editor", screen_device_chain),
    ("07", "Anatomy & theme spec", "dimensioned components · palette legend · CAD layer key", screen_anatomy),
    ("08", "Overlays & modals", "context menu · snap dropdown · colour palette · Preferences", screen_overlays),
    ("09", "Controllers — hardware mapping (reference)", "REAL hardware (Launchpad / APC40 mkII) connected over MIDI — NOT an app screen · maps the grid to the ControlSurface seam", screen_controllers),
]
