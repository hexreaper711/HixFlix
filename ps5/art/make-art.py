#!/usr/bin/env python3
"""Draw HixFlix's home-screen art and write it as the console wants it.

    python3 ps5/art/make-art.py [sce_sys-dir]     (needs Pillow and NumPy)

Writes into ps5/sce_sys (or the folder given):
  icon0.png   512x512: a red H folded like a ribbon on black, the wordmark under it
  pic0.dds    3840x2160 BC7 (DX10, no mipmaps): black, red light sweeping in from the right
  pic1.dds    the same picture (the launch background)
The BC7 encoder is a small one (mode 6 only, bounding-box endpoints): plenty for
smooth gradients and big shapes.
"""
import os
import struct
import sys

import numpy as np
from PIL import Image, ImageDraw, ImageFilter, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
RED = (229, 9, 20)
DARK_RED = (140, 4, 10)
FONT = os.path.join(REPO, "assets", "fonts", "Inter-Bold.ttf")


def radial(size, center, radius, color, strength):
    """A soft light: color fading out from center over radius."""
    w, h = size
    y, x = np.mgrid[0:h, 0:w].astype(np.float32)
    d = np.sqrt((x - center[0]) ** 2 + (y - center[1]) ** 2) / radius
    a = np.clip(1 - d, 0, 1) ** 2 * strength
    img = np.zeros((h, w, 4), np.float32)
    img[..., :3] = color
    img[..., 3] = a * 255
    return Image.fromarray(img.astype(np.uint8), "RGBA")


def ribbon_h(size):
    """The H: two red bars and a crossbar, with a darker fold where they meet."""
    s = size
    img = Image.new("RGBA", (s, s), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    bar_w, top, bottom = s * 0.2, s * 0.12, s * 0.88
    left_x, right_x = s * 0.24, s * 0.56
    shadow = Image.new("RGBA", (s, s), (0, 0, 0, 0))
    ds = ImageDraw.Draw(shadow)
    ds.rectangle([left_x + s * 0.02, top + s * 0.03, left_x + bar_w + s * 0.02, bottom + s * 0.03], fill=(0, 0, 0, 160))
    ds.rectangle([right_x + s * 0.02, top + s * 0.03, right_x + bar_w + s * 0.02, bottom + s * 0.03], fill=(0, 0, 0, 160))
    img.alpha_composite(shadow.filter(ImageFilter.GaussianBlur(s * 0.03)))
    # bars with a top-to-bottom shade
    for x0 in (left_x, right_x):
        for i in range(int(top), int(bottom)):
            t = (i - top) / (bottom - top)
            c = tuple(int(RED[k] * (1 - 0.25 * t) + DARK_RED[k] * 0.25 * t) for k in range(3))
            d.line([(x0, i), (x0 + bar_w, i)], fill=c + (255,))
    # the crossbar, folded: darker where it tucks under the bars
    cy0, cy1 = s * 0.43, s * 0.57
    d.rectangle([left_x + bar_w, cy0, right_x, cy1], fill=RED + (255,))
    fold = s * 0.045
    d.polygon([(left_x + bar_w, cy0), (left_x + bar_w + fold, cy0), (left_x + bar_w, cy1)], fill=DARK_RED + (255,))
    d.polygon([(right_x, cy1), (right_x - fold, cy1), (right_x, cy0)], fill=DARK_RED + (255,))
    return img


def icon():
    s = 512
    img = Image.new("RGBA", (s, s), (10, 10, 10, 255))
    img.alpha_composite(radial((s, s), (s / 2, s * 0.42), s * 0.7, RED, 0.28))
    img.alpha_composite(ribbon_h(int(s * 0.86)), (int(s * 0.07), int(s * 0.0)))
    d = ImageDraw.Draw(img)
    font = ImageFont.truetype(FONT, 58)
    word = "HIXFLIX"
    w = d.textlength(word, font=font)
    d.text(((s - w) / 2, s * 0.8), word, font=font, fill=RED + (255,))
    return img.convert("RGB")


def background():
    w, h = 3840, 2160
    img = Image.new("RGBA", (w, h), (8, 8, 8, 255))
    img.alpha_composite(radial((w, h), (w * 0.95, h * 0.05), w * 0.75, RED, 0.35))
    img.alpha_composite(radial((w, h), (w * 1.05, h * 0.9), w * 0.55, DARK_RED, 0.5))
    # sweeping red arcs on the right, like light through a curtain
    arcs = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    d = ImageDraw.Draw(arcs)
    for i in range(14):
        r = 1500 + i * 150
        c = (int(229 - i * 9), int(9 + i), int(20 + i), int(150 - i * 9))
        d.arc([w * 1.08 - r, h * 0.5 - r, w * 1.08 + r, h * 0.5 + r], 120, 240, fill=c, width=26 - i)
    img.alpha_composite(arcs.filter(ImageFilter.GaussianBlur(18)))
    # dark on the left, where the console writes the title
    shade = np.zeros((h, w, 4), np.uint8)
    ramp = np.clip(1 - np.linspace(0, 1, w) / 0.55, 0, 1) ** 1.5
    shade[..., 3] = (ramp * 200).astype(np.uint8)[None, :]
    img.alpha_composite(Image.fromarray(shade, "RGBA"))
    return img


WEIGHTS = np.array([0, 4, 9, 13, 17, 21, 26, 30, 34, 38, 43, 47, 51, 55, 60, 64], np.int32)


def bc7_mode6(rgba):
    """RGBA (H, W, 4) uint8 -> BC7 blocks, mode 6 with bounding-box endpoints."""
    h, w, _ = rgba.shape
    blocks = rgba.reshape(h // 4, 4, w // 4, 4, 4).transpose(0, 2, 1, 3, 4).reshape(-1, 16, 4).astype(np.int32)
    lo, hi = blocks.min(axis=1), blocks.max(axis=1)
    # endpoints: 7 bits + a p-bit (0 for the low end, 1 for the high end)
    c0, c1 = lo >> 1, np.minimum(hi >> 1, 127)
    e0 = c0 << 1
    e1 = (c1 << 1) | 1
    axis = (e1 - e0)[:, None, :].astype(np.float32)
    rel = (blocks - e0[:, None, :]).astype(np.float32)
    dd = (axis * axis).sum(axis=2)
    t = np.where(dd > 0, (rel * axis).sum(axis=2) / np.maximum(dd, 1e-6), 0)
    t = np.clip(t, 0, 1) * 64
    idx = np.abs(t[..., None] - WEIGHTS[None, None, :]).argmin(axis=2)
    # the first pixel's index must have its top bit clear: swap the ends if not
    swap = idx[:, 0] >= 8
    idx[swap] = 15 - idx[swap]
    c0s, c1s = c0.copy(), c1.copy()
    c0s[swap], c1s[swap] = c1[swap], c0[swap]
    p0 = np.where(swap, 1, 0)
    p1 = np.where(swap, 0, 1)
    n = blocks.shape[0]
    bits = np.zeros((n, 128), np.uint8)

    def put(value, start, count):
        for b in range(count):
            bits[:, start + b] = (value >> b) & 1

    bits[:, 6] = 1  # mode 6
    pos = 7
    for ch in range(4):
        put(c0s[:, ch], pos, 7)
        put(c1s[:, ch], pos + 7, 7)
        pos += 14
    put(p0, 63, 1)
    put(p1, 64, 1)
    put(idx[:, 0], 65, 3)
    for i in range(1, 16):
        put(idx[:, i], 68 + (i - 1) * 4, 4)
    return np.packbits(bits, axis=1, bitorder="little").tobytes()


def write_dds(path, img):
    rgba = np.asarray(img.convert("RGBA"), np.uint8)
    h, w, _ = rgba.shape
    data = bc7_mode6(rgba)
    header = struct.pack("<4sIIIIIII44x", b"DDS ", 124, 0x1 | 0x2 | 0x4 | 0x1000 | 0x80000, h, w, w * h, 0, 1)
    pixel_format = struct.pack("<II4s20x", 32, 0x4, b"DX10")
    caps = struct.pack("<IIII4x", 0x1000, 0, 0, 0)
    dx10 = struct.pack("<IIIII", 98, 3, 0, 1, 0)
    with open(path, "wb") as f:
        f.write(header + pixel_format + caps + dx10 + data)


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(REPO, "ps5", "sce_sys")
    os.makedirs(out, exist_ok=True)
    icon().save(os.path.join(out, "icon0.png"))
    bg = background()
    bg.convert("RGB").save(os.path.join(out, "background-source.png"))
    write_dds(os.path.join(out, "pic0.dds"), bg)
    write_dds(os.path.join(out, "pic1.dds"), bg)
    print("wrote icon0.png, pic0.dds, pic1.dds in", out)


if __name__ == "__main__":
    main()
