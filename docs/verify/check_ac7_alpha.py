#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
AC-7 transparency actually applied.

Compares two real screenshots of the same ball - one taken with opacity = 20 %,
one with opacity = 100 % - and measures the effective brightness of the ball core.
A layered window at low opacity composites heavily with the desktop, so the core
brightness must differ from the fully opaque rendering by a wide margin
(default >= 20 percentage points of the 0-255 range).

Also reports the mean alpha of the PNG (informational: CopyFromScreen returns
a fully opaque screenshot, so alpha stays 255 and brightness is the real metric).

Exit codes: 0 PASS / 1 FAIL / 2 SKIP
"""

import argparse
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

REL_DIFF_MIN = 0.20  # 20 % of full scale


def lin(c):
    c = c / 255.0
    return c / 12.92 if c <= 0.04045 else ((c + 0.055) / 1.055) ** 2.4


def rel_luminance(rgb):
    return 0.2126 * lin(rgb[0]) + 0.7152 * lin(rgb[1]) + 0.0722 * lin(rgb[2])


def core_stats(path, core_ratio=0.6):
    from PIL import Image
    im = Image.open(path).convert("RGBA")
    w, h = im.size
    px = im.load()
    cw, ch = max(1, int(w * core_ratio)), max(1, int(h * core_ratio))
    x0, y0 = (w - cw) // 2, (h - ch) // 2
    lum_sum = 0.0
    a_sum = 0.0
    n = 0
    for y in range(y0, y0 + ch):
        for x in range(x0, x0 + cw):
            r, g, b, a = px[x, y]
            lum_sum += rel_luminance((r, g, b))
            a_sum += a
            n += 1
    if n == 0:
        return None
    return {
        "w": w, "h": h,
        "lum": lum_sum / n,          # 0..1 relative luminance
        "bright_pct": 100.0 * lum_sum / n,
        "alpha": a_sum / n,
    }


def run_selftest():
    """Toolchain self-test with two synthetic grey images in %TEMP% (NOT evidence)."""
    import tempfile
    try:
        from PIL import Image
    except ImportError:
        print("RESULT: SKIP (Pillow not installed: pip install pillow)")
        return 2

    d = tempfile.gettempdir()
    dark = os.path.join(d, "nb_ac7_selftest_dark.png")
    light = os.path.join(d, "nb_ac7_selftest_light.png")
    Image.new("RGB", (80, 80), (32, 32, 32)).save(dark)
    Image.new("RGB", (80, 80), (240, 240, 240)).save(light)

    print("AC-7 / SELFTEST  brightness comparator sanity")
    print("  dark  : %s" % dark)
    print("  light : %s" % light)
    a = core_stats(dark)
    b = core_stats(light)
    if not a or not b:
        print("RESULT: FAIL (core_stats returned nothing)")
        return 1
    diff = abs(b["lum"] - a["lum"])
    print("  lum dark=%.4f light=%.4f diff=%.4f" % (a["lum"], b["lum"], diff))
    if diff >= REL_DIFF_MIN:
        print("METRIC: selftest_diff=%.4f vs_min=%.2f" % (diff, REL_DIFF_MIN))
        print("RESULT: PASS (comparator detects a 208/255 brightness gap)")
        return 0
    print("RESULT: FAIL (comparator blind to an obvious brightness gap)")
    return 1


def main():
    ap = argparse.ArgumentParser(description="AC-7 opacity verification (20% vs 100%)")
    ap.add_argument("--low", metavar="PNG", default=None, help="screenshot at opacity 20%")
    ap.add_argument("--high", metavar="PNG", default=None, help="screenshot at opacity 100%")
    ap.add_argument("--min-diff", type=float, default=REL_DIFF_MIN,
                    help="minimum |relative luminance| difference, default 0.20")
    ap.add_argument("--selftest", action="store_true", help="comparator sanity check (no evidence produced)")
    args = ap.parse_args()

    if args.selftest:
        return run_selftest()

    here = os.path.dirname(os.path.abspath(__file__))
    low = args.low or os.path.join(here, "alpha-20.png")
    high = args.high or os.path.join(here, "alpha-100.png")

    print("AC-7 OPACITY CHECK (20% vs 100%)")
    print("  low  : %s" % low)
    print("  high : %s" % high)
    print("  min relative-luminance difference: %.2f (%.0f%% of full scale)" % (args.min_diff, args.min_diff * 100))

    missing = [p for p in (low, high) if not os.path.isfile(p)]
    if missing:
        for p in missing:
            print("  MISSING: %s" % p)
        print("RESULT: SKIP (need two screenshots; run check_screenshot.ps1 at opacity 20% and 100%)")
        return 2

    try:
        from PIL import Image  # noqa: F401
    except ImportError:
        print("RESULT: SKIP (Pillow not installed: pip install pillow)")
        return 2

    try:
        a = core_stats(low)
        b = core_stats(high)
    except Exception as e:
        print("RESULT: FAIL (cannot read screenshots: %s)" % e)
        return 1

    if not a or not b:
        print("RESULT: FAIL (empty image data)")
        return 1

    print("")
    print("  %-28s %-12s %-12s %-10s" % ("file", "size", "brightness", "alpha"))
    for name, s in ((os.path.basename(low), a), (os.path.basename(high), b)):
        print("  %-28s %-12s %-12s %-10.1f" % (
            name, "%dx%d" % (s["w"], s["h"]), "%.2f%%" % s["bright_pct"], s["alpha"]))

    diff = abs(b["lum"] - a["lum"])
    print("")
    print("  |lum(100%%) - lum(20%%)| = %.4f  (%.1f%% of full scale)" % (diff, diff * 100))
    print("METRIC: lum_low=%.4f lum_high=%.4f rel_diff=%.4f min_required=%.2f" % (
        a["lum"], b["lum"], diff, args.min_diff))

    if diff >= args.min_diff:
        print("RESULT: PASS (opacity visibly changes the rendering)")
        return 0
    print("RESULT: FAIL (opacity 20% and 100% render almost identically -> alpha not applied, or both shots at same opacity)")
    return 1


if __name__ == "__main__":
    sys.exit(main())
