"""build.py — assemble Forge UI mockups: per-state DXF + PNG, and one tiled storyboard."""
import os, sys, re
from forge_ui import new_doc, sheet
from screens import SCREENS, WIN_W, WIN_H
from ezdxf.addons.drawing import matplotlib as mpl

OUT = "/work/out"
os.makedirs(OUT, exist_ok=True)
BG = "#0d0e0f"
TITLE_H = 40


def slug(num, title):
    s = re.sub(r"[^a-z0-9]+", "-", title.lower()).strip("-")
    return f"{num}-{s}"


def render(doc, png, dpi=120):
    from PIL import Image
    mpl.qsave(doc.modelspace(), png, dpi=dpi, bg=BG)
    try:
        w, h = Image.open(png).size
        print(f"   {os.path.basename(png)}  {w}x{h}px")
    except Exception as e:
        print("   (size?)", e)


def build_one(num, title, caption, fn, dpi=190):
    doc = new_doc()
    cv = sheet(doc, 0, 0, WIN_W, WIN_H, num, title, caption)
    fn(cv)
    dxf = f"{OUT}/{slug(num, title)}.dxf"
    png = f"{OUT}/{slug(num, title)}.png"
    doc.saveas(dxf)
    render(doc, png, dpi=dpi)
    return dxf


def build_storyboard_dxf():
    """Tiled vector storyboard (all states in one DXF for an at-a-glance overview)."""
    doc = new_doc()
    GAPX, GAPY = 200, 200
    for i, (num, title, caption, fn) in enumerate(SCREENS):
        col, row = i % 2, i // 2
        ox = col * (WIN_W + GAPX)
        oy = -row * (WIN_H + TITLE_H + GAPY)
        cv = sheet(doc, ox, oy, WIN_W, WIN_H, num, title, caption)
        fn(cv)
    doc.saveas(f"{OUT}/forge-ui-storyboard.dxf")


def montage(png_paths, out, cols=2, pad=24, bg=(13, 14, 15)):
    """Reliable contact-sheet PNG: tile the per-sheet PNGs in a grid."""
    from PIL import Image
    imgs = [Image.open(p).convert("RGB") for p in png_paths]
    cw = max(im.width for im in imgs)
    ch = max(im.height for im in imgs)
    rows = (len(imgs) + cols - 1) // cols
    W = cols * cw + (cols + 1) * pad
    H = rows * ch + (rows + 1) * pad
    sheet_img = Image.new("RGB", (W, H), bg)
    for i, im in enumerate(imgs):
        r, cc = divmod(i, cols)
        x = pad + cc * (cw + pad) + (cw - im.width) // 2
        y = pad + r * (ch + pad) + (ch - im.height) // 2
        sheet_img.paste(im, (x, y))
    sheet_img.save(out)
    print(f"   montage {os.path.basename(out)}  {W}x{H}px")


if __name__ == "__main__":
    only = sys.argv[1] if len(sys.argv) > 1 else None
    print("building per-state sheets…")
    pngs = []
    for num, title, caption, fn in SCREENS:
        if only and only not in (num, slug(num, title)):
            continue
        print(f" · {num} {title}")
        build_one(num, title, caption, fn)
        pngs.append(f"{OUT}/{slug(num, title)}.png")
    if not only:
        print("building storyboard…")
        build_storyboard_dxf()
        montage(pngs, f"{OUT}/forge-ui-storyboard.png")
    print("done →", OUT)
