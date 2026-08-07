#!/usr/bin/env python3
"""Render the Talos GitHub banner + social-preview card.

Theme: 1-Wire is the contact end of access control - no field, no antenna, just
metal on metal - so the palette is aged bronze and verdigris against Warden's
violet at 13.56 MHz and Bastion's copper at 125 kHz. Bronze because Talos was
cast in it; verdigris because bronze that has been handled for years goes green,
which is what every keyring fob in a building looks like.

The motifs are the app's own: the iButton can seen face on, and the 64-bit ROM
laid out as eight stamped cells - the graphic that makes the whole argument,
since a Dallas key's entire secret fits on one line with room to spare.

Supersampled 2x and downscaled for clean edges.

    python3 tools_gen_banner.py
"""
from PIL import Image, ImageChops, ImageDraw, ImageFilter, ImageFont
import os

OUT = os.path.join(os.path.dirname(__file__), "images")
os.makedirs(OUT, exist_ok=True)

BOLD = "/System/Library/Fonts/Supplemental/Arial Bold.ttf"
BLACK_F = "/System/Library/Fonts/Supplemental/Arial Black.ttf"
MONO = "/System/Library/Fonts/Supplemental/Andale Mono.ttf"
REG = "/System/Library/Fonts/Supplemental/Arial.ttf"

BG_TOP = (8, 14, 14)
BG_BOT = (17, 29, 28)
PATINA = (47, 211, 165)
PATINA_HI = (128, 242, 206)
BRONZE = (198, 141, 74)
WHITE = (238, 246, 243)
GRAY = (146, 166, 161)
DIM = (100, 120, 116)
STEEL = (26, 40, 39)
INK = (10, 18, 17)

SS = 2  # supersample

# A plausible DS1990A ROM: family 01, a serial, and a CRC that really is the
# Maxim CRC-8 of the first seven bytes.
ROM = [0x01, 0xA3, 0x4F, 0x09, 0x00, 0x00, 0x00]


def maxim_crc8(data):
    crc = 0
    for byte in data:
        for _ in range(8):
            mix = (crc ^ byte) & 1
            crc >>= 1
            if mix:
                crc ^= 0x8C
            byte >>= 1
    return crc


ROM = ROM + [maxim_crc8(ROM)]


def font(path, px):
    try:
        return ImageFont.truetype(path, px)
    except OSError:
        return ImageFont.truetype(BOLD, px)


def vgradient(w, h):
    img = Image.new("RGB", (w, h), BG_TOP)
    d = ImageDraw.Draw(img)
    for y in range(h):
        t = y / max(1, h - 1)
        d.line(
            [(0, y), (w, y)],
            fill=tuple(int(BG_TOP[i] + (BG_BOT[i] - BG_TOP[i]) * t) for i in range(3)),
        )
    return img


def add_glow(img, centre, radius, colour, strength):
    """Composite a soft radial wash additively, so it brightens rather than greys."""
    layer = Image.new("RGB", img.size, (0, 0, 0))
    d = ImageDraw.Draw(layer)
    cx, cy = centre
    d.ellipse([cx - radius, cy - radius, cx + radius, cy + radius], fill=colour)
    layer = layer.filter(ImageFilter.GaussianBlur(radius * 0.55))
    layer = Image.eval(layer, lambda v: v * strength // 255)
    return ImageChops.add(img, layer)


def spaced(d, xy, text, fnt, fill, tracking):
    """Draw letterspaced text."""
    x, y = xy
    for ch in text:
        d.text((x, y), ch, font=fnt, fill=fill)
        x += d.textlength(ch, font=fnt) + tracking


def spaced_width(d, text, fnt, tracking):
    return sum(d.textlength(c, font=fnt) for c in text) + tracking * max(0, len(text) - 1)


def can(d, cx, cy, r, lit=True):
    """The iButton can, face on: a bronze case ring and a data lid.

    The same two contacts the icon draws, at a size where the seam between them
    is visible - which is the point, because those two rings are the entire
    interface a Dallas key offers the world.
    """
    ring = int(r * 0.20)
    d.ellipse([cx - r, cy - r, cx + r, cy + r], fill=BRONZE)
    d.ellipse(
        [cx - r + ring, cy - r + ring, cx + r - ring, cy + r - ring],
        fill=INK,
    )
    lid = int(r * 0.52)
    d.ellipse(
        [cx - lid, cy - lid, cx + lid, cy + lid],
        fill=PATINA if lit else STEEL,
    )
    # the faint highlight that says "metal"
    d.arc(
        [cx - r + ring // 2, cy - r + ring // 2, cx + r - ring // 2, cy + r - ring // 2],
        200,
        320,
        fill=PATINA_HI,
        width=max(1, ring // 3),
    )


def rom_plate(d, x, y, w, h, fenced=True):
    """The 64-bit ROM as eight stamped cells - the app's signature graphic.

    Family code fenced off on the left, checksum on the right and filled because
    it verifies. Everything between is the serial: the whole of the secret.
    """
    cell = w / len(ROM)
    f = font(MONO, int(h * 0.46))
    for i, b in enumerate(ROM):
        x0 = x + i * cell
        is_family = i == 0
        is_crc = i == len(ROM) - 1
        if is_crc:
            fill, ink = PATINA, INK
        elif is_family:
            fill, ink = STEEL, BRONZE
        else:
            fill, ink = STEEL, PATINA_HI
        d.rounded_rectangle(
            [x0 + 3 * SS, y, x0 + cell - 3 * SS, y + h],
            radius=int(h * 0.16),
            fill=fill,
            outline=BRONZE if is_family else None,
            width=2 * SS,
        )
        txt = f"{b:02X}"
        d.text(
            (x0 + cell / 2 - d.textlength(txt, font=f) / 2, y + h * 0.26),
            txt,
            font=f,
            fill=ink,
        )

    if fenced:
        fl = font(BOLD, int(h * 0.26))
        d.text((x + 2 * SS, y + h + h * 0.16), "FAMILY", font=fl, fill=BRONZE)
        mid = "SERIAL - the entire secret"
        d.text(
            (x + w / 2 - d.textlength(mid, font=fl) / 2, y + h + h * 0.16),
            mid,
            font=fl,
            fill=DIM,
        )
        d.text(
            (x + w - d.textlength("CRC", font=fl) - 2 * SS, y + h + h * 0.16),
            "CRC",
            font=fl,
            fill=PATINA,
        )


def grade_stamp(d, cx, cy, s, letter, sub, ring=PATINA):
    """A rounded-square grade badge with corner ticks - the app's verdict card."""
    half = s // 2
    d.rounded_rectangle(
        [cx - half, cy - half, cx + half, cy + half],
        radius=s // 7,
        fill=(12, 20, 19),
        outline=ring,
        width=5 * SS,
    )
    for dx, dy in ((-1, -1), (1, -1), (-1, 1), (1, 1)):
        px, py = cx + dx * (half - 14 * SS), cy + dy * (half - 14 * SS)
        d.line([(px - dx * 10 * SS, py), (px, py)], fill=ring, width=3 * SS)
        d.line([(px, py - dy * 10 * SS), (px, py)], fill=ring, width=3 * SS)

    f = font(BLACK_F, int(s * 0.50))
    bbox = f.getbbox(letter)
    d.text(
        (cx - d.textlength(letter, font=f) / 2, cy - (bbox[3] + bbox[1]) / 2 - s * 0.07),
        letter,
        font=f,
        fill=WHITE,
    )

    fs = font(BOLD, int(s * 0.092))
    tw = spaced_width(d, sub, fs, 2 * SS)
    spaced(d, (cx - tw / 2, cy + s * 0.27), sub, fs, ring, 2 * SS)


def banner(w=1600, h=460, name="banner.png"):
    W, H = w * SS, h * SS
    img = vgradient(W, H)
    img = add_glow(img, (int(W * 0.20), int(H * 0.55)), int(H * 0.85), PATINA, 40)
    img = add_glow(img, (int(W * 0.84), int(H * 0.44)), int(H * 0.55), BRONZE, 26)
    d = ImageDraw.Draw(img)

    pad = int(W * 0.055)

    spaced(
        d,
        (pad, int(H * 0.135)),
        "iBUTTON / DALLAS KEY GRADER",
        font(BOLD, 21 * SS),
        PATINA,
        5 * SS,
    )
    d.text((pad - 5 * SS, int(H * 0.205)), "TALOS", font=font(BLACK_F, 104 * SS), fill=WHITE)

    d.text(
        (pad, int(H * 0.545)),
        "A Dallas key is a number, not a secret.",
        font=font(BOLD, 28 * SS),
        fill=PATINA_HI,
    )
    d.text(
        (pad, int(H * 0.645)),
        "Touch it once and you hold everything the lock will ever ask for.",
        font=font(REG, 23 * SS),
        fill=GRAY,
    )

    rom_plate(d, pad, int(H * 0.755), int(W * 0.46), int(H * 0.105))

    # The can, and the verdict it earns.
    can(d, int(W * 0.705), int(H * 0.46), int(H * 0.20))
    grade_stamp(d, int(W * 0.865), int(H * 0.42), int(H * 0.46), "F", "CLONEABLE")

    # Centred under the can-and-stamp pair rather than the whole canvas, so it
    # reads as their caption instead of drifting between the two columns.
    f_note = font(BOLD, 19 * SS)
    note = "NO PART SOLD AS A DOOR KEY PASSES"
    group_cx = W * 0.785
    spaced(
        d,
        (group_cx - spaced_width(d, note, f_note, 3 * SS) / 2, int(H * 0.775)),
        note,
        f_note,
        GRAY,
        3 * SS,
    )

    d.rectangle([0, H - 5 * SS, W, H], fill=PATINA)

    path = os.path.join(OUT, name)
    img.resize((w, h), Image.LANCZOS).save(path)
    print("wrote", path)


def social(w=1280, h=640, name="social-preview.png"):
    W, H = w * SS, h * SS
    img = vgradient(W, H)
    img = add_glow(img, (int(W * 0.5), int(H * 0.30)), int(H * 0.80), PATINA, 36)
    img = add_glow(img, (int(W * 0.5), int(H * 1.02)), int(H * 0.45), BRONZE, 22)
    d = ImageDraw.Draw(img)

    can(d, W // 2, int(H * 0.185), int(H * 0.135))

    f_eyebrow = font(BOLD, 23 * SS)
    eb = "iBUTTON KEY GRADER   FLIPPER ZERO"
    spaced(
        d,
        ((W - spaced_width(d, eb, f_eyebrow, 5 * SS)) / 2, int(H * 0.355)),
        eb,
        f_eyebrow,
        PATINA,
        5 * SS,
    )

    f_title = font(BLACK_F, 118 * SS)
    d.text(
        ((W - d.textlength("TALOS", font=f_title)) / 2, int(H * 0.415)),
        "TALOS",
        font=f_title,
        fill=WHITE,
    )

    f_tag = font(BOLD, 29 * SS)
    tag = "A Dallas key is a number, not a secret."
    d.text(
        ((W - d.textlength(tag, font=f_tag)) / 2, int(H * 0.685)), tag, font=f_tag, fill=PATINA_HI
    )

    f_sub = font(REG, 22 * SS)
    sub = "DS1990A    DS1961S    DS1963S    DS1992    DS1971    Cyfral    Metakom"
    d.text(((W - d.textlength(sub, font=f_sub)) / 2, int(H * 0.775)), sub, font=f_sub, fill=GRAY)

    pw = int(W * 0.56)
    rom_plate(d, (W - pw) // 2, int(H * 0.855), pw, int(H * 0.072), fenced=False)

    d.rectangle([0, H - 6 * SS, W, H], fill=PATINA)

    path = os.path.join(OUT, name)
    img.resize((w, h), Image.LANCZOS).save(path)
    print("wrote", path)


if __name__ == "__main__":
    banner()
    social()
