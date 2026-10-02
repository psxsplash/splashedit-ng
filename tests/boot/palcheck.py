# Check that a rendered texture shows only colours from its palette.
# usage: palcheck.py shot.png x0 y0 x1 y1 decoded.png
# decoded.png is `splashpack-cli texstats --out`. Each pixel in the rectangle is
# matched to the nearest palette colour; the GPU's ordered dither moves a pixel
# by at most one 5-bit step (9 in 8-bit units), so anything further away means
# the CLUT or the texel indices were read wrong.
import sys
import numpy as np
from PIL import Image

shot, x0, y0, x1, y1, dec = sys.argv[1], *map(int, sys.argv[2:6]), sys.argv[6]
im = np.asarray(Image.open(shot).convert('RGB')).astype(int)
d = np.asarray(Image.open(dec).convert('RGB')).astype(int)
pal = np.unique(((d * 31 + 127) // 255 * 255 // 31).reshape(-1, 3), axis=0)
px = im[y0:y1 + 1, x0:x1 + 1].reshape(-1, 3)
dist = np.abs(px[:, None, :] - pal[None, :, :]).max(-1).min(-1)
print(f'palette {len(pal)} pixels {len(px)} exact {(dist == 0).mean():.3f} '
      f'within-dither {(dist <= 9).mean():.3f} max {dist.max()}')
sys.exit(0 if dist.max() <= 9 else 1)
