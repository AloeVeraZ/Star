"""Regenerates the README images from real renderer output.

    ./build.sh && ./preview sheet frames && python3 make_previews.py frames

Writes ../../face-expressions.png (a labelled sheet of expressions) and puts the
idle frame into the enclosure mockups ../../face-preview.png and .svg.
Needs Pillow (pip install pillow).
"""
import base64
import io
import os
import re
import subprocess
import sys

from PIL import Image, ImageDraw

frames = sys.argv[1] if len(sys.argv) > 1 else "frames"
here = os.path.dirname(os.path.abspath(__file__))
root = os.path.normpath(os.path.join(here, "..", ".."))

names = ["idle", "happy", "sad", "angry", "surprised", "dizzy", "sleeping",
         "look_right", "look_down_left", "shy", "confused", "petted"]
subprocess.run([sys.executable, os.path.join(here, "sheet.py"),
                os.path.join(root, "face-expressions.png"), "--scale", "1", "--cols", "6"]
               + [os.path.join(frames, n + ".ppm") for n in names], check=True)

# Mockup screen: centre (256, 253), radius 126 px in the 512 px preview.
idle = Image.open(os.path.join(frames, "idle.ppm")).convert("RGB")
size = 252
screen = idle.resize((size, size), Image.LANCZOS)
mask = Image.new("L", (size * 4, size * 4), 0)
ImageDraw.Draw(mask).ellipse((0, 0, size * 4 - 1, size * 4 - 1), fill=255)
mask = mask.resize((size, size), Image.LANCZOS)
png_path = os.path.join(root, "face-preview.png")
mock = Image.open(png_path).convert("RGB")
mock.paste(screen, (256 - size // 2, 253 - size // 2), mask)
mock.save(png_path)

buf = io.BytesIO()
idle.save(buf, format="PNG", optimize=True)
data = base64.b64encode(buf.getvalue()).decode()
svg_path = os.path.join(root, "face-preview.svg")
svg = open(svg_path).read()
image = ('  <clipPath id="screen"><circle cx="256" cy="253" r="126"/></clipPath>\n'
         '  <image clip-path="url(#screen)" x="130" y="127" width="252" height="252" '
         'image-rendering="optimizeQuality" href="data:image/png;base64,' + data + '"/>\n')
# Everything after the screen circle is the face; replace it with the render.
svg = re.sub(r'(<circle cx="256" cy="253" r="126"[^>]*/>\n)(.*)(</svg>)',
             lambda m: m.group(1) + image + m.group(3), svg, flags=re.S)
open(svg_path, "w").write(svg)
print("updated", png_path, svg_path)
