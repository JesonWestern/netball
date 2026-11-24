#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Isolate why capture() returned rc=0 with empty output and no PNG.

Two encoding traps found on this host:
  * python stdout defaults to gbk -> printing the U+FFFD from a mojibake decode
    raises UnicodeEncodeError.
  * PowerShell 5.1 writes OEM/ANSI bytes unless the script sets
    [Console]::OutputEncoding, so decoding with strict utf-8 yields U+FFFD.
"""
import os
import subprocess
import sys

for _s in (sys.stdout, sys.stderr):
    try:
        _s.reconfigure(encoding="utf-8", errors="replace")
    except Exception:
        pass

HERE = os.path.dirname(os.path.abspath(__file__))
VERIFY = os.path.dirname(HERE)
CAP = os.path.join(VERIFY, "check_screenshot.ps1")
OUT = os.path.join(VERIFY, "media-v4")
DETACH = 0x00000008 | 0x08000000
NO_WIN = 0x08000000

os.makedirs(OUT, exist_ok=True)


def decode(b):
    for enc in ("utf-8", "cp936", "latin-1"):
        try:
            return b.decode(enc)
        except Exception:
            continue
    return repr(b)


args = ["-OutFile", "X", "-ClassName", "NetBallBallWnd", "-ProcessName", "NetBall_v4"]
cmd = ["powershell", "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", CAP] + args

for label, flags in (("none", 0), ("CREATE_NO_WINDOW", NO_WIN), ("DETACHED", DETACH)):
    png = os.path.join(OUT, "t-%s.png" % label)
    c = list(cmd)
    c[5] = png
    try:
        p = subprocess.run(c, capture_output=True, timeout=90, creationflags=flags)
        out = decode((p.stdout or b"") + (p.stderr or b""))
        print("[%s] rc=%r out_len=%d exists=%s" % (label, p.returncode, len(out), os.path.exists(png)))
        for line in out.splitlines():
            print("   | " + line)
    except Exception as e:
        print("[%s] EXCEPTION %r" % (label, e))
    print("")

print("python=%s" % sys.version.split()[0])
print("stdout_encoding=%s" % sys.stdout.encoding)
