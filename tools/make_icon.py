#!/usr/bin/env python3
"""make_icon.py -- launcher/icon.jpg, the HOME-menu icon (256x256 JPEG, what
nacptool/elf2nro take).

From newicon.png at the top of the port when it is there (any size, scaled
down, zoomed in 1.2x on its title so the name reads on the HOME menu);
otherwise drawn here from plain shapes -- a sky, two pipes and two round
birds. Needs Pillow.

  python3 tools/make_icon.py [--from newicon.png] [--zoom 1.2] [--center 0.485,0.5]
                             [-o launcher/icon.jpg]
"""
import argparse
import os

from PIL import Image, ImageDraw, ImageFilter

HERE = os.path.dirname(os.path.abspath(__file__))
S = 4  # supersampling
W = 256 * S


def lerp(a, b, t):
    return tuple(int(round(a[i] + (b[i] - a[i]) * t)) for i in range(3))


def bird(d, cx, cy, r, body, belly, wing):
    # body, belly, eye, beak, wing: all circles and polygons
    d.ellipse([cx - r, cy - r, cx + r, cy + r], fill=body, outline=(40, 30, 30), width=3 * S)
    d.chord([cx - r * 0.8, cy - r * 0.1, cx + r * 0.6, cy + r * 0.95], 0, 180, fill=belly)
    d.ellipse([cx + r * 0.15, cy - r * 0.62, cx + r * 0.72, cy - r * 0.05], fill=(255, 255, 255),
              outline=(40, 30, 30), width=2 * S)
    d.ellipse([cx + r * 0.42, cy - r * 0.45, cx + r * 0.62, cy - r * 0.2], fill=(30, 25, 25))
    d.polygon([(cx + r * 0.72, cy - r * 0.02), (cx + r * 1.28, cy + r * 0.12), (cx + r * 0.72, cy + r * 0.34)],
              fill=(245, 120, 40), outline=(40, 30, 30))
    d.ellipse([cx - r * 0.95, cy - r * 0.05, cx - r * 0.2, cy + r * 0.45], fill=wing, outline=(40, 30, 30),
              width=2 * S)


def pipe(d, x, w, y0, y1, cap_at_top):
    green, dark, light = (92, 190, 64), (48, 120, 36), (160, 228, 120)
    d.rectangle([x, y0, x + w, y1], fill=green, outline=dark, width=3 * S)
    d.rectangle([x + w * 0.15, y0, x + w * 0.3, y1], fill=light)
    cy = y0 if cap_at_top else y1
    ch = 22 * S
    cap = [x - 8 * S, cy - (0 if cap_at_top else ch), x + w + 8 * S, cy + (ch if cap_at_top else 0)]
    d.rectangle(cap, fill=green, outline=dark, width=3 * S)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("-o", default=os.path.join(os.path.dirname(HERE), "launcher", "icon.jpg"))
    ap.add_argument("--from", dest="src", default=os.path.join(os.path.dirname(HERE), "newicon.png"))
    ap.add_argument("--zoom", type=float, default=1.2, help="how far to zoom in on the picture")
    ap.add_argument("--center", default="0.485,0.5", help="the point zoomed in on, as fractions (the title)")
    a = ap.parse_args()
    if a.src and os.path.exists(a.src):
        img = Image.open(a.src).convert("RGB")
        w, h = img.size
        side = min(w, h) / max(a.zoom, 1.0)  # a square, zoomed in on the centre point
        cx, cy = (float(v) for v in a.center.split(","))
        x0 = min(max(cx * w - side / 2, 0), w - side)
        y0 = min(max(cy * h - side / 2, 0), h - side)
        img = img.crop((round(x0), round(y0), round(x0 + side), round(y0 + side)))
        img = img.resize((256, 256), Image.LANCZOS)
        img.save(a.o, "JPEG", quality=95)
        print("wrote", a.o, "from", a.src)
        return
    img = Image.new("RGB", (W, W))
    d = ImageDraw.Draw(img)
    top, bottom = (78, 192, 202), (190, 236, 232)
    for y in range(W):
        d.line([(0, y), (W, y)], fill=lerp(top, bottom, y / W))
    # soft clouds
    clouds = Image.new("L", (W, W), 0)
    cd = ImageDraw.Draw(clouds)
    for cx, cy, r in [(40, 180, 40), (90, 170, 52), (150, 182, 44), (210, 168, 50), (250, 186, 36)]:
        cd.ellipse([(cx - r) * S, (cy - r) * S, (cx + r) * S, (cy + r) * S], fill=150)
    clouds = clouds.filter(ImageFilter.GaussianBlur(6 * S))
    img.paste((255, 255, 255), mask=clouds)
    d = ImageDraw.Draw(img)
    # pipes
    pipe(d, 150 * S, 44 * S, -10 * S, 70 * S, cap_at_top=False)
    pipe(d, 150 * S, 44 * S, 150 * S, 212 * S, cap_at_top=True)
    # ground
    d.rectangle([0, 212 * S, W, W], fill=(222, 210, 150))
    d.rectangle([0, 212 * S, W, 220 * S], fill=(120, 200, 70))
    for x in range(0, 256, 16):
        d.polygon([(x * S, 220 * S), ((x + 8) * S, 220 * S), ((x + 4) * S, 227 * S)], fill=(96, 176, 56))
    # two players
    bird(d, 70 * S, 92 * S, 30 * S, body=(250, 206, 50), belly=(255, 236, 150), wing=(255, 246, 210))
    bird(d, 100 * S, 158 * S, 24 * S, body=(232, 84, 72), belly=(250, 170, 150), wing=(255, 225, 215))
    img = img.resize((256, 256), Image.LANCZOS)
    img.save(a.o, "JPEG", quality=92)
    print("wrote", a.o)


if __name__ == "__main__":
    main()
