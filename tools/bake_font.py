"""Bake Monocraft (OFL 1.1) ASCII 32..126 into TerraCraft 8x8 font table.

Renders each glyph directly at a small pixel size (no rescaling, so
periods stay dots and asterisks stay asterisks), centered into the
8x8 cell with row 7 kept as the blank descender. Writes preview + C.
"""
import sys
from PIL import Image, ImageFont, ImageDraw

SRC = sys.argv[1]
PREVIEW = sys.argv[2]
TABLE = sys.argv[3]
SIZE = int(sys.argv[4]) if len(sys.argv) > 4 else 11

font = ImageFont.truetype(SRC, SIZE)


def bake(code):
    ch = chr(code)
    img = Image.new("L", (32, 32), 0)
    d = ImageDraw.Draw(img)
    d.text((8, 8), ch, font=font, fill=255)
    bbox = img.getbbox()
    if bbox is None:
        return [0] * 8
    x0, y0, x1, y1 = bbox
    w, h = x1 - x0, y1 - y0
    if w > 8 or h > 7:
        # Too big at this size: shrink to fit.
        glyph = img.crop(bbox)
        s = min(8.0 / w, 7.0 / h)
        glyph = glyph.resize((max(1, int(w * s + 0.5)), max(1, int(h * s + 0.5))),
                             Image.LANCZOS)
        x0, y0, x1, y1 = 0, 0, glyph.size[0], glyph.size[1]
        img = Image.new("L", (32, 32), 0)
        img.paste(glyph, (8, 8))
        bbox = (8, 8, 8 + glyph.size[0], 8 + glyph.size[1])
        x0, y0, x1, y1 = bbox
        w, h = x1 - x0, y1 - y0
    ox = (8 - w) // 2 - x0
    oy = (7 - h) // 2 - y0
    rows = []
    px = img.load()
    for y in range(8):
        b = 0
        for x in range(8):
            sx, sy = x - ox, y - oy
            if 0 <= sx < 32 and 0 <= sy < 32 and px[sx, sy] >= 128:
                b |= 0x80 >> x
        rows.append(b)
    return rows


rows_all = [bake(code) for code in range(32, 127)]

sheet = Image.new("RGB", (16 * 24, 6 * 24), (25, 25, 30))
d = ImageDraw.Draw(sheet)
for i, rows in enumerate(rows_all):
    ox, oy = (i % 16) * 24 + 4, (i // 16) * 24 + 4
    for y in range(8):
        for x in range(8):
            if rows[y] & (0x80 >> x):
                d.rectangle([ox + x * 2, oy + y * 2, ox + x * 2 + 1, oy + y * 2 + 1],
                            fill=(255, 240, 200))
sheet.save(PREVIEW)

with open(TABLE, "w") as f:
    f.write("static const uint8_t FONT[FONT_COUNT][FONT_H] = {\n")
    for i, rows in enumerate(rows_all):
        code = 32 + i
        ch = chr(code)
        comment = "space" if code == 32 else (ch if ch not in ("\\", "'") else "0x%02X" % code)
        f.write("    {%s}, /* %s */\n" % (", ".join("0x%02X" % b for b in rows), comment))
    f.write("};\n")
print("baked 95 glyphs at size %d" % SIZE)
