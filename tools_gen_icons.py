#!/usr/bin/env python3
"""Generate the 1-bit 10x10 Flipper app icon for Talos.

The glyph is an iButton can seen face on, which is genuinely what the part looks
like: a solid outer ring (the case, which is ground), a seam, and a solid disc in
the middle (the lid, which is data). Those two contacts are the whole interface,
so the icon is the subject rather than a metaphor.

Generated from geometry rather than typed out, because the radii were chosen by
rendering candidates at 20x and looking at them - solid masses read at 10 px
where line art turns to mush, and a ring one pixel too thin disappears.

    python3 tools_gen_icons.py            # write icons/talos_10px.png
    python3 tools_gen_icons.py --preview  # also write a 20x upscale to inspect
"""
from PIL import Image
import math
import os
import sys

OUT = os.path.join(os.path.dirname(__file__), "icons")
os.makedirs(OUT, exist_ok=True)

SIZE = 10
CENTRE = (SIZE - 1) / 2  # 4.5 - the glyph is symmetric about the pixel grid

# Outer edge of the case, inner edge of the case (the seam starts here), and the
# outer edge of the lid. The gap between the middle two is the white seam.
R_CASE_OUT = 5.2
R_CASE_IN = 3.9
R_LID = 2.6


def can_glyph():
    rows = []
    for y in range(SIZE):
        row = ""
        for x in range(SIZE):
            d = math.hypot(x - CENTRE, y - CENTRE)
            on = (R_CASE_IN <= d <= R_CASE_OUT) or (d <= R_LID)
            row += "#" if on else "."
        rows.append(row)
    return rows


GLYPHS = {"talos_10px": can_glyph()}


def render(name, rows):
    img = Image.new("1", (SIZE, SIZE), 1)  # 1 = white background
    for y, row in enumerate(rows):
        for x, ch in enumerate(row[:SIZE]):
            if ch == "#":
                img.putpixel((x, y), 0)  # 0 = black foreground; fbt reads dark as "on"
    path = os.path.join(OUT, name + ".png")
    img.save(path)
    return path, img


if __name__ == "__main__":
    preview = "--preview" in sys.argv
    for name, rows in GLYPHS.items():
        assert len(rows) == SIZE, f"{name} must have {SIZE} rows"
        for r in rows:
            assert len(r) == SIZE, f"{name} row is not {SIZE} wide: {r!r}"
        path, img = render(name, rows)
        print("wrote", path)
        for r in rows:
            print("   ", r)
        if preview:
            big = img.convert("L").resize((SIZE * 20, SIZE * 20), Image.NEAREST)
            p = os.path.join(OUT, name + "_preview.png")
            big.save(p)
            print("wrote", p)
