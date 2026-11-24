import sys
from PIL import Image

path = sys.argv[1]
im = Image.open(path).convert("RGBA")
px = im.load()
w, h = im.size
print("size", w, h)
cx, cy = w // 2, h // 2
for dy in (-120, -100, -80, -60, -40, -20, 0, 20, 40, 60, 80, 100, 120):
    print(cy + dy, [px[cx + dx, cy + dy][:3] for dx in (-60, -40, -20, 0, 20, 40, 60)])
# histogram of luma in the central 60 %
import collections
hist = collections.Counter()
for y in range(cy - 84, cy + 84, 2):
    for x in range(cx - 84, cx + 84, 2):
        r, g, b, a = px[x, y]
        hist[(r, g, b)] += 1
print("top colours:", hist.most_common(8))
