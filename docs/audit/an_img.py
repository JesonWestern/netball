import sys, os, json
from PIL import Image

def analyze(path, bg=(255, 0, 0), tol=90):
    im = Image.open(path).convert('RGB')
    w, h = im.size
    px = im.load()
    mask = [[False] * w for _ in range(h)]
    n = 0
    for y in range(h):
        for x in range(w):
            r, g, b = px[x, y]
            # reddish background of the ball (fill), tolerate AA halo
            if abs(r - bg[0]) <= tol and abs(g - bg[1]) <= tol and abs(b - bg[2]) <= tol and r > 120:
                mask[y][x] = True
                n += 1
    cov = n / float(w * h)
    # chord at 10% and 90% of height
    def row_extent(y):
        xs = [x for x in range(w) if mask[y][x]]
        if not xs:
            return None
        return (xs[0] / float(w), (w - 1 - xs[-1]) / float(w))
    e10 = row_extent(max(1, int(h * 0.10)))
    e90 = row_extent(min(h - 2, int(h * 0.90)))
    e50 = row_extent(h // 2)
    # corner test
    corners = [mask[0][0], mask[0][w - 1], mask[h - 1][0], mask[h - 1][w - 1]]
    # text ink inside mask: pixels far from bg color and not transparent-ish
    ink = 0
    for y in range(h):
        for x in range(w):
            if not mask[y][x]:
                continue
            r, g, b = px[x, y]
            if (r > 200 and g > 200 and b > 200) or (r < 40 and g < 40 and b < 40):
                ink += 1
    colors = len(im.getcolors(maxcolors=1000000) or [])
    return dict(file=os.path.basename(path), size='%dx%d' % (w, h), coverage=round(cov, 4),
                inset10=round(e10[0], 4) if e10 else None,
                inset90=round(e90[0], 4) if e90 else None,
                mid_extent=e50, corners=mask and corners, ink=ink, colors=colors)

if __name__ == '__main__':
    out = []
    for p in sys.argv[1:]:
        out.append(analyze(p))
    print(json.dumps(out, indent=1, ensure_ascii=False))
