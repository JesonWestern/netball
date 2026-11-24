#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Parameter sweep over verifier_ocr.ps1 for one png.

The shared OCR settings (-Scale 6 -Threshold 110 -CropLeftPct 0.18) were tuned on
a 154 px ball. The build under test renders an 88 px ball, so the crop is taken
out of the digits themselves and the glyphs are ~7 px tall; this sweep finds out
whether the number is machine-readable at all with any setting.
"""
import os
import re
import subprocess
import sys

for _s in (sys.stdout, sys.stderr):
    try:
        _s.reconfigure(encoding="utf-8", errors="replace")
    except Exception:
        pass

HERE = os.path.dirname(os.path.abspath(__file__))
VERIFY = os.path.dirname(HERE)
OCR = os.path.join(VERIFY, "verifier_ocr.ps1")
CHILD = 0x08000000


def ocr(png, scale, thr, crop):
    cmd = ["powershell", "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", OCR,
           "-Path", png, "-Scale", str(scale)]
    if thr > 0:
        cmd += ["-Threshold", str(thr)]
    if crop > 0:
        cmd += ["-CropLeftPct", str(crop)]
    p = subprocess.run(cmd, capture_output=True, creationflags=CHILD)
    b = (p.stdout or b"") + (p.stderr or b"")
    for enc in ("utf-8", "cp936"):
        try:
            txt = b.decode(enc)
            break
        except Exception:
            txt = b.decode("utf-8", "replace")
    text = re.search(r"OCR_TEXT: (.*)", txt)
    mem = re.search(r"BALL_MEM_PCT=(-?\d+)", txt)
    down = re.search(r"BALL_DOWN_BPS=(\d+)", txt)
    return {"rc": p.returncode,
            "text": text.group(1).strip() if text else "",
            "mem": mem.group(1) if mem else "-",
            "down": down.group(1) if down else "-",
            "raw": txt}


png = sys.argv[1]
print("OCR SWEEP: %s" % png)
print("| scale | thr | crop | rc | OCR_TEXT | mem | down |")
print("|---|---|---|---|---|---|---|")
for scale in (4, 6, 8, 10, 12):
    for thr in (0, 110, 140):
        for crop in (0.0, 0.10, 0.18):
            r = ocr(png, scale, thr, crop)
            print("| %d | %d | %.2f | %d | %s | %s | %s |"
                  % (scale, thr, crop, r["rc"], r["text"] or "(empty)", r["mem"], r["down"]))
            if r["mem"] != "-" and r["down"] != "-":
                print("")
                print("FIRST READABLE COMBO: scale=%d threshold=%d crop=%.2f" % (scale, thr, crop))
                print(r["raw"])
                sys.exit(0)
print("")
print("NO COMBO PRODUCED A READABLE NUMBER")
