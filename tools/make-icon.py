#!/usr/bin/env python3
"""Draw the ZeroSend app icon set: LocalSend's own logo (the dashed ring and
centre dot, from localsend/app/assets/img/logo-512.png) with "Zero" set in the
centre dot.

The CardputerZero spec asks for a 256x256 icon; APPLaunch itself draws app
icons at 100px (centred tile) and 80px (side tiles), so all three are produced.
Full-bleed and square: the launcher applies its own shape.

    python3 tools/make-icon.py
"""
from PIL import Image, ImageDraw, ImageFont
import os
import sys

OUTPUTS = [
    ("assets/images/zerosend.png", 256),
    ("assets/images/zerosend_100.png", 100),
    ("assets/images/zerosend_80.png", 80),
]

repo = sys.argv[1] if len(sys.argv) > 1 else os.path.join(
    os.path.dirname(os.path.abspath(__file__)), "..")

logo = Image.open(os.path.join(repo, "assets/icon-source/localsend-logo-512.png")).convert("RGBA")
SIZE = logo.width  # 512
image = Image.new("RGBA", (SIZE, SIZE), (255, 255, 255, 255))
image.alpha_composite(logo)

# The centre dot spans x 150..362 in the 512px logo: "Zero" fills ~78% of it,
# so it still reads on the 80px side tile.
dot_center, dot_width = (256, 255), 212
draw = ImageDraw.Draw(image)
font_path = os.path.join(repo, "assets/fonts/inter-bold.ttf")
size = 120
while True:
    font = ImageFont.truetype(font_path, size)
    box = draw.textbbox((0, 0), "Zero", font=font)
    if box[2] - box[0] <= dot_width * 0.78 or size <= 20:
        break
    size -= 2
w, h = box[2] - box[0], box[3] - box[1]
draw.text((dot_center[0] - w / 2 - box[0], dot_center[1] - h / 2 - box[1]), "Zero", font=font, fill="white")

for rel, out in OUTPUTS:
    image.resize((out, out), Image.LANCZOS).save(os.path.join(repo, rel))
    print("wrote {} ({}x{}, text {}px at 512)".format(rel, out, out, size))
