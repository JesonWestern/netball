#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
AC-8 text contrast >= 4.5:1 (WCAG 2.1).

Two independent checks:

  1) --shots DIR   real screenshots (docs/verify/shape-*.png). For each image the
                   opaque ball pixels are split into two luminance clusters
                   (2-means). The smaller cluster is the text, the bigger one the
                   background; the WCAG contrast between the two cluster mean
                   colours must be >= 4.5.
  2) --math        pure arithmetic on the 8 preset background colours: composite
                   rgb over a desktop backdrop at the configured opacity, then
                   apply the product rule "pick black or white, whichever has the
                   higher contrast" and assert the winner is >= 4.5:1.

Exit codes: 0 PASS / 1 FAIL / 2 SKIP
"""

import argparse
import glob
import os
import sys


def _force_utf8():
    """Keep non-ASCII paths readable when PowerShell captures the pipe (cp936)."""
    for stream in (sys.stdout, sys.stderr):
        try:
            stream.reconfigure(encoding="utf-8", errors="replace")
        except Exception:
            pass


_force_utf8()

MIN_CONTRAST = 4.5

# 8 preset background colours used by the product (documented in the report)
PRESETS = [
    ("black",       (0, 0, 0)),
    ("dark-gray",   (32, 32, 32)),
    ("mid-gray",    (128, 128, 128)),
    ("light-gray",  (200, 200, 200)),
    ("white",       (255, 255, 255)),
    ("blue",        (0, 96, 192)),
    ("green",       (16, 160, 96)),
    ("red",         (192, 48, 48)),
]
OPACITIES = [1.0, 0.6, 0.2]
BACKDROPS = [
    ("desktop-white", (255, 255, 255)),
    ("desktop-dark",  (24, 24, 32)),
    ("desktop-photo", (96, 112, 128)),
]


# ------------------------------------------------------------------ wcag math
def _lin(c):
    c = c / 255.0
    return c / 12.92 if c <= 0.04045 else ((c + 0.055) / 1.055) ** 2.4


def rel_luminance(rgb):
    r, g, b = rgb[:3]
    return 0.2126 * _lin(r) + 0.7152 * _lin(g) + 0.0722 * _lin(b)


def luma(rgb):
    """Perceptual luma (0-255). Used for pixel clustering, NOT for WCAG numbers."""
    r, g, b = rgb[:3]
    return 0.299 * r + 0.587 * g + 0.114 * b


def contrast_ratio(a, b):
    la, lb = rel_luminance(a), rel_luminance(b)
    hi, lo = max(la, lb), min(la, lb)
    return (hi + 0.05) / (lo + 0.05)


def composite(fg, alpha, bg):
    return tuple(int(round(fg[i] * alpha + bg[i] * (1.0 - alpha))) for i in range(3))


def pick_text_color(bg_rgb):
    """Product rule: choose whichever of black/white has higher contrast."""
    cb = contrast_ratio(bg_rgb, (0, 0, 0))
    cw = contrast_ratio(bg_rgb, (255, 255, 255))
    return ((0, 0, 0), cb) if cb >= cw else ((255, 255, 255), cw)


def hexs(rgb):
    return "#%02X%02X%02X" % tuple(rgb[:3])


# --------------------------------------------------------------- math check
def run_math():
    print("AC-8 / MATH  autoTextColor rule on 8 preset colours (composited over 3 backdrops)")
    print("")
    header = "%-14s %-6s %-14s %-9s %-9s %-8s %-6s" % (
        "preset", "alpha", "composited", "contrast-B", "contrast-W", "picked", "verdict")
    print(header)
    print("-" * len(header))
    worst = 999.0
    worst_desc = ""
    fails = 0
    for backdrop_name, backdrop in BACKDROPS:
        print("[backdrop: %s %s]" % (backdrop_name, hexs(backdrop)))
        for name, rgb in PRESETS:
            for alpha in OPACITIES:
                comp = composite(rgb, alpha, backdrop)
                cb = contrast_ratio(comp, (0, 0, 0))
                cw = contrast_ratio(comp, (255, 255, 255))
                picked, best = pick_text_color(comp)
                ok = best >= MIN_CONTRAST
                if not ok:
                    fails += 1
                if best < worst:
                    worst = best
                    worst_desc = "%s alpha=%.1f over %s -> %s" % (name, alpha, backdrop_name, hexs(comp))
                print("%-14s %-6.1f %-14s %-9.2f %-9.2f %-8s %-6s" % (
                    name, alpha, hexs(comp), cb, cw,
                    "black" if picked == (0, 0, 0) else "white",
                    "PASS" if ok else "FAIL"))
        print("")
    print("  worst contrast found : %.3f:1  (%s)" % (worst, worst_desc))
    print("  theoretical floor    : 4.58:1 (the black/white optimum is never below 4.58)")
    print("METRIC: worst_contrast=%.3f cases=%d fails=%d" % (worst, len(PRESETS) * len(OPACITIES) * len(BACKDROPS), fails))
    if fails == 0:
        print("RESULT: PASS (autoTextColor rule guarantees >= %.1f:1 for every preset/opacity/backdrop)" % MIN_CONTRAST)
        return 0
    print("RESULT: FAIL (%d cases below %.1f:1)" % (fails, MIN_CONTRAST))
    return 1


# -------------------------------------------------------------- image check
def kmeans2(pixels, iters=25):
    """pixels: list of (r,g,b). Split on luminance into 2 clusters."""
    lums = [rel_luminance(p) for p in pixels]
    lo, hi = min(lums), max(lums)
    if hi - lo < 1e-6:
        return None
    c1 = lo + 0.15 * (hi - lo)
    c2 = hi - 0.15 * (hi - lo)
    a_mask = []
    for _ in range(iters):
        a_mask = [abs(l - c1) <= abs(l - c2) for l in lums]
        g1 = [lums[i] for i in range(len(lums)) if a_mask[i]]
        g2 = [lums[i] for i in range(len(lums)) if not a_mask[i]]
        if not g1 or not g2:
            return None
        c1 = sum(g1) / len(g1)
        c2 = sum(g2) / len(g2)
    grp1 = [pixels[i] for i in range(len(pixels)) if a_mask[i]]
    grp2 = [pixels[i] for i in range(len(pixels)) if not a_mask[i]]
    def mean(grp):
        n = len(grp)
        return tuple(sum(p[i] for p in grp) / n for i in range(3))
    return mean(grp1), mean(grp2), len(grp1), len(grp2)


def analyse_image(path, min_alpha=200):
    """Measure WCAG contrast of the ball's own fill vs its glyphs.

    A raw 2-means over the whole capture is wrong for a non-rectangular ball:
    23 % of a 154x154 circle capture is the DESKTOP seen through the transparent
    corners, and those pixels join the glyph cluster and dilute it (measured
    error: up to -55 % contrast, enough to turn a 5.18:1 PASS into a 4.50:1 FAIL).

    Fix: build a geometric mask from the fill colour - per image row the fill
    pixels give the shape's [minX, maxX] span (a circle's chord, a rect's width),
    then cluster only pixels inside that span. The desktop is excluded by
    construction, whatever it looks like.
    """
    from PIL import Image
    im = Image.open(path).convert("RGBA")
    w, h = im.size
    px = im.load()
    pad = max(2, min(w, h) // 30)

    # 1. dominant colour inside the window area = the ball fill
    hist = {}
    for y in range(pad, h - pad):
        for x in range(pad, w - pad):
            r, g, b, a = px[x, y]
            if a < min_alpha:
                continue
            key = (r >> 3, g >> 3, b >> 3)
            hist[key] = hist.get(key, 0) + 1
    if not hist:
        return None
    fill_key = max(hist, key=hist.get)
    fill = (fill_key[0] * 8 + 4, fill_key[1] * 8 + 4, fill_key[2] * 8 + 4)
    fill_luma = luma(fill)

    # 2. per-row span of fill-like pixels
    rows = {}
    for y in range(pad, h - pad):
        xs = []
        for x in range(pad, w - pad):
            r, g, b, a = px[x, y]
            if a < min_alpha:
                continue
            if abs(luma((r, g, b)) - fill_luma) <= 18:
                xs.append(x)
        if len(xs) >= 3:
            rows[y] = (min(xs), max(xs))

    # 3. cluster only inside those spans (plus a 1px slack for anti-aliased glyph edges)
    inside = []
    for y, (x0, x1) in rows.items():
        for x in range(max(pad, x0 - 1), min(w - pad, x1 + 2)):
            r, g, b, a = px[x, y]
            if a >= min_alpha:
                inside.append((r, g, b))
    if len(inside) < 100:
        return None

    # 4. mode, not mean: anti-aliased glyph edges would drag a cluster mean toward
    #    the fill (measured: #000000 read back as #030E0A, -10% contrast)
    exact = {}
    for p in inside:
        exact[p] = exact.get(p, 0) + 1
    bg = max(exact, key=exact.get)
    bg_luma = luma(bg)
    ink = {p: n for p, n in exact.items() if abs(luma(p) - bg_luma) > 40}
    if not ink:
        return None
    text = max(ink, key=ink.get)
    cr = contrast_ratio(bg, text)
    return {
        "size": (w, h),
        "opaque_px": len(inside),
        "mask_px": len(inside),
        "fill": fill,
        "bg": bg,
        "text": text,
        "bg_px": exact[bg],
        "text_px": ink[text],
        "contrast": cr,
    }


def run_shots(pattern, min_alpha):
    files = sorted(glob.glob(pattern)) if any(ch in pattern for ch in "*?[") else \
        (sorted(glob.glob(os.path.join(pattern, "*.png"))) if os.path.isdir(pattern) else [pattern])
    files = [f for f in files if os.path.isfile(f)]
    print("AC-8 / SHOTS  WCAG contrast measured on real screenshots")
    print("  pattern : %s" % pattern)
    print("  files   : %d" % len(files))
    if not files:
        print("RESULT: SKIP (no screenshots yet; run check_screenshot.ps1 per shape/colour preset)")
        return 2
    try:
        from PIL import Image  # noqa: F401
    except ImportError:
        print("RESULT: SKIP (Pillow not installed: pip install pillow)")
        return 2

    print("")
    hdr = "%-44s %-9s %-9s %-10s %-6s" % ("file", "bg", "text", "contrast", "verdict")
    print(hdr)
    print("-" * len(hdr))
    fails = 0
    measured = 0
    for f in files:
        try:
            r = analyse_image(f, min_alpha)
        except Exception as e:
            print("%-44s ERROR %s" % (os.path.basename(f), e))
            fails += 1
            continue
        if r is None:
            print("%-44s SKIP (no separable colour clusters)" % os.path.basename(f))
            continue
        measured += 1
        ok = r["contrast"] >= MIN_CONTRAST
        if not ok:
            fails += 1
        print("%-44s %-9s %-9s %-10.2f %-6s" % (
            os.path.basename(f), hexs(r["bg"]), hexs(r["text"]), r["contrast"], "PASS" if ok else "FAIL"))
    print("")
    print("  measured: %d image(s)" % measured)
    if measured == 0:
        print("RESULT: SKIP (screenshots present but none analysable - check that the window is not occluded)")
        return 2
    print("METRIC: images=%d fails=%d min_required=%.1f" % (measured, fails, MIN_CONTRAST))
    if fails == 0:
        print("RESULT: PASS (every screenshot >= %.1f:1)" % MIN_CONTRAST)
        return 0
    print("RESULT: FAIL (%d screenshot(s) below %.1f:1)" % (fails, MIN_CONTRAST))
    return 1


def run_selftest():
    """Toolchain self-test: analyse a synthetic in-memory image (NOT evidence).

    Proves the clustering + WCAG path actually computes, so a later SKIP/FAIL
    cannot be blamed on a broken analyser. The PNG lives in %TEMP%, never in
    docs/verify.
    """
    import tempfile
    try:
        from PIL import Image, ImageDraw
    except ImportError:
        print("RESULT: SKIP (Pillow not installed: pip install pillow)")
        return 2

    tmp = os.path.join(tempfile.gettempdir(), "nb_ac8_selftest.png")
    im = Image.new("RGBA", (140, 140), (0, 0, 0, 255))
    d = ImageDraw.Draw(im)
    d.rectangle([24, 58, 116, 82], fill=(255, 255, 255, 255))   # white "text" on black
    im.save(tmp)

    print("AC-8 / SELFTEST  analyser sanity on a synthetic black/white image")
    print("  temp file : %s (diagnostic only, never shipped as evidence)" % tmp)
    r = analyse_image(tmp)
    if r is None:
        print("RESULT: FAIL (analyser returned no clusters on a trivial black/white image)")
        return 1
    print("  bg=%s text=%s contrast=%.2f" % (hexs(r["bg"]), hexs(r["text"]), r["contrast"]))
    expect = contrast_ratio((0, 0, 0), (255, 255, 255))
    print("  expected  : %.2f (pure black vs pure white)" % expect)
    tol = 0.5
    if abs(r["contrast"] - expect) <= tol:
        print("METRIC: selftest_contrast=%.2f expected=%.2f" % (r["contrast"], expect))
        print("RESULT: PASS (analyser reproduces the WCAG 21:1 reference)")
        return 0
    print("RESULT: FAIL (measured %.2f, expected %.2f +-%.1f)" % (r["contrast"], expect, tol))
    return 1


def main():
    ap = argparse.ArgumentParser(description="AC-8 WCAG contrast verification")
    ap.add_argument("--shots", metavar="GLOB_OR_DIR", default=None,
                    help="screenshot glob/dir, e.g. docs/verify/shape-*.png")
    ap.add_argument("--math", action="store_true", help="run the pure-arithmetic preset check")
    ap.add_argument("--all", action="store_true", help="run both (default when no mode given)")
    ap.add_argument("--selftest", action="store_true", help="analyser sanity check (no evidence produced)")
    ap.add_argument("--min-alpha", type=int, default=200)
    args = ap.parse_args()

    if args.selftest:
        return run_selftest()

    here = os.path.dirname(os.path.abspath(__file__))
    shots = args.shots or os.path.join(here, "shape-*.png")

    if not args.shots and not args.math:
        args.math = True
        args.shots = shots

    rc_math = None
    rc_shots = None
    if args.math:
        rc_math = run_math()
        print("")
    if args.shots:
        rc_shots = run_shots(args.shots, args.min_alpha)

    codes = [c for c in (rc_math, rc_shots) if c is not None]
    if 1 in codes:
        return 1
    if 0 in codes:
        return 0
    return 2


if __name__ == "__main__":
    sys.exit(main())
