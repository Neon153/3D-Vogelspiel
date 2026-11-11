#!/usr/bin/env python3
# Erzeugt src/font.h: Monospace-Bitmapfont (DejaVu Sans Mono Bold, freie Lizenz) fuer das HUD.
import sys
from PIL import Image, ImageDraw, ImageFont
chars = [chr(c) for c in range(32, 127)] + list("äöüÄÖÜß°")
CW, CH, SIZE = 11, 22, 17
font = ImageFont.truetype("/usr/share/fonts/truetype/dejavu/DejaVuSansMono-Bold.ttf", SIZE)
out = ["// automatisch erzeugt von tools/mkfont.py", "#pragma once",
       f"static const int FONT_CW = {CW}, FONT_CH = {CH}, FONT_N = {len(chars)};",
       "// Zeichen 0..94 = ASCII 32..126, danach: ae oe ue Ae Oe Ue ss Grad",
       f"static const unsigned char FONT_DATA[{len(chars)}][{CW*CH}] = {{"]
for ch in chars:
    im = Image.new("L", (CW, CH), 0)
    ImageDraw.Draw(im).text((0, 1), ch, font=font, fill=255)
    out.append("{" + ",".join(str(v) for v in im.getdata()) + "},")
out.append("};")
open(sys.argv[1], "w").write("\n".join(out) + "\n")
