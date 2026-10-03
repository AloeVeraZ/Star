"""Turns preview frames into a labelled contact sheet as the round panel shows them.

    python3 sheet.py OUTPUT.png FRAME.ppm [FRAME.ppm ...] [--scale 2] [--cols 4]

Pixels outside the 240 px circle are hidden behind a grey bezel, the way the
GC9A01A panel and enclosure crop them. Needs Pillow (pip install pillow).
"""
import os
import sys
from PIL import Image, ImageDraw

args = sys.argv[1:]
scale, cols = 2, 4
if "--scale" in args:
    i = args.index("--scale"); scale = int(args[i + 1]); del args[i:i + 2]
if "--cols" in args:
    i = args.index("--cols"); cols = int(args[i + 1]); del args[i:i + 2]
out, frames = args[0], args[1:]

size = 240 * scale
pad, label = 16, 22
mask = Image.new("L", (size, size), 0)
ImageDraw.Draw(mask).ellipse((0, 0, size - 1, size - 1), fill=255)

rows = (len(frames) + cols - 1) // cols
sheet = Image.new("RGB", (cols * (size + pad) + pad, rows * (size + pad + label) + pad), (214, 218, 220))
draw = ImageDraw.Draw(sheet)
for n, path in enumerate(frames):
    img = Image.open(path).convert("RGB").resize((size, size), Image.NEAREST)
    x = pad + (n % cols) * (size + pad)
    y = pad + (n // cols) * (size + pad + label)
    draw.ellipse((x - 6, y - 6, x + size + 5, y + size + 5), fill=(40, 43, 46))
    sheet.paste(img, (x, y), mask)
    draw.text((x + 4, y + size + 6), os.path.splitext(os.path.basename(path))[0], fill=(20, 20, 20))
sheet.save(out)
print(out)
