"""anatomy.py — dimensioned component spec sheet + palette legend + CAD layer key."""
from forge_ui import RGB, LAYERS, TRACK_COLORS, H_TITLE, H_SECTION, H_LABEL, H_META, H_TINY

PALETTE_ROWS = [
    ("shell",    "shellBg",   "#1A1C1E", "UI-SHELL-BG",  "window / shell"),
    ("panel",    "panelBg",   "#232629", "UI-PANEL-BG",  "docked panels, headers"),
    ("raised",   "raisedBg",  "#2D3135", "UI-RAISED",    "raised controls / buttons"),
    ("textPrim", "textPrim",  "#D6D9DC", "UI-TEXT",      "primary text"),
    ("textSec",  "textSec",   "#8A9095", "UI-TEXT-SEC",  "labels / metadata"),
    ("hairline", "hairline",  "#34383C", "UI-HAIRLINE",  "separators / hairlines"),
    ("accent",   "accent",    "#E0902F", "UI-ACCENT",    "playhead · arm · selection · focus"),
    ("onAccent", "onAccent",  "#241600", "(on accent)",  "text drawn on the amber fill"),
    ("record",   "recordRed", "#E24B4A", "UI-RECORD",    "active recording · clipping (reserved)"),
]


def section(c, x, y, w, title):
    c.text(title, x, y, height=H_SECTION, layer="UI-ACCENT", align="ml")
    c.line(x, y + 11, x + w, y + 11, "UI-HAIRLINE")


def draw_anatomy(c, W, H):
    c.fill(0, 0, W, H, "UI-SHELL-BG")
    c.text("FORGE — UI ANATOMY · THEME · CAD LAYER KEY", 40, 34, height=H_TITLE, layer="UI-ACCENT", align="ml")
    c.text("1 drawing unit = 1 logical UI pixel · all dimensions trace the source (ForgeShell / ArrangeView / MixerView / PianoRollView / DetailView)",
           40, 62, height=H_META, layer="UI-TEXT-SEC", align="ml")

    # ============================ BAND A — palette + layers =================
    section(c, 40, 96, 560, "PALETTE  →  CAD LAYER")
    ry = 122
    for key, name, hexv, layer, desc in PALETTE_ROWS:
        c.swatch(40, ry, 50, 22, RGB[key])
        c.text(hexv, 100, ry + 6, height=H_META, layer="UI-TEXT", align="ml")
        c.text(name, 178, ry + 6, height=H_META, layer="UI-TEXT", align="ml")
        c.text(layer, 100, ry + 18, height=H_TINY, layer="UI-TEXT-SEC", align="ml")
        c.text(desc, 270, ry + 11, height=H_TINY, layer="UI-TEXT-SEC", align="ml")
        ry += 29

    section(c, 640, 96, 920, "DRAWING LAYERS  (toggle / freeze categories in CAD)")
    ly = 122
    col = 0
    for i, (name, key, lw) in enumerate(LAYERS):
        cx = 640 + col * 310
        c.swatch(cx, ly, 18, 12, RGB[key])
        c.text(name, cx + 26, ly + 6, height=H_TINY, layer="UI-TEXT", align="ml")
        ly += 18
        if i == 8:
            col = 1
            ly = 122
    c.text("Freeze the *-BG fill layers → clean wireframe.   Each layer carries its true-colour:",
           640, 312, height=H_TINY, layer="UI-TEXT-SEC", align="ml")
    c.text("edit one layer colour to re-theme everything (mirrors ForgeLookAndFeel colour IDs).",
           640, 328, height=H_TINY, layer="UI-TEXT-SEC", align="ml")

    # control bar anatomy (full width)
    section(c, 640, 360, 920, "CONTROL BAR  ·  46 px tall, full width")
    cby = 388
    bx0 = 640
    bw0 = 920
    c.fill(bx0, cby, bw0, 46, "UI-PANEL-BG"); c.line(bx0, cby + 46, bx0 + bw0, cby + 46, "UI-HAIRLINE")
    bx = bx0 + 6
    c.button(bx, cby + 6, 60, 34, "Browser", height=H_TINY); bx += 66
    for nm in ["New", "Open", "Save", "…"]:
        c.button(bx, cby + 6, 48, 34, nm, height=H_TINY); bx += 51
    c.text("file cmds", bx + 2, cby + 23, height=H_TINY, layer="UI-TEXT-SEC", align="ml")
    from screens import g_play, g_stop, g_record, g_loop, g_x
    tx = bx0 + 360
    for gl, col_ in [(g_play, "UI-TEXT"), (g_stop, "UI-TEXT"), (g_record, "UI-RECORD"), (g_loop, "UI-TEXT")]:
        c.button(tx, cby + 6, 50, 34, height=H_TINY)
        gl(c, tx + 25, cby + 23, 10, layer=col_)
        tx += 54
    c.fill(tx, cby + 6, 190, 34, "UI-RAISED"); c.outline(tx, cby+6, 190, 34, "UI-HAIRLINE")
    c.text("00:00:05.812", tx + 10, cby + 23, height=H_LABEL, layer="UI-TEXT", align="ml")
    c.text("3 . 2 . 1", tx + 182, cby + 23, height=H_LABEL, layer="UI-ACCENT", align="mr")
    ex = bx0 + bw0 - 6 - 56
    c.button(ex, cby + 6, 56, 34, "Editor", height=H_TINY)
    c.button(ex - 10 - 48, cby + 6, 48, 34, "Mix", height=H_TINY)
    c.button(ex - 10 - 48 - 62, cby + 6, 62, 34, "Arrange", on=True, height=H_TINY)
    c.dim_v(cby, cby + 46, bx0 - 12, "46")
    c.callout(tx + 95, cby + 6, tx + 60, cby - 14, "transport · timecode + bars|beats", align="mr")
    c.callout(ex + 28, cby + 40, ex + 70, cby + 66, "view-switch + drawer toggle", align="ml")

    # ============================ BAND B — components to scale ==============
    c.text("COMPONENTS  (drawn to scale)", 40, 470, height=H_SECTION, layer="UI-ACCENT", align="ml")
    c.line(40, 482, 1560, 482, "UI-HAIRLINE")

    # ---- track lane ----
    lx, lyy = 40, 520
    c.text("ARRANGE TRACK LANE", lx, lyy, height=H_META, layer="UI-TEXT", align="ml")
    laneY = lyy + 24
    H_LANE, HDR = 76, 150
    c.fill(lx, laneY, 520, H_LANE, "UI-CLIP", rgb=(0x22, 0x25, 0x28))
    c.fill(lx, laneY, HDR, H_LANE, "UI-PANEL-BG")
    c.swatch(lx, laneY + 2, 10, H_LANE - 4, TRACK_COLORS[0])
    c.text("Drums", lx + 18, laneY + 20, height=H_LABEL, layer="UI-TEXT", align="ml")
    c.text("Audio", lx + HDR - 8, laneY + 20, height=H_TINY, layer="UI-TEXT-SEC", align="mr")
    bw = (HDR - 18 - 8 - 8) / 3
    bxx = lx + 18
    for lbl in ["M", "S", "R"]:
        c.button(bxx, laneY + 44, bw, 20, lbl, height=H_TINY); bxx += bw + 4
    c.fill(lx + HDR + 24, laneY + 3, 320, H_LANE - 6, "UI-CLIP", rgb=(0x6a, 0x46, 0x2a))
    c.outline(lx + HDR + 24, laneY + 3, 320, H_LANE - 6, rgb=(0x10, 0x10, 0x10))
    c.text("Drums A", lx + HDR + 30, laneY + 12, height=H_TINY, layer="UI-TEXT", align="ml")
    c.waveform(lx + HDR + 28, laneY + 20, 312, H_LANE - 26, seed=3)
    c.dim_v(laneY, laneY + H_LANE, lx - 12, "76")
    c.dim_h(lx, lx + HDR, laneY + H_LANE + 24, "150  header")
    c.callout(lx + 5, laneY + H_LANE - 6, lx - 30, laneY + H_LANE + 58, "10px colour swatch", align="ml")
    c.callout(lx + 18 + bw*1.5, laneY + 54, lx + 150, laneY + H_LANE + 44, "M / S / R · arm = engine-derived", align="ml")
    c.callout(lx + HDR + 180, laneY + 40, lx + HDR + 250, laneY + H_LANE + 30, "clip · waveform @0.85, colour @0.55", align="ml")

    # ---- mixer strip ----
    sx, syy = 640, 520
    c.text("MIXER STRIP", sx, syy, height=H_META, layer="UI-TEXT", align="ml")
    st_y = syy + 24
    SW, SH = 92, 360
    c.fill(sx, st_y, SW, SH, "UI-SHELL-BG")
    c.fill(sx, st_y, SW, 5, "UI-CLIP", rgb=TRACK_COLORS[0])
    c.fill(sx, st_y + 5, SW, 20, "UI-PANEL-BG"); c.text("Drums", sx + SW/2, st_y + 15, height=H_TINY, layer="UI-TEXT", align="mc")
    c.knob(sx + SW/2, st_y + 51, 18, value=0.5, label="pan")
    iy = st_y + 77
    for nm in ["EQ Eight", "Comp", "Glue"]:
        c.fill(sx + 2, iy, SW - 4, 15, "UI-PANEL-BG"); c.outline(sx+2, iy, SW-4, 15, "UI-HAIRLINE")
        c.circle(sx + 8, iy + 7.5, 3, "UI-ACCENT")
        c.text(nm, sx + 16, iy + 7.5, height=8, layer="UI-TEXT", align="ml")
        iy += 16
    c.fill(sx + 2, iy, SW - 4, 15, "UI-RAISED"); c.text("+ insert", sx + SW/2, iy + 7.5, height=8, layer="UI-ACCENT", align="mc")
    fy = iy + 22
    c.meter(sx + 6, fy, 8, st_y + SH - 30 - fy, level=0.6, peak=0.78)
    c.vfader(sx + SW - 30, fy, 20, st_y + SH - 48 - fy, pos=0.74)
    c.button(sx + 6, st_y + SH - 26, (SW-16)/2, 20, "M", height=H_TINY)
    c.button(sx + 6 + (SW-16)/2 + 4, st_y + SH - 26, (SW-16)/2, 20, "S", height=H_TINY)
    c.dim_h(sx, sx + SW, st_y + SH + 22, "92")
    c.callout(sx + SW/2, st_y + 3, sx + SW + 26, st_y - 6, "5px colour band", align="ml")
    c.callout(sx + SW/2, st_y + 51, sx + SW + 26, st_y + 46, "rotary pan · 52px", align="ml")
    c.callout(sx + 10, fy + 70, sx + SW + 26, fy + 86, "8px meter · −60..+6 dB", align="ml")
    c.callout(sx + SW - 20, fy + 140, sx + SW + 26, fy + 156, "dB fader · 2×click = unity", align="ml")

    # ---- piano-roll ----
    kx, kyy = 900, 520
    c.text("PIANO-ROLL  ·  keybed + grid", kx, kyy, height=H_META, layer="UI-TEXT", align="ml")
    ky0 = kyy + 24
    GUT, KH = 28, 12
    rows, base = 26, 84
    gw = 320
    blackset = {1, 3, 6, 8, 10}
    for r in range(rows):
        ry2 = ky0 + r * KH
        pitch = base - r
        if pitch % 12 in blackset:
            c.fill(kx + GUT, ry2, gw - GUT, KH, "UI-RAISED")
        if pitch % 12 == 0:
            c.line(kx + GUT, ry2, kx + gw, ry2, "UI-HAIRLINE")
    for b in range(0, 13):  # beat lines
        bxx = kx + GUT + b * ((gw - GUT) / 12)
        c.line(bxx, ky0, bxx, ky0 + rows*KH, "UI-HAIRLINE" if b % 4 == 0 else "UI-GHOST")
    c.fill(kx, ky0, GUT, rows * KH, "UI-SHELL-BG")
    c.line(kx + GUT - 1, ky0, kx + GUT - 1, ky0 + rows*KH, "UI-HAIRLINE")
    for r in range(rows):
        ry2 = ky0 + r * KH
        pitch = base - r
        if pitch % 12 == 0:
            c.text(f"C{pitch//12 - 1}", kx + 3, ry2 + KH/2, height=8, layer="UI-TEXT-SEC", align="ml")
    c.fill(kx + GUT + 40, ky0 + 5*KH + 0.5, 80, KH - 1, "UI-MIDI-NOTE", rgb=RGB["accent"])
    c.outline(kx + GUT + 40, ky0 + 5*KH + 0.5, 80, KH - 1, rgb=RGB["textPrim"])
    c.fill(kx + GUT + 40, ky0 + 9*KH + 0.5, 56, KH - 1, "UI-MIDI-NOTE", rgb=(0xc0, 0x82, 0x3e))
    c.fill(kx + GUT + 130, ky0 + 7*KH + 0.5, 44, KH - 1, "UI-MIDI-NOTE", rgb=(0xc0, 0x82, 0x3e))
    c.outline(kx, ky0, gw, rows * KH, "UI-HAIRLINE")
    c.dim_h(kx, kx + GUT, ky0 + rows*KH + 22, "28")
    c.dim_v(ky0, ky0 + KH, kx + gw + 16, "12")
    c.callout(kx + GUT + 80, ky0 + 5*KH + 6, kx + gw + 26, ky0 + 5*KH - 16, "note · selected (opaque + light stroke)", align="ml")
    c.callout(kx + 14, ky0 + 12*KH, kx - 4, ky0 + rows*KH + 24, "keybed gutter · C-1…C9", align="ml")

    # ---- legend column ----
    gx, gyy = 1290, 520
    c.text("STATES & GLYPHS", gx, gyy, height=H_META, layer="UI-TEXT", align="ml")
    gy = gyy + 28
    # clip states
    for label, rgb, stroke in [("clip idle", (0x6a, 0x46, 0x2a), (0x10, 0x10, 0x10)),
                               ("clip selected", (0x6a, 0x46, 0x2a), RGB["accent"]),
                               ("recording", (0x52, 0x24, 0x24), RGB["record"])]:
        c.fill(gx, gy, 60, 22, "UI-CLIP", rgb=rgb)
        c.outline(gx, gy, 60, 22, rgb=stroke)
        c.text(label, gx + 70, gy + 11, height=H_TINY, layer="UI-TEXT-SEC", align="ml")
        gy += 28
    # transport glyph legend
    gy += 6
    from screens import g_play, g_stop, g_record, g_loop
    for gl, lbl in [(g_play, "play"), (g_stop, "stop"), (g_record, "record (red)"), (g_loop, "loop")]:
        c.fill(gx, gy, 26, 22, "UI-RAISED"); c.outline(gx, gy, 26, 22, "UI-HAIRLINE")
        gl(c, gx + 13, gy + 11, 8, layer="UI-RECORD" if "record" in lbl else "UI-TEXT")
        c.text(lbl, gx + 36, gy + 11, height=H_TINY, layer="UI-TEXT-SEC", align="ml")
        gy += 26
    # marker + playhead
    gy += 6
    c.polyline([(gx, gy+2), (gx+8, gy+2), (gx, gy+12)], layer="UI-ACCENT", close=True)
    c.text("marker flag", gx + 16, gy + 7, height=H_TINY, layer="UI-TEXT-SEC", align="ml")
    gy += 22
    c.playhead(gx + 4, gy, gy + 16)
    c.text("playhead", gx + 16, gy + 8, height=H_TINY, layer="UI-TEXT-SEC", align="ml")

    # ---------------- bottom note ----------------
    c.text("●  shipped today        ○  envisioned (markers · automation lanes · sends/returns · LUFS · device-chain drawer · Inspect tab · Preferences)",
           40, H - 34, height=H_META, layer="UI-TEXT-SEC", align="ml")
