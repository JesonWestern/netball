#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Bandwidth probe (verifier-4).

Two environment hazards are handled here and reused by the formal AC-3 driver:
  * shared console: a Ctrl+C event in the agent console is broadcast to every
    process attached to it and kills curl (observed rc=0xC000013A at t=5.09s).
    Everything therefore runs with DETACHED_PROCESS + redirected stdio.
  * cloudflare cap: bytes=104857600 returns 1 byte, use 20000000.
"""
import os
import subprocess
import sys
import tempfile
import time

URL = "https://speed.cloudflare.com/__down?bytes=20000000"
DETACH = 0x00000008 | 0x08000000          # DETACHED_PROCESS | CREATE_NO_WINDOW


def run(limit, tag):
    out = os.path.join(tempfile.gettempdir(), "nb_v4_curl_%s.txt" % tag)
    cmd = ["curl.exe", "-s", "-L", "--max-time", str(limit), "-o", "NUL",
           "-w", "%{size_download} %{time_total} %{speed_download} %{http_code}", URL]
    t0 = time.time()
    with open(out, "w") as f:
        p = subprocess.Popen(cmd, stdout=f, stderr=subprocess.STDOUT, creationflags=DETACH)
        rc = p.wait()
    el = time.time() - t0
    raw = ""
    try:
        with open(out) as f:
            raw = f.read().strip()
    except Exception:
        pass
    print("max_time=%ds rc=%s wall=%.2fs out=%r (file=%s)" % (limit, rc, el, raw, out))
    parts = raw.split()
    if len(parts) >= 2 and rc == 0:
        size = float(parts[0]); secs = float(parts[1])
        rate = size / secs if secs else 0
        print("  bytes=%d  curl_time=%.3f  rate=%.1f B/s (%.2f KB/s)" % (int(size), secs, rate, rate / 1024))
        print("  extrapolated_bytes_in_20s=%d  (gate needs >=300000)" % int(rate * 20))
        return rate
    return 0.0


print("BANDWIDTH PROBE  %s" % time.strftime("%Y-%m-%d %H:%M:%S"))
print("python=%s" % sys.version.split()[0])
rates = []
for lim in (8, 12):
    rates.append(run(lim, str(lim)))
    print("")
if rates and min(rates) > 0:
    r = sum(rates) / len(rates)
    print("avg_rate=%.1f B/s -> 20s window ~ %d bytes; 25s ~ %d bytes"
          % (r, int(r * 20), int(r * 25)))
    print("gate(300000 B in >=20s) reachable=%s" % ("YES" if r * 20 >= 300000 else "NO"))
print("DONE")
