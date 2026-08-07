#!/usr/bin/env python3
"""Render 128x64 Flipper-style mockups of Talos's screens for the README.

Faithful on purpose: every coordinate below is copied from the layout constants
in views/scan_view.c and views/result_view.c, the logic trace runs the same
timeline function the firmware does, and text is positioned by BASELINE (PIL
anchor "ls"/"ms"/"rs") because canvas_draw_str() takes y as the baseline.

Drawing the screens the way the firmware does is what catches a collision before
it ships, so these are worth keeping in sync when a row moves.
"""
from PIL import Image, ImageDraw, ImageFont
import os

OUT = os.path.join(os.path.dirname(__file__), "images")
os.makedirs(OUT, exist_ok=True)

ORANGE = (255, 159, 12)
INK = (26, 18, 2)
BEZEL = (16, 22, 21)
BEZEL_HI = (40, 58, 55)

SCALE = 7
W, H = 128, 64

FB = "/System/Library/Fonts/Supplemental/Arial Bold.ttf"
FR = "/System/Library/Fonts/Supplemental/Arial.ttf"
FBLK = "/System/Library/Fonts/Supplemental/Arial Black.ttf"

PRIM = ImageFont.truetype(FB, 9)  # FontPrimary
SEC = ImageFont.truetype(FR, 8)  # FontSecondary
BIG = ImageFont.truetype(FBLK, 19)  # FontBigNumbers


def screen():
    return Image.new("RGB", (W, H), ORANGE)


def base(d, x, y, s, font, fill=INK, anchor="ls"):
    """Draw `s` with its BASELINE on y, like canvas_draw_str()."""
    d.text((x, y), s, font=font, fill=fill, anchor=anchor)


def width(d, s, font):
    return d.textlength(s, font=font)


def circle(d, cx, cy, r, fill=None, outline=INK):
    d.ellipse([cx - r, cy - r, cx + r, cy + r], fill=fill, outline=outline)


def finish(img, name):
    up = img.resize((W * SCALE, H * SCALE), Image.NEAREST)
    pad = 20
    canvas = Image.new("RGB", (W * SCALE + pad * 2, H * SCALE + pad * 2), BEZEL)
    d = ImageDraw.Draw(canvas)
    d.rounded_rectangle(
        [6, 6, canvas.width - 6, canvas.height - 6], radius=16, outline=BEZEL_HI, width=3
    )
    canvas.paste(up, (pad, pad))
    path = os.path.join(OUT, name)
    canvas.save(path)
    print("wrote", path)
    return canvas


# ------------------------------------------------------------------- 1. menu
def m_menu():
    img = screen()
    d = ImageDraw.Draw(img)
    base(d, 64, 9, "Talos", PRIM, anchor="ms")
    d.line([(0, 11), (127, 11)], fill=INK)
    d.rounded_rectangle([2, 14, 125, 26], radius=3, fill=INK)
    base(d, 7, 23, "Grade a Key", SEC, fill=ORANGE)
    base(d, 7, 36, "Keyring Log", SEC)
    base(d, 7, 48, "Settings", SEC)
    base(d, 7, 60, "About", SEC)
    return finish(img, "screen_menu.png")


# ------------------------------------------------------------------- 2. scan
# Mirrors views/scan_view.c: the SV_* constants and sv_bus_low().
SV_RESET, SV_IDLE = 28, 44
SV_RECOVER, SV_PRESENCE, SV_GAP = 5, 8, 5
SV_BIT_CELLS, SV_LOW_ONE, SV_LOW_ZERO = 6, 1, 4
SV_TAIL = 30
SV_TRACE_X0, SV_TRACE_X1 = 38, 125
SV_HI, SV_LO = 21, 37


def sv_period(decoded, nbytes):
    if not decoded:
        return SV_RESET + SV_IDLE
    return SV_RESET + SV_RECOVER + SV_PRESENCE + SV_GAP + nbytes * 8 * SV_BIT_CELLS + SV_TAIL


def sv_bus_low(decoded, bits, t):
    if not decoded:
        return t < SV_RESET
    if t < SV_RESET:
        return True
    t -= SV_RESET
    if t < SV_RECOVER:
        return False
    t -= SV_RECOVER
    if t < SV_PRESENCE:
        return True
    t -= SV_PRESENCE
    if t < SV_GAP:
        return False
    t -= SV_GAP
    nbits = len(bits) * 8
    if t < nbits * SV_BIT_CELLS:
        i, off = t // SV_BIT_CELLS, t % SV_BIT_CELLS
        one = (bits[i // 8] >> (i % 8)) & 1  # 1-Wire clocks out LSB first
        return off < (SV_LOW_ONE if one else SV_LOW_ZERO)
    return False


def m_scan(fname, decoded=False, bits=(), phase=0, mark="RESET, no answer", stage="Pulsing the contact", secs=17):
    img = screen()
    d = ImageDraw.Draw(img)

    base(d, 2, 9, "Touch a Key", PRIM)
    if not decoded:
        base(d, 126, 9, f"{secs}s", SEC, anchor="rs")
    d.line([(0, 11), (127, 11)], fill=INK)

    # the can, face on: sv_draw_can(canvas, 16, 29, answered)
    circle(d, 16, 29, 13)
    circle(d, 16, 29, 10)
    circle(d, 16, 29, 7, fill=INK if decoded else None)

    base(d, 31, SV_HI + 3, "1", SEC)
    base(d, 31, SV_LO + 3, "0", SEC)

    period = sv_period(decoded, len(bits))
    prev = None
    for x in range(SV_TRACE_X0, SV_TRACE_X1 + 1):
        t = (phase + (x - SV_TRACE_X0)) % period
        y = SV_LO if sv_bus_low(decoded, bits, t) else SV_HI
        if prev is not None and prev != y:
            d.line([(x, prev), (x, y)], fill=INK)
        d.point((x, y), fill=INK)
        prev = y

    base(d, SV_TRACE_X0, 46, mark, SEC)
    base(d, 64, 55, stage, SEC, anchor="ms")
    base(d, 64, 63, "Flat face to the two pads", SEC, anchor="ms")
    return finish(img, fname)


# ----------------------------------------------------------------- 3. result
# Mirrors views/result_view.c: the RV_* constants and rv_draw_rom().
def m_result(fname, name, letter, band, score, copy_time, copy_label, rom, crc_ok=True, suspect=False):
    img = screen()
    d = ImageDraw.Draw(img)

    base(d, 2, 9, name, PRIM)
    d.line([(0, 11), (127, 11)], fill=INK)

    # band bar, inverted
    d.rounded_rectangle([0, 13, 127, 24], radius=2, fill=INK)
    base(d, 4, 23, letter, PRIM, fill=ORANGE)
    d.text((70, 19), band, font=SEC, fill=ORANGE, anchor="mm")
    if suspect:
        base(d, 121, 23, "!", PRIM, fill=ORANGE)

    # score
    if score is None:
        base(d, 3, 44, "--", BIG)
    else:
        s = str(score)
        base(d, 3, 44, s, BIG)
        base(d, 3 + width(d, s, BIG) + 2, 43, "/100", SEC)

    if copy_time is None:
        base(d, 54, 33, copy_label[0], SEC)
        base(d, 54, 43, copy_label[1], SEC)
    else:
        base(d, 54, 33, f"COPY   {copy_time}", SEC)
        base(d, 54, 43, copy_label, SEC)

    # the ROM, stamped across one line: RV_ROM_RULE 45, TOP 46, BASE 53
    d.line([(0, 45), (127, 45)], fill=INK)
    if not rom:
        for x in range(4, 124, 4):
            d.line([(x, 49), (x + 1, 49)], fill=INK)
    else:
        cell = 120 // len(rom)
        dallas = len(rom) >= 8
        for i, b in enumerate(rom):
            x0 = 4 + i * cell
            is_crc = dallas and i == len(rom) - 1
            hx = f"{b:02X}"
            if is_crc and crc_ok and not suspect:
                d.rectangle([x0, 46, x0 + cell - 1, 53], fill=INK)
                d.text((x0 + cell / 2, 53), hx, font=SEC, fill=ORANGE, anchor="ms")
            else:
                d.text((x0 + cell / 2, 53), hx, font=SEC, fill=INK, anchor="ms")
                if is_crc:
                    d.line([(x0 + 1, 53), (x0 + cell - 2, 46)], fill=INK)
        if dallas:
            d.line([(4 + cell, 46), (4 + cell, 53)], fill=INK)
            d.line([(4 + (len(rom) - 1) * cell, 46), (4 + (len(rom) - 1) * cell, 53)], fill=INK)

    # footer
    d.rectangle([0, 55, 127, 63], fill=INK)
    base(d, 3, 62, "OK Report", SEC, fill=ORANGE)
    base(d, 125, 62, "Rescan >", SEC, fill=ORANGE, anchor="rs")
    return finish(img, fname)


# ----------------------------------------------------------- 4. scrolled text
def m_scroll(fname, lines):
    """The widget text-scroll element: bold headers, a scrollbar on the right."""
    img = screen()
    d = ImageDraw.Draw(img)
    y = 8
    for text, bold in lines:
        base(d, 2, y, text, PRIM if bold else SEC)
        y += 9 if bold else 8
        if y > 64:
            break
    d.line([(126, 0), (126, 63)], fill=INK)
    d.rectangle([124, 0, 127, 22], fill=INK)
    return finish(img, fname)


def strip(images, name, cols=None):
    cols = cols or len(images)
    gap = 14
    cw, ch = images[0].width, images[0].height
    rows = (len(images) + cols - 1) // cols
    sheet = Image.new(
        "RGB", (cols * cw + (cols - 1) * gap, rows * ch + (rows - 1) * gap), (12, 18, 17)
    )
    for i, im in enumerate(images):
        sheet.paste(im, ((i % cols) * (cw + gap), (i // cols) * (ch + gap)))
    path = os.path.join(OUT, name)
    sheet.save(path)
    print("wrote", path)


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


def rom_for(family, serial):
    """A ROM with a real Maxim CRC, laid out the way the silicon does."""
    body = [family] + [(serial >> (8 * i)) & 0xFF for i in range(6)]
    return body + [maxim_crc8(body)]


if __name__ == "__main__":
    DS1990 = rom_for(0x01, 0x0000094FA3)
    DS1961 = rom_for(0x33, 0x00A17C22E4)
    DS18B20 = rom_for(0x28, 0x0000FF5B31)

    menu = m_menu()
    scan = m_scan("screen_scan.png", phase=0)
    m_scan("screen_scan_idle.png", phase=30, mark="BUS IDLE", secs=11)
    read = m_scan(
        "screen_scan_read.png",
        decoded=True,
        bits=DS1990,
        phase=20,
        mark="PRESENCE!",
        stage="Key answered",
    )

    ds1990 = m_result(
        "screen_grade_ds1990.png",
        "DS1990A / DS2401",
        "F",
        "CLONEABLE",
        22,
        "~3 s",
        "Any Flipper",
        DS1990,
    )
    ds1961 = m_result(
        "screen_grade_ds1961.png",
        "DS1961S / DS2432",
        "B",
        "CHALLENGED",
        74,
        "lab",
        "Lab attack",
        DS1961,
    )
    neighbour = m_result(
        "screen_grade_neighbour.png",
        "DS1990A / DS2401",
        "F",
        "REPLAYABLE",
        14,
        "~3 s",
        "Any Flipper",
        rom_for(0x01, 0x0000094FA5),
    )
    sensor = m_result(
        "screen_grade_sensor.png",
        "DS18B20",
        "-",
        "NOT A KEY",
        None,
        None,
        ("Not a key", "Temperature s.."),
        DS18B20,
    )
    unread = m_result(
        "screen_unread.png",
        "No key read",
        "-",
        "UNREAD",
        None,
        None,
        ("Nothing read", "OK for help"),
        [],
    )

    report = m_scroll(
        "screen_report.png",
        [
            ("DS1990A / DS2401", True),
            ("Grade F   22/100  CLONEABLE", False),
            ("64 bits in the clear, to", False),
            ("anything that touches it", False),
            ("", False),
            ("Findings", True),
            ("[x] No challenge: one read", False),
            ("    hands over the whole key", False),
            ("[x] A Flipper, or a one-dollar", False),
            ("    blank, is a working copy", False),
        ],
    )
    score = m_scroll(
        "screen_score.png",
        [
            ("Score", True),
            ("Authentication  0/45", False),
            ("Integrity      10/15", False),
            ("Copy cost       0/25", False),
            ("Key space      12/15", False),
            ("Total          22/100", False),
            ("", False),
            ("A key that answers READ ROM and", False),
            ("nothing else scores none of the", False),
            ("45 points for authentication.", False),
        ],
    )

    strip([menu, scan, read, ds1990], "screens.png", cols=4)
    strip([ds1961, neighbour, sensor, unread], "screens_grades.png", cols=4)
    strip([report, score], "screens_report.png", cols=2)
