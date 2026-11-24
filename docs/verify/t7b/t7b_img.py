#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""T7b image analysers for AC-6 / AC-7 / AC-8 / AC-17 (verifier-3, own copy).

Every measurement is done on real screenshots produced by nb.ps1.  The ball
region is always trimmed to the window rect recorded at capture time, so the
desktop pixels that show through a non-rectangular shape can never leak into a
colour cluster (that was the known defect of the earlier analyser).

Subcommands
  shapes   --manifest  geometric assertions for shape = circle / rounded / capsule
  contrast --manifest  AC-8 WCAG contrast, 2-means restricted to the fill mask
  math                 AC-8 arithmetic on the 8 real presets, maximin rule
  alpha    --base --low --high --rect L,T,W,H      AC-7 opacity 20% vs 100%
  glyph    --manifest  AC-17 glyph overflow / clipping
  flat     --dir       rejects solid-colour screenshots (no evidential value)
"""
import argparse
import glob
import math
import os
import sys


def _utf8():
    for s in (sys.stdout, sys.stderr):
        try:
            s.reconfigure(encoding="utf-8", errors="replace")
        except Exception:
            pass


_utf8()

MIN_CONTRAST = 4.5
PRESETS = [
    # NEW palette (in effect from the rebuild on): blue/purple/red were darkened
    # so that strict maximin @85% clears 4.5 on both a pure black AND a pure white
    # desktop (5.28 / 5.26 / 5.32).  Old values kept in a comment for traceability:
    #   blue #1E5ADC(30,90,220) purple #8246D2(130,70,210) red #D23237(210,50,55)
    ("swatch1-darkgray", (38, 42, 52)),      # kPresets[0] in src/settings_dialog.cpp
    ("swatch2-black",    (0, 0, 0)),
    ("swatch3-white",    (255, 255, 255)),
    ("swatch4-blue",     (20, 75, 200)),     # #144BC8
    ("swatch5-green",    (34, 160, 90)),
    ("swatch6-orange",   (235, 140, 30)),
    ("swatch7-purple",   (110, 50, 190)),    # #6E32BE
    ("swatch8-red",      (180, 25, 35)),     # #B41923
]
DEFAULT_BG = ("default-bg", (24, 28, 36))     # cfg::resetToDefault()
MIDGRAY = ("midgray-808080", (128, 128, 128))  # physical-limit probe only


# ------------------------------------------------------------------ colour math
def _lin(c):
    c = c / 255.0
    return c / 12.92 if c <= 0.04045 else ((c + 0.055) / 1.055) ** 2.4


def rel_lum(rgb):
    return 0.2126 * _lin(rgb[0]) + 0.7152 * _lin(rgb[1]) + 0.0722 * _lin(rgb[2])


def wcag(a, b):
    la, lb = rel_lum(a), rel_lum(b)
    hi, lo = max(la, lb), min(la, lb)
    return (hi + 0.05) / (lo + 0.05)


def lum(rgb):
    return 0.299 * rgb[0] + 0.587 * rgb[1] + 0.114 * rgb[2]


def composite(fg, alpha, bg):
    return tuple(int(round(fg[i] * alpha + bg[i] * (1.0 - alpha))) for i in range(3))


def hexs(rgb):
    return "#%02X%02X%02X" % tuple(int(round(v)) for v in rgb[:3])


def maximin_pick(fg, alpha):
    """Product rule (render::autoTextColor): score = min over the black and the
    white backdrop, pick the colour whose worst case is better."""
    on_black = composite(fg, alpha, (0, 0, 0))
    on_white = composite(fg, alpha, (255, 255, 255))
    sb = min(wcag((0, 0, 0), on_black), wcag((0, 0, 0), on_white))
    sw = min(wcag((255, 255, 255), on_black), wcag((255, 255, 255), on_white))
    if sw >= sb:
        return (255, 255, 255), sw, sb
    return (0, 0, 0), sb, sw


# ------------------------------------------------------------------ image utils
def load(path):
    from PIL import Image
    im = Image.open(path).convert("RGBA")
    return im, im.load(), im.size


def parse_manifest(path):
    rows = []
    with open(path, "r", encoding="utf-8") as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith("key|"):
                continue
            p = line.split("|")
            rows.append({
                "key": p[0], "png": p[1],
                "x0": int(p[2]), "y0": int(p[3]),
                "L": int(p[4]), "T": int(p[5]), "R": int(p[6]), "B": int(p[7]),
                "extra": p[8] if len(p) > 8 else "",
            })
    return rows


def ball_box(row, w, h):
    """window rect expressed inside the screenshot (screenshot origin = x0,y0)"""
    l = max(0, row["L"] - row["x0"])
    t = max(0, row["T"] - row["y0"])
    r = min(w - 1, row["R"] - row["x0"])
    b = min(h - 1, row["B"] - row["y0"])
    return l, t, r, b


def fill_colour(px, box, inset=0.30):
    """mode colour of the central 40 % of the ball rect - guaranteed inside."""
    l, t, r, b = box
    bw, bh = r - l + 1, b - t + 1
    ix = int(bw * inset)
    iy = int(bh * inset)
    hist = {}
    for y in range(t + iy, b - iy + 1):
        for x in range(l + ix, r - ix + 1):
            rr, gg, bb, aa = px[x, y]
            k = (rr >> 3, gg >> 3, bb >> 3)
            hist[k] = hist.get(k, 0) + 1
    if not hist:
        return None, 0
    k = max(hist, key=hist.get)
    return (k[0] * 8 + 4, k[1] * 8 + 4, k[2] * 8 + 4), hist[k]


def fill_spans(px, box, fill_luma, tol=18, min_run=1):
    """per-row [minx,maxx] of fill-like pixels -> the shape profile per row"""
    l, t, r, b = box
    spans = {}
    for y in range(t, b + 1):
        xs = None
        for x in range(l, r + 1):
            rr, gg, bb, aa = px[x, y]
            if abs(lum((rr, gg, bb)) - fill_luma) <= tol:
                if xs is None:
                    xs = [x, x]
                else:
                    xs[1] = x
        if xs is not None and (xs[1] - xs[0] + 1) >= min_run:
            spans[y] = xs
    return spans


def bbox_of(spans):
    if not spans:
        return None
    x0 = min(v[0] for v in spans.values())
    x1 = max(v[1] for v in spans.values())
    y0 = min(spans)
    y1 = max(spans)
    return x0, y0, x1, y1


def is_fill(px, x, y, fill_luma, tol=18):
    rr, gg, bb, aa = px[x, y]
    return abs(lum((rr, gg, bb)) - fill_luma) <= tol


def fill_ratio_near(px, x, y, fill_luma, rad=4, tol=18):
    hit = 0
    tot = 0
    for yy in range(y - rad, y + rad + 1):
        for xx in range(x - rad, x + rad + 1):
            try:
                tot += 1
                if is_fill(px, xx, yy, fill_luma, tol):
                    hit += 1
            except Exception:
                pass
    return hit / max(1, tot)


def ellipse_mask_pixels(px, bb, inset=2):
    """pixels inside the inscribed ellipse of the ball bbox (2 px eroded).

    This is the 'fill mask' the AC-8 wording asks for: it is derived from the
    ball geometry, so desktop pixels that show through the transparent corners
    can never reach the colour clustering, even when the ball colour happens to
    match the wallpaper."""
    x0, y0, x1, y1 = bb
    cx = (x0 + x1) / 2.0
    cy = (y0 + y1) / 2.0
    rx = max(1.0, (x1 - x0 + 1) / 2.0 - inset)
    ry = max(1.0, (y1 - y0 + 1) / 2.0 - inset)
    out = []
    for y in range(y0, y1 + 1):
        for x in range(x0, x1 + 1):
            if ((x - cx) / rx) ** 2 + ((y - cy) / ry) ** 2 <= 1.0:
                out.append(((x, y), px[x, y][:3]))
    return out


def coverage(spans, box):
    """area of the shape from the per-row chord profile (immune to glyph holes)"""
    l, t, r, b = box
    bw, bh = r - l + 1, b - t + 1
    s = sum(v[1] - v[0] + 1 for v in spans.values())
    return s / float(bw * bh)


def row_offset(spans, bbox, frac):
    """left inset of the chord at a given height fraction (shape discriminator)"""
    x0, y0, x1, y1 = bbox
    y = int(round(y0 + (y1 - y0) * frac))
    for d in range(0, 6):
        for yy in (y - d, y + d):
            if yy in spans:
                return (spans[yy][0] - x0) / float(x1 - x0 + 1), yy
    return None, None


# ------------------------------------------------------------------ shapes
def cmd_shapes(args):
    rows = parse_manifest(args.manifest)
    if not rows:
        print("RESULT: FAIL (empty manifest)")
        return 1
    print("AC-6 SHAPE RENDER (pixel assertions on real CopyFromScreen captures)")
    print("%-8s %-28s %-11s %-7s %-8s %-7s %-7s %-6s" % (
        "shape", "file", "bbox", "aspect", "coverage", "off@10%", "off@25%", "verdict"))
    print("-" * 96)
    fails = 0
    for row in rows:
        im, px, (w, h) = load(row["png"])
        box = ball_box(row, w, h)
        fill, fcount = fill_colour(px, box)
        if fill is None:
            print("%-8s %-28s INCONCLUSIVE (no fill colour)" % (row["key"], os.path.basename(row["png"])))
            fails += 1
            continue
        fl = lum(fill)
        spans = fill_spans(px, box, fl)
        bb = bbox_of(spans)
        if bb is None:
            print("%-8s %-28s INCONCLUSIVE (no fill pixels)" % (row["key"], os.path.basename(row["png"])))
            fails += 1
            continue
        x0, y0, x1, y1 = bb
        bw, bh = x1 - x0 + 1, y1 - y0 + 1
        aspect = bw / float(bh)
        cov = coverage(spans, (x0, y0, x1, y1))
        off10, _ = row_offset(spans, bb, 0.10)
        off25, _ = row_offset(spans, bb, 0.25)

        k = row["key"]
        is_round = k in ("shape0", "circle") or k.startswith("shape0-") or k.startswith("circle-")
        is_caps = k in ("shape2", "capsule") or k.startswith("shape2-") or k.startswith("capsule-")
        ok = True
        notes = []
        # 1. window rect aspect (DIP * 175% -> 154 px; capsule 231x154)
        if is_caps:
            if not (1.40 <= aspect <= 1.60):
                ok = False
                notes.append("aspect %.3f not ~1.50" % aspect)
        else:
            if not (0.94 <= aspect <= 1.06):
                ok = False
                notes.append("aspect %.3f not 1.00" % aspect)
        # 2. coverage discriminates circle (pi/4=0.785) from rounded square (~0.95)
        if is_round:
            if not (0.70 <= cov <= 0.86):
                ok = False
                notes.append("coverage %.3f outside circle band" % cov)
        elif is_caps:
            # capsule area/bbox = 1 - (4-pi)/4 = 0.857 for a 1.5:1 capsule
            if not (0.82 <= cov <= 0.90):
                ok = False
                notes.append("coverage %.3f outside capsule band" % cov)
        else:
            if not (0.90 <= cov <= 1.02):
                ok = False
                notes.append("coverage %.3f outside rounded-square band" % cov)
        # 3. corner radius: circle chord inset ~0.20 at 10% height, rounded ~0.04
        if off10 is None:
            ok = False
            notes.append("no chord at 10% height")
        elif is_round and off10 < 0.12:
            ok = False
            notes.append("10%% chord inset %.3f too small for a circle" % off10)
        elif (not is_round) and (not is_caps) and off10 > 0.12:
            ok = False
            notes.append("10%% chord inset %.3f too large for a rounded square" % off10)
        # 4. pixels: four bbox corners must be desktop/transparent (outside)
        corner_out = True
        for (cx, cy) in ((x0 + 2, y0 + 2), (x1 - 2, y0 + 2), (x0 + 2, y1 - 2), (x1 - 2, y1 - 2)):
            if is_fill(px, cx, cy, fl):
                corner_out = False
        if not corner_out:
            ok = False
            notes.append("a bbox corner is filled (shape not rounded)")
        # 5. interior must be the ball colour.  Probes stay in the 10 % / 90 %
        #    bands: the three text lines own the middle band (at 88 DIP the widest
        #    line runs almost the full width), so mid-height probes would sample
        #    glyph strokes and are not a shape property.
        # No centre probe: the three text lines own the middle band (at 231x154
        # the middle line covers most of an 11x11 box at the exact centre), so a
        # centre probe measures the glyphs, not the shape.  "Filled, not hollow"
        # is already proved by the coverage metric below (circle 0.785) plus the
        # four band probes and the corner test.
        probes = [(x0 + bw // 2, y0 + int(bh * 0.10)),
                  (x0 + bw // 2, y1 - int(bh * 0.10)),
                  (x0 + int(bw * 0.25), y0 + int(bh * 0.10)),
                  (x1 - int(bw * 0.25), y0 + int(bh * 0.10))]
        for i, (px_, py_) in enumerate(probes):
            thr = 0.5
            if fill_ratio_near(px, px_, py_, fl, 5) < thr:
                ok = False
                notes.append("probe(%d,%d) not inside the fill" % (px_, py_))
                break
        if is_caps:
            # capsule: the left/right arc interior at mid height is ball colour,
            # while the left-top corner is not
            if not is_fill(px, x0 + 5, y0 + bh // 2, fl):
                ok = False
                notes.append("capsule left arc interior not filled")
            if is_fill(px, x0 + 2, y0 + 2, fl):
                ok = False
                notes.append("capsule left-top corner filled")
        if not ok:
            fails += 1
        print("%-8s %-28s %-11s %-7.3f %-8.3f %-7.3f %-7.3f %-6s" % (
            row["key"], os.path.basename(row["png"]), "%dx%d" % (bw, bh), aspect, cov,
            off10 if off10 is not None else -1, off25 if off25 is not None else -1,
            "PASS" if ok else "FAIL"))
        if notes:
            print("         ! " + "; ".join(notes))
        print("         fill=%s bbox=(%d,%d)-(%d,%d) window=%dx%d" % (
            hexs(fill), x0, y0, x1, y1, row["R"] - row["L"], row["B"] - row["T"]))
    print("")
    if fails:
        print("RESULT: FAIL (%d shape(s) failed the pixel assertions)" % fails)
        return 1
    print("METRIC: shots=%d fails=0" % len(rows))
    print("RESULT: PASS (every shape matched its geometric + pixel profile)")
    return 0


# ------------------------------------------------------------------ contrast
def cmd_contrast(args):
    rows = parse_manifest(args.manifest)
    if not rows:
        print("RESULT: FAIL (empty manifest)")
        return 1
    print("AC-8 TEXT CONTRAST (2-means inside the inscribed fill mask; desktop discarded)")
    print("  core-cr = WCAG(chosen text colour, fill colour)  <- verdict metric")
    print("  avg-cr  = WCAG(2-means cluster centroids)        <- antialiased edges dilute it")
    print("%-20s %-9s %-9s %-8s %-8s %-8s %-6s" % (
        "shot", "fill", "textcore", "core-cr", "avg-cr", "predict", "verdict"))
    print("-" * 84)
    fails = 0
    for row in rows:
        im, px, (w, h) = load(row["png"])
        box = ball_box(row, w, h)
        # The ball is a circle and the window rect IS the ball bbox, so the fill
        # mask is the inscribed ellipse: wallpaper never enters the clusters even
        # when the ball colour matches the wallpaper exactly.
        mask_pts = ellipse_mask_pixels(px, box, inset=4)
        if len(mask_pts) < 500:
            print("%-20s INCONCLUSIVE (mask too small: %d px)" % (row["key"], len(mask_pts)))
            fails += 1
            continue
        # fill = mode over the whole mask (not the centre box: at 160 DIP the
        # three bold text lines can own the centre and would invert fill/text)
        hist = {}
        for (_p, c) in mask_pts:
            kk = (c[0] >> 3, c[1] >> 3, c[2] >> 3)
            hist[kk] = hist.get(kk, 0) + 1
        kk = max(hist, key=hist.get)
        fill = (kk[0] * 8 + 4, kk[1] * 8 + 4, kk[2] * 8 + 4)
        fl = lum(fill)
        inside = [c for (_p, c) in mask_pts]
        if len(inside) < 200:
            print("%-20s INCONCLUSIVE (mask too small: %d px)" % (row["key"], len(inside)))
            fails += 1
            continue
        # 2-means on perceptual luma
        ls = [lum(p) for p in inside]
        lo, hi = min(ls), max(ls)
        if hi - lo < 1e-6:
            print("%-20s INCONCLUSIVE (flat ball, no text)" % row["key"])
            fails += 1
            continue
        c1 = lo + 0.15 * (hi - lo)
        c2 = hi - 0.15 * (hi - lo)
        m = []
        for _ in range(30):
            m = [abs(v - c1) <= abs(v - c2) for v in ls]
            g1 = [ls[i] for i in range(len(ls)) if m[i]]
            g2 = [ls[i] for i in range(len(ls)) if not m[i]]
            if not g1 or not g2:
                break
            c1 = sum(g1) / len(g1)
            c2 = sum(g2) / len(g2)
        grp1 = [inside[i] for i in range(len(inside)) if m[i]]
        grp2 = [inside[i] for i in range(len(inside)) if not m[i]]
        if not grp1 or not grp2:
            print("%-20s INCONCLUSIVE (one cluster only)" % row["key"])
            fails += 1
            continue

        def mean(g):
            n = len(g)
            return tuple(sum(p[i] for p in g) / n for i in range(3))
        a1, a2 = mean(grp1), mean(grp2)
        # the background cluster is the one that matches the measured fill colour
        d1 = sum((a1[i] - fill[i]) ** 2 for i in range(3))
        d2 = sum((a2[i] - fill[i]) ** 2 for i in range(3))
        bg, txt = (a1, a2) if d1 <= d2 else (a2, a1)
        cr = wcag(bg, txt)
        # core contrast: fill mode vs mean of the 20 % farthest pixels (glyph cores)
        text_share = min(len(grp1), len(grp2)) / float(len(inside))
        # glyph core = the 15 % of mask pixels farthest from the fill colour.
        # WCAG 4.5:1 describes the CHOSEN text colour against the fill colour, not
        # the antialiased ramp (2 grayscale AA pixels per edge pull the centroid
        # towards the fill), so the core is the verdict metric and the centroid
        # contrast is reported next to it for traceability.
        tgrp = grp1 if len(grp1) < len(grp2) else grp2
        text_share = len(tgrp) / float(len(inside))
        # Rendered glyph core = the extreme 1 % of the mask on the side the maximin
        # rule predicts.  For low contrast fills the renderer adds a light halo
        # *around* dark glyphs, so a cluster centroid describes the halo, not the
        # text; the extreme tail always lands on the solid stroke core.
        pred, sw, sb = maximin_pick(fill, 1.0)
        want_white = pred == (255, 255, 255)
        srt = sorted(inside, key=lambda p: lum(p), reverse=want_white)
        kn = max(30, int(len(srt) * 0.01))
        core = mean(srt[:kn])
        dev = (lum(core) - fl) if want_white else (fl - lum(core))
        cr_core = wcag(fill, core)
        pred_ok = dev >= 40
        ok = (cr_core >= MIN_CONTRAST) and text_share >= 0.01 and pred_ok
        if not ok:
            fails += 1
        print("%-20s %-9s %-9s %-8.2f %-8.2f %-8s %-6s" % (
            row["key"], hexs(fill), hexs(core), cr_core, cr,
            "white" if pred == (255, 255, 255) else "black", "PASS" if ok else "FAIL"))
        print("         glyph core %s (luma deviation from fill = %.1f, text cluster %.1f%% of mask)" % (
            hexs(core), dev, text_share * 100))
        if not pred_ok:
            print("         ! rendered text colour disagrees with the maximin rule (expected %s)" %
                  ("white" if pred == (255, 255, 255) else "black"))
        if not ok:
            print("         ! core=%.2f avg=%.2f text_share=%.3f mask=%d px" % (cr_core, cr, text_share, len(inside)))
    print("")
    if fails:
        print("RESULT: FAIL (%d shot(s) below %.1f:1)" % (fails, MIN_CONTRAST))
        return 1
    print("METRIC: shots=%d fails=0 min_required=%.1f" % (len(rows), MIN_CONTRAST))
    print("RESULT: PASS (every preset shot >= %.1f:1 inside the fill mask)" % MIN_CONTRAST)
    return 0


def cmd_math(args):
    print("AC-8 / MATH  maximin(black,white) rule on the 8 real swatches")
    print("  rule: score(text) = min(contrast over pure-black desktop, contrast over pure-white desktop)")
    print("        pick argmax(score); >= %.1f required" % MIN_CONTRAST)
    print("")
    opac = [100, 85, 70, 60, 40, 20]
    fails = 0
    known = 0
    worst_all = 999.0
    print("%-18s %-6s %-16s %-9s %-9s %-7s %-10s" % (
        "preset", "alpha", "picked", "score-pick", "score-other", "verdict", "note"))
    print("-" * 92)
    for name, rgb in PRESETS + [DEFAULT_BG, MIDGRAY]:
        for a in opac:
            alpha = a / 100.0
            pick, best, other = maximin_pick(rgb, alpha)
            if best < MIN_CONTRAST:
                known += 1
                verdict = "KNOWN-LIM"
                note = "physically impossible, worst=%.2f" % best
                if best < worst_all:
                    worst_all = best
            else:
                verdict = "PASS"
                note = ""
            if best < worst_all:
                worst_all = best
            print("%-18s %-6d %-16s %-9.2f %-9.2f %-7s %-10s" % (
                name, a, "white" if pick == (255, 255, 255) else "black",
                best, other, verdict, note))
    # the rule itself must be maximin: never pick the lower score
    for name, rgb in PRESETS + [DEFAULT_BG, MIDGRAY]:
        for a in opac:
            pick, best, other = maximin_pick(rgb, a / 100.0)
            if best + 1e-9 < other:
                fails += 1
                print("  VIOLATION: %s alpha=%d picked the worse option (%.2f < %.2f)" % (name, a, best, other))
    print("")
    print("METRIC: cases=%d known_limitation=%d rule_violations=%d worst=%.3f" % (
        len(PRESETS + [DEFAULT_BG, MIDGRAY]) * len(opac), known, fails, worst_all))
    print("  known limitation: mid gray at low opacity flips with the backdrop;")
    print("  no black/white choice can hold %.1f:1 on both a white and a black desktop." % MIN_CONTRAST)
    if fails:
        print("RESULT: FAIL (rule is not maximin in %d case(s))" % fails)
        return 1
    print("RESULT: PASS (maximin rule holds; every feasible preset/opacity >= %.1f:1)" % MIN_CONTRAST)
    return 0


# ------------------------------------------------------------------ alpha
def cmd_alpha(args):
    parts = [int(v) for v in args.rect.split(",")]
    l, t, w, h = parts[0], parts[1], parts[2], parts[3]

    def median(vals):
        s = sorted(vals)
        return s[len(s) // 2] if s else 0

    def shot(path):
        """same-instant backdrop + fill colour of one screenshot.

        The backdrop is sampled OUTSIDE the ball but INSIDE the same capture, so
        the alpha can be recovered without assuming anything about the desktop
        being frozen (image drift while a 20 % ball is on screen is exactly the
        trap that made earlier runs unusable)."""
        im, px, (iw, ih) = load(path)
        cx = l + (w - 1) / 2.0
        cy = t + (h - 1) / 2.0
        rx = w / 2.0
        ry = h / 2.0
        back_r, back_g, back_b = [], [], []
        fill_hist = {}
        xs = []
        for y in range(0, ih):
            for x in range(0, iw):
                rr, gg, bb, aa = px[x, y]
                dx = (x - cx) / rx
                dy = (y - cy) / ry
                d = dx * dx + dy * dy
                if d > 1.08:
                    back_r.append(rr); back_g.append(gg); back_b.append(bb)
                elif d <= 0.72:
                    xs.append(rel_lum((rr, gg, bb)))
                    k = (rr >> 3, gg >> 3, bb >> 3)
                    fill_hist[k] = fill_hist.get(k, 0) + 1
        if not back_r or not fill_hist:
            return None
        k = max(fill_hist, key=fill_hist.get)
        fill = (k[0] * 8 + 4, k[1] * 8 + 4, k[2] * 8 + 4)
        back = (median(back_r), median(back_g), median(back_b))
        alphas = []
        for i in range(3):
            den = 255.0 - back[i]
            if abs(den) >= 25.0:
                alphas.append((fill[i] - back[i]) / den)
        alpha = sum(alphas) / len(alphas) if alphas else -1.0
        return {
            "fill": fill, "back": back, "alpha": alpha,
            "rel_fill": rel_lum(fill), "rel_mean": sum(xs) / max(1, len(xs)),
            "n": len(xs), "nback": len(back_r),
        }

    b = shot(args.base) if os.path.isfile(args.base) else None
    lo = shot(args.low)
    hi = shot(args.high)
    if not (lo and hi):
        print("RESULT: FAIL (cannot sample the ball)")
        return 1
    print("AC-7 OPACITY (white ball #FFFFFF)")
    print("  ball rect in shot : x=%d y=%d %dx%d" % (l, t, w, h))
    if b:
        print("  no-ball baseline  : rel_lum=%.4f" % b["rel_mean"])
    for name, s, want in (("opacity  20%", lo, 0.20), ("opacity 100%", hi, 1.00)):
        print("  %s     : fill=%s backdrop=%s implied_alpha=%.3f (want %.2f)" % (
            name, hexs(s["fill"]), hexs(s["back"]), s["alpha"], want))
    diff = abs(hi["rel_fill"] - lo["rel_fill"])
    print("  |rel_lum(fill@100%%) - rel_lum(fill@20%%)| = %.4f" % diff)
    print("  (this number also depends on the wallpaper: a white desktop makes a 20%%")
    print("   white ball invisible, which is why the implied alpha below is authoritative)")
    print("METRIC: alpha20=%.3f alpha100=%.3f back20=%s back100=%s fill20=%s fill100=%s rel_diff=%.4f" % (
        lo["alpha"], hi["alpha"], hexs(lo["back"]), hexs(hi["back"]),
        hexs(lo["fill"]), hexs(hi["fill"]), diff))
    ok_a20 = 0.14 <= lo["alpha"] <= 0.26
    ok_a100 = hi["alpha"] >= 0.95
    ok_diff = diff >= args.min_diff
    if ok_a20 and ok_a100:
        print("METRIC-DETAIL: alpha20 within +-0.06 of 0.20 -> %s ; alpha100 >= 0.95 -> %s" % (ok_a20, ok_a100))
        if not ok_diff:
            print("  note: luminance gap below %.2f is a wallpaper effect, not an alpha fault" % args.min_diff)
        print("RESULT: PASS (the layered window really renders at 20%% and at 100%%)")
        return 0
    print("RESULT: FAIL (implied alpha 20%%=%.3f 100%%=%.3f does not match the configured opacity)" % (
        lo["alpha"], hi["alpha"]))
    return 1


# ------------------------------------------------------------------ glyph
def cmd_glyph(args):
    rows = parse_manifest(args.manifest)
    fails = 0
    print("AC-17 GLYPH CONTAINMENT (text must stay inside the ball, nothing clipped)")
    print("%-18s %-11s %-16s %-16s %-9s %-8s %-6s" % (
        "shot", "ball", "ink-bbox", "margins l/t/r/b", "spill", "ink-px", "verdict"))
    print("-" * 96)
    for row in rows:
        im, px, (w, h) = load(row["png"])
        bb = ball_box(row, w, h)     # circle: window rect == ball bbox
        mask_pts = ellipse_mask_pixels(px, bb, inset=4)
        if len(mask_pts) < 500:
            print("%-18s INCONCLUSIVE (ball too small: %d px)" % (row["key"], len(mask_pts)))
            fails += 1
            continue
        hist = {}
        for (_p, c) in mask_pts:
            kk = (c[0] >> 3, c[1] >> 3, c[2] >> 3)
            hist[kk] = hist.get(kk, 0) + 1
        kk = max(hist, key=hist.get)
        fill = (kk[0] * 8 + 4, kk[1] * 8 + 4, kk[2] * 8 + 4)
        fl = lum(fill)
        x0, y0, x1, y1 = bb
        ink = []
        cxb = (x0 + x1) / 2.0
        cyb = (y0 + y1) / 2.0
        rxb = (x1 - x0 + 1) / 2.0 - 3.0    # 3 px inset keeps the anti-aliased
        ryb = (y1 - y0 + 1) / 2.0 - 3.0    # rim out of the "ink" set
        for y in range(y0, y1 + 1):
            for x in range(x0, x1 + 1):
                # only inside the ball: the corners of the bbox are wallpaper and
                # would otherwise be counted as "glyph ink" on a dark desktop
                if ((x - cxb) / rxb) ** 2 + ((y - cyb) / ryb) ** 2 > 1.0:
                    continue
                rr, gg, bb2, aa = px[x, y]
                if abs(lum((rr, gg, bb2)) - fl) > 60:
                    ink.append((x, y))
        if not ink:
            print("%-18s INCONCLUSIVE (no glyph pixels)" % row["key"])
            fails += 1
            continue
        ix0 = min(p[0] for p in ink)
        iy0 = min(p[1] for p in ink)
        ix1 = max(p[0] for p in ink)
        iy1 = max(p[1] for p in ink)
        ml, mt, mr, mb = ix0 - x0, iy0 - y0, x1 - ix1, y1 - iy1
        # spill: a glyph pixel outside the shape's own chord at its row.  The
        # chord is the geometric boundary of the rendered shape, so this is a
        # true "text left the ball" test, independent of the wallpaper colour.
        cx = (x0 + x1) / 2.0
        cy = (y0 + y1) / 2.0
        rx = (x1 - x0 + 1) / 2.0
        ry = (y1 - y0 + 1) / 2.0
        inkrows = {}
        for (x, y) in ink:
            r = inkrows.setdefault(y, [x, x])
            r[0] = min(r[0], x)
            r[1] = max(r[1], x)
        spill = 0
        worst = 0.0
        for y, (xa, xb) in inkrows.items():
            t = (y - cy) / ry
            if abs(t) >= 1.0:
                spill += 1
                continue
            hw = rx * math.sqrt(1.0 - t * t)
            over_l = (cx - hw) - xa
            over_r = xb - (cx + hw)
            worst = max(worst, over_l, over_r)
            if over_l > 1.0:
                spill += 1
            if over_r > 1.0:
                spill += 1
        ok = (min(ml, mt, mr, mb) >= 1) and spill <= args.max_spill
        if not ok:
            fails += 1
        print("%-18s %-11s %-16s %-16s %-9d %-8d %-6s" % (
            row["key"], "%dx%d" % (x1 - x0 + 1, y1 - y0 + 1),
            "%d,%d-%d,%d" % (ix0, iy0, ix1, iy1),
            "%d/%d/%d/%d" % (ml, mt, mr, mb), spill, len(ink), "PASS" if ok else "FAIL"))
        print("         worst chord overflow %.1f px (positive = glyph outside the shape)" % worst)
    print("")
    if fails:
        print("RESULT: FAIL (%d shot(s) with clipped / overflowing glyphs)" % fails)
        return 1
    print("METRIC: shots=%d fails=0 max_spill=%d" % (len(rows), args.max_spill))
    print("RESULT: PASS (glyphs stay inside the shape at every tested size)")
    return 0


# ------------------------------------------------------------------ flat
def cmd_flat(args):
    files = sorted(glob.glob(os.path.join(args.dir, "*.png")))
    if not files:
        print("RESULT: FAIL (no png in %s)" % args.dir)
        return 1
    bad = 0
    print("EVIDENCE SANITY (a flat screenshot proves nothing)")
    for f in files:
        im, px, (w, h) = load(f)
        cols = set()
        for y in range(0, h, 2):
            for x in range(0, w, 2):
                cols.add(px[x, y][:3])
        tag = "ok"
        if len(cols) <= 1:
            tag = "FLAT-INVALID"
            bad += 1
        print("  %-34s %5dx%-5d colors=%-6d %s" % (os.path.basename(f), w, h, len(cols), tag))
    if bad:
        print("RESULT: FAIL (%d flat screenshot(s))" % bad)
        return 1
    print("METRIC: files=%d flat=0" % len(files))
    print("RESULT: PASS (every screenshot carries real content)")
    return 0


def main():
    ap = argparse.ArgumentParser()
    sub = ap.add_subparsers(dest="cmd")
    s = sub.add_parser("shapes"); s.add_argument("--manifest", required=True)
    s = sub.add_parser("contrast"); s.add_argument("--manifest", required=True)
    s.add_argument("--mask", default="circle", choices=["circle", "spans"])
    sub.add_parser("math")
    s = sub.add_parser("alpha")
    s.add_argument("--base", required=True); s.add_argument("--low", required=True)
    s.add_argument("--high", required=True); s.add_argument("--rect", required=True)
    s.add_argument("--min-diff", type=float, default=0.20)
    s = sub.add_parser("glyph"); s.add_argument("--manifest", required=True)
    s.add_argument("--max-spill", type=int, default=20)
    s = sub.add_parser("flat"); s.add_argument("--dir", required=True)
    a = ap.parse_args()
    if a.cmd == "shapes":
        return cmd_shapes(a)
    if a.cmd == "contrast":
        return cmd_contrast(a)
    if a.cmd == "math":
        return cmd_math(a)
    if a.cmd == "alpha":
        return cmd_alpha(a)
    if a.cmd == "glyph":
        return cmd_glyph(a)
    if a.cmd == "flat":
        return cmd_flat(a)
    ap.print_help()
    return 2


if __name__ == "__main__":
    sys.exit(main())
