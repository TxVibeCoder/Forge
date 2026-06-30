"""overlays.py — non-persistent surfaces: context menu, snap dropdown, colour palette, Preferences."""
from forge_ui import RGB, TRACK_COLORS, H_TITLE, H_SECTION, H_LABEL, H_META, H_TINY


def card(c, x, y, w, h, *, accent=False):
    c.fill(x - 3, y - 3, w + 6, h + 6, "UI-CLIP", rgb=(0x08, 0x09, 0x0a))  # shadow
    c.fill(x, y, w, h, "UI-RAISED")
    c.outline(x, y, w, h, "UI-ACCENT" if accent else "UI-HAIRLINE")


def menu(c, x, y, w, items, title=None):
    rh = 22
    h = rh * len(items) + (18 if title else 0) + 8
    card(c, x, y, w, h)
    cy = y + 6
    if title:
        c.text(title, x + 12, cy + 9, height=H_TINY, layer="UI-TEXT-SEC", align="ml")
        c.line(x + 6, cy + 18, x + w - 6, cy + 18, "UI-HAIRLINE")
        cy += 20
    for label, hot, sub in items:
        if label == "—":
            c.line(x + 6, cy + rh/2, x + w - 6, cy + rh/2, "UI-HAIRLINE")
            cy += rh
            continue
        if hot == "*":  # highlighted row
            c.fill(x + 3, cy, w - 6, rh, "UI-ACCENT")
            c.text(label, x + 14, cy + rh/2, height=H_META, layer="UI-TEXT", align="ml", rgb=RGB["onAccent"])
        else:
            c.text(label, x + 14, cy + rh/2, height=H_META, layer="UI-TEXT", align="ml")
            if hot:
                c.text(hot, x + w - 12, cy + rh/2, height=H_TINY, layer="UI-TEXT-SEC", align="mr")
        if sub:
            from screens import g_tri
            g_tri(c, x + w - 12, cy + rh/2, 3, up=False, layer="UI-TEXT-SEC")
        cy += rh


def draw_overlays(c, W, H):
    from screens import control_bar, arrange_region, status_strip, demo_tracks, WIN_W, WIN_H, CB_H, ST_H
    # ghosted backdrop
    control_bar(c, view="arrange", drawer_on=False)
    arrange_region(c, 0, CB_H, W, H - CB_H - ST_H, demo_tracks(5), playhead_bar=4.2, marker_lane=True)
    status_strip(c, "Non-persistent surfaces — context menus · dropdowns · call-outs · modal dialogs", "Overlays")

    # 1) right-click clip context menu
    menu(c, 250, 250, 210, [
        ("Rename", "F2", False), ("Cut", "Ctrl+X", False), ("Copy", "Ctrl+C", False),
        ("Duplicate", "Ctrl+D", False), ("Delete", "Del", False), ("—", None, False),
        ("Reverse", None, False), ("Consolidate", None, False), ("—", None, False),
        ("Colour", None, True), ("Clip Gain…", None, False),
    ], title="CLIP  ·  Vox V1")

    # 2) snap-division dropdown (open)
    menu(c, 520, 250, 150, [
        ("Off", None, False), ("Bar", None, False), ("1/2", None, False),
        ("1/4", None, False), ("1/8", None, False), ("1/16", "*", False), ("1/32", None, False),
    ], title="SNAP")

    # 3) colour palette call-out (grid)
    cgx, cgy = 250, 560
    card(c, cgx, cgy, 196, 120, accent=True)
    c.text("CLIP COLOUR", cgx + 12, cgy + 14, height=H_TINY, layer="UI-TEXT-SEC", align="ml")
    sw = 28
    for i, col in enumerate(TRACK_COLORS):
        gx = cgx + 12 + (i % 4) * (sw + 6)
        gy = cgy + 26 + (i // 4) * (sw + 6)
        c.swatch(gx, gy, sw, sw, col)
        if i == 0:
            c.outline(gx - 2, gy - 2, sw + 4, sw + 4, "UI-ACCENT")
    c.text("clip colour = track colour", cgx + 12, cgy + 110, height=H_TINY, layer="UI-TEXT-SEC", align="ml")

    # 4) Preferences (tabbed, vision)
    dx, dy, dw, dh = 720, 360, 540, 320
    card(c, dx, dy, dw, dh, accent=True)
    c.fill(dx, dy, dw, 30, "UI-PANEL-BG")
    c.text("Preferences", dx + 14, dy + 15, height=H_LABEL, layer="UI-TEXT", align="ml")
    from screens import g_x
    g_x(c, dx + dw - 14, dy + 15, 4, layer="UI-TEXT-SEC")
    # tabs (left column)
    tabs = ["Audio", "MIDI", "Appearance", "Behaviour", "Plug-ins", "Folders"]
    tx, ty = dx, dy + 30
    c.fill(tx, ty, 120, dh - 30, "UI-PANEL-BG")
    for i, t in enumerate(tabs):
        ry = ty + i * 30
        if t == "Audio":
            c.fill(tx, ry, 120, 30, "UI-RAISED")
            c.line(tx, ry, tx, ry + 30, "UI-ACCENT")
            c.line(tx + 1, ry, tx + 1, ry + 30, "UI-ACCENT")
        c.text(t, tx + 14, ry + 15, height=H_META, layer="UI-TEXT" if t == "Audio" else "UI-TEXT-SEC", align="ml")
    # content (Audio)
    cx0 = dx + 120 + 16
    cw0 = dw - 120 - 32
    rows = [("Driver type", "Windows Audio (WASAPI)"), ("Output device", "Focusrite USB ASIO"),
            ("Input device", "Focusrite 2i2 — In 1+2"), ("Sample rate", "48000 Hz"),
            ("Buffer size", "256 samples (5.3 ms)"), ("Bit depth", "24-bit")]
    ry = ty + 14
    for label, val in rows:
        c.text(label, cx0, ry + 11, height=H_TINY, layer="UI-TEXT-SEC", align="ml")
        c.fill(cx0 + 120, ry + 2, cw0 - 120, 20, "UI-RAISED"); c.outline(cx0+120, ry+2, cw0-120, 20, "UI-HAIRLINE")
        c.text(val, cx0 + 128, ry + 12, height=H_TINY, layer="UI-TEXT", align="ml")
        from screens import g_tri
        g_tri(c, cx0 + cw0 - 10, ry + 12, 3, up=False, layer="UI-TEXT-SEC")
        ry += 30
    c.button(dx + dw - 86, dy + dh - 32, 76, 22, "Done", on=True, height=H_TINY)
