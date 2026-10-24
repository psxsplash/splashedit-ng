# Writes spark.png: a 32x8 sprite sheet of four 8x8 cells, a soft dot that
# shrinks from cell 0 to cell 3 (white core, warm rim, transparent corners).
# usage: python3 make_sheet.py
import os
from PIL import Image

img = Image.new('RGBA', (32, 8), (0, 0, 0, 0))
for c in range(4):
    r = 4.0 - c * 0.7
    for y in range(8):
        for x in range(8):
            d = ((x - 3.5) ** 2 + (y - 3.5) ** 2) ** 0.5
            if d <= r:
                t = d / r
                col = (255, int(255 - 120 * t), int(255 - 230 * t), 255)
                img.putpixel((c * 8 + x, y), col)
img.save(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'spark.png'))
