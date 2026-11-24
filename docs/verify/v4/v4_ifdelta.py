#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Per-interface InOctets delta dump (verifier-4).

AC-3 keeps failing with ball/NIC ~4-5x the curl application rate. If several
adapters advance by exactly the same byte count in the same second, they are
mirrors of one link (VPN / Npcap / WFP virtual adapters), and the product's
"OperStatus && type not in {24,131} && FilterInterface==0" filter cannot remove
them - which would make the displayed speed N times too high.
"""
import ctypes
import os
import sys
import time

for _s in (sys.stdout, sys.stderr):
    try:
        _s.reconfigure(encoding="utf-8", errors="replace")
    except Exception:
        pass

HERE = os.path.dirname(os.path.abspath(__file__))
VERIFY = os.path.dirname(HERE)
sys.path.insert(0, VERIFY)
import check_ac3_net as nic  # noqa: E402

secs = int(sys.argv[1]) if len(sys.argv) > 1 else 8

rows = nic.read_rows()
print("PER-INTERFACE InOctets DELTA  %s  (%d rows total)" % (time.strftime("%H:%M:%S"), len(rows)))
print("")

kept = nic.select_rows(rows)
keep_idx = {r.InterfaceIndex for r in kept}
print("%-4s %-3s %-5s %-4s %-4s %-30s %s"
      % ("idx", "typ", "mtu", "oper", "filt", "alias", "kept"))
for r in rows:
    print("%-4d %-3d %-5d %-4d %-4d %-30s %s"
          % (r.InterfaceIndex, r.Type, r.Mtu, r.OperStatus,
             int(r.InterfaceAndOperStatusFlags.FilterInterface),
             r.Alias, "YES" if r.InterfaceIndex in keep_idx else "-"))
print("")

prev = {r.InterfaceIndex: int(r.InOctets) for r in rows}
print("second-by-second InOctets delta (bytes)")
for i in range(secs):
    time.sleep(1.0)
    cur = nic.read_rows()
    line = []
    mirror = {}
    for r in cur:
        idx = r.InterfaceIndex
        d = int(r.InOctets) - prev.get(idx, int(r.InOctets))
        prev[idx] = int(r.InOctets)
        if d > 2048:
            line.append("%d:%d" % (idx, d))
            mirror.setdefault(d, []).append(idx)
    print("  t+%d  %s" % (i + 1, "  ".join(line) if line else "(idle)"))
    for d, idxs in mirror.items():
        if len(idxs) > 1:
            print("        MIRROR: interfaces %s all moved by exactly %d bytes" % (idxs, d))
print("")
print("RESULT: PASS (per-interface deltas dumped)")
