#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Find OCR settings that read BOTH the speed and the memory number.

The shared defaults (Scale 6 / Threshold 110 / CropLeftPct 0.18) read
"内 存 82 ％" in one shot and "内 存 ％" (digits dropped) in the next three,
so the memory line needs a different binarisation or upscale. Sweeping offline
over saved pngs keeps the online sampling window free of OCR cost.
"""
import glob
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
PRELUDE = (
    "Add-Type -TypeDefinition 'using System;using System.Runtime.InteropServices;"
    "public class V4A2{[DllImport(\"user32.dll\")]"
    "public static extern IntPtr SetThreadDpiAwarenessContext(IntPtr c);}';"
    "[void][V4A2]::SetThreadDpiAwarenessContext([IntPtr]-4);"
)


def ocr(png, scale, thr, crop):
    line = (PRELUDE + " & '" + OCR + "' -Path '" + png + "' -Scale " + str(scale)
            + " -Threshold " + str(thr) + " -CropLeftPct " + str(crop))
    p = subprocess.run(["powershell", "-NoProfile", "-ExecutionPolicy", "Bypass",
                        "-Command", line], capture_output=True, creationflags=CHILD)
    b = (p.stdout or b"") + (p.stderr or b"")
    txt = None
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
            "mem": int(mem.group(1)) if mem else -1,
            "down": int(down.group(1)) if down else 0}


pngs = sys.argv[1:] if len(sys.argv) > 1 else sorted(glob.glob(os.path.join(VERIFY, "media-v4", "mem-*.png")))
if not pngs:
    print("no pngs")
    sys.exit(2)

combos = [(s, t, c) for s in (6, 8, 10, 12) for t in (0, 90, 110, 140) for c in (0.18, 0.24)]
print("TUNING on %d png(s): %s" % (len(pngs), ", ".join(os.path.basename(p) for p in pngs)))
print("| scale | thr | crop | mem_hits | down_hits | example |")
print("|---|---|---|---|---|---|")
best = None
for (s, t, c) in combos:
    mem_hits = down_hits = 0
    sample = ""
    for p in pngs:
        r = ocr(p, s, t, c)
        if r["mem"] > 0:
            mem_hits += 1
        if r["down"] > 0:
            down_hits += 1
        if not sample and r["text"]:
            sample = r["text"][:46]
    print("| %d | %d | %.2f | %d/%d | %d/%d | %s |"
          % (s, t, c, mem_hits, len(pngs), down_hits, len(pngs), sample))
    if best is None or (mem_hits + down_hits) > best[0]:
        best = (mem_hits + down_hits, s, t, c, mem_hits, down_hits)
print("")
if best:
    print("BEST: scale=%d threshold=%d crop=%.2f  mem=%d/%d down=%d/%d"
          % (best[1], best[2], best[3], best[4], len(pngs), best[5], len(pngs)))
