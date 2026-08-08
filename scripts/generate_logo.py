#!/usr/bin/env python3
"""Generate Almanac's compass-rose mark as a packed 1-bit C array.

The mark is a compass rose inside an instrument bezel: it ties the product
name (an almanac is a navigator's reference book), the Instrument theme's
panel chrome, and the radar screen's range rings together in one shape.

Drawn at 8x and downsampled with LANCZOS before thresholding, so the curves
and the rose's diagonal edges land cleanly instead of stair-stepping (this
gets harder as --size shrinks -- always inspect the --preview PNG before
trusting a new size).

Bit packing matches what GfxRenderer::drawImage expects and what the previous
mark used: MSB first, 1 = white, 0 = black. --invert flips that (1 = black,
0 = white) for white-ink-on-black rendering -- e.g. a mark drawn into a solid
black masthead bar, where drawImage has no invert parameter of its own.
Inversion only touches the packed bytes; the --preview PNG is always rendered
black-ink-on-white so it stays visually judgeable regardless of --invert.

  python3 scripts/generate_logo.py                    # writes src/images/Logo120.h
  python3 scripts/generate_logo.py --preview           # also writes a PNG to inspect
  python3 scripts/generate_logo.py --size 64 --invert  # writes src/images/Logo64Inv.h
"""

import argparse
import math
import os

from PIL import Image, ImageDraw

SS = 8  # supersample factor
THRESHOLD = 128


def rose_points(cx, cy, tip_r, valley_r):
    """8-vertex compass rose: four cardinal points separated by valleys.

    Four points rather than the classic eight: at 120px in one bit, eight
    points render as spindly lines with no body to them, while four read as a
    solid navigation star.
    """
    pts = []
    for k in range(4):
        tip_angle = math.radians(k * 90)
        pts.append((cx + tip_r * math.sin(tip_angle), cy - tip_r * math.cos(tip_angle)))
        valley_angle = math.radians(k * 90 + 45)
        pts.append((cx + valley_r * math.sin(valley_angle), cy - valley_r * math.cos(valley_angle)))
    return pts


def draw_mark(size):
    n = size * SS
    img = Image.new("L", (n, n), 255)
    d = ImageDraw.Draw(img)
    c = n / 2

    unit = n / 120.0  # so measurements below read in final-image pixels

    # Instrument bezel.
    bezel_outer = 55 * unit
    bezel_width = 4.5 * unit
    d.ellipse(
        [c - bezel_outer, c - bezel_outer, c + bezel_outer, c + bezel_outer],
        outline=0,
        width=int(round(bezel_width)),
    )

    # Bearing ticks just inside the bezel: long at the cardinals, short between.
    tick_outer = bezel_outer - bezel_width - 1.5 * unit
    for k in range(8):
        cardinal = k % 2 == 0
        length = (7.5 if cardinal else 4.0) * unit
        width = (3.4 if cardinal else 2.2) * unit
        a = math.radians(k * 45)
        sin_a, cos_a = math.sin(a), math.cos(a)
        x0, y0 = c + tick_outer * sin_a, c - tick_outer * cos_a
        x1, y1 = c + (tick_outer - length) * sin_a, c - (tick_outer - length) * cos_a
        d.line([x0, y0, x1, y1], fill=0, width=int(round(width)))

    # Compass rose.
    d.polygon(rose_points(c, c, tip_r=36 * unit, valley_r=13 * unit), fill=0)

    return img.resize((size, size), Image.LANCZOS)


def pack(img, size, invert=False):
    """Pack `img` (an L-mode image, `size` x `size`) into a 1-bit array.

    Bit set == background (white) when not inverting -- matching the header
    comment's "1 = white, 0 = black". `invert` flips the comparison so the
    packed bits come out flipped (1 = black/ink, 0 = white) without touching
    `img` itself, so a caller who also wants --preview still gets a normal
    black-on-white PNG.
    """
    px = list(img.getdata())
    out = []
    for y in range(size):
        for x in range(0, size, 8):
            byte = 0
            for b in range(8):
                if x + b < size:
                    is_background = px[y * size + x + b] >= THRESHOLD
                    bit = (not is_background) if invert else is_background
                    if bit:
                        byte |= 1 << (7 - b)
            out.append(byte)
    return out


def parse_args():
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    parser.add_argument(
        "--size", type=int, default=120, help="mark size in pixels, both dimensions (default: 120)"
    )
    parser.add_argument(
        "--invert",
        action="store_true",
        help="flip the packed bits (1 = black, 0 = white) for white-ink-on-black rendering",
    )
    parser.add_argument("--preview", action="store_true", help="also write a PNG to inspect")
    return parser.parse_args()


def main():
    args = parse_args()
    size = args.size
    array_name = f"Logo{size}" + ("Inv" if args.invert else "")

    img = draw_mark(size)
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

    if args.preview:
        # Always black-ink-on-white, even with --invert: only the packed
        # bytes below are inverted, so the preview stays visually judgeable.
        preview = os.path.join(root, "src", "images", f"{array_name}.png")
        img.point(lambda v: 255 if v >= THRESHOLD else 0).save(preview)
        print(f"Wrote {preview}")

    packed = pack(img, size, args.invert)
    # 19 bytes per line, no trailing whitespace: this is exactly what
    # clang-format produces for this array at the repo's 120-column limit, so
    # regenerating stays idempotent and bin/clang-format-fix has nothing to do.
    per_line = 19
    rows = [packed[i : i + per_line] for i in range(0, len(packed), per_line)]
    lines = ["    " + ", ".join(f"0x{v:02x}" for v in row) for row in rows]

    body = "#pragma once\n#include <cstdint>\n\n"
    body += f"// Image dimensions: {size}x{size}\n"
    if args.invert:
        body += "// Bits inverted: 1 = black (ink), 0 = white -- for white-ink-on-black rendering.\n"
    body += "// Generated by scripts/generate_logo.py -- do not edit by hand.\n"
    body += f"static const uint8_t {array_name}[] = {{\n"
    body += ",\n".join(lines)
    body += "};\n"

    out_path = os.path.join(root, "src", "images", f"{array_name}.h")
    with open(out_path, "w") as f:
        f.write(body)
    print(f"Wrote {out_path} ({len(packed)} bytes)")


if __name__ == "__main__":
    main()
