import sys, json, math
from PIL import Image

def annulus_alpha(pw, pb, cx, cy, r0, r1):
    a = Image.open(pw).convert('RGB'); b = Image.open(pb).convert('RGB')
    pa, pb_ = a.load(), b.load()
    diffs = []
    for y in range(a.size[1]):
        for x in range(a.size[0]):
            d = math.hypot(x - cx, y - cy)
            if r0 <= d <= r1:
                diffs.append(sum(pa[x, y]) / 3.0 - sum(pb_[x, y]) / 3.0)
    diffs.sort()
    n = len(diffs)
    return dict(n=n, mean=sum(diffs) / n, median=diffs[n // 2],
                alpha_mean=sum(diffs) / n / 255.0, alpha_median=diffs[n // 2] / 255.0)

# ---- independent WCAG recompute (sRGB relative luminance, threshold 0.04045) ----
def lin(c):
    c = c / 255.0
    return c / 12.92 if c <= 0.04045 else ((c + 0.055) / 1.055) ** 2.4

def lum(rgb):
    r, g, b = rgb
    return 0.2126 * lin(r) + 0.7152 * lin(g) + 0.0722 * lin(b)

def cr(a, b):
    la, lb = lum(a), lum(b)
    hi, lo = max(la, lb), min(la, lb)
    return (hi + 0.05) / (lo + 0.05)

def comp(fg, bg, a):
    return tuple(int(round(f * a + b * (1 - a))) for f, b in zip(fg, bg))

def maximin(bg, opacity):
    a = opacity / 100.0
    eb = comp(bg, (0, 0, 0), a)
    ew = comp(bg, (255, 255, 255), a)
    white = (255, 255, 255); black = (0, 0, 0)
    sw = min(cr(white, eb), cr(white, ew))
    sb = min(cr(black, eb), cr(black, ew))
    pick = 'white' if sw >= sb else 'black'
    return dict(bg='#%02X%02X%02X' % bg, op=opacity, on_black='#%02X%02X%02X' % eb,
                on_white='#%02X%02X%02X' % ew, score_white=round(sw, 3), score_black=round(sb, 3),
                best=round(max(sw, sb), 3), pick=pick, ok=max(sw, sb) >= 4.5)

PRESETS = {
    'deepgray': (38, 42, 52), 'black': (0, 0, 0), 'white': (255, 255, 255),
    'blue': (20, 75, 200), 'green': (34, 160, 90), 'orange': (235, 140, 30),
    'purple': (110, 50, 190), 'red': (180, 25, 35),
}

if __name__ == '__main__':
    mode = sys.argv[1]
    if mode == 'alpha':
        print(json.dumps(annulus_alpha(sys.argv[2], sys.argv[3], 77, 77, 64, 72), indent=1))
    elif mode == 'ac8':
        rows = []
        for k, v in PRESETS.items():
            rows.append(maximin(v, 85))
        for r in rows:
            print('%-9s %s op85 max=%.3f pick=%s ok=%s' % (r['bg'], r['on_black'], r['best'], r['pick'], r['ok']))
        print('--- 20%% spots ---')
        for k in ('white', 'deepgray'):
            print(maximin(PRESETS[k], 20))
        print('--- default config bg #181C24 @85 ---')
        print(maximin((24, 28, 36), 85))
        print('--- min over presets =', min(maximin(v, 85)['best'] for v in PRESETS.values()))
