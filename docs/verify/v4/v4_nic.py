#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Idle-link background traffic probe (verifier-4).

Reuses the READ-ONLY parts of docs/verify/check_ac3_net.py (MIB_IF_ROW2 layout +
select_rows dedup rule) and never calls its cmd_compare, so docs/verify/net.txt
is not touched. Prints the machine's NIC aggregate rate while this script itself
generates no traffic: that is the floor the ball can never read below, and it
explains why "ball vs curl" can diverge on a shared host.
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

import check_ac3_net as nic  # noqa: E402  (import only; no file writes)


def human(bps):
    v = float(bps)
    for unit in ("B/s", "KB/s", "MB/s"):
        if v < 1024.0:
            return "%.2f %s" % (v, unit)
        v /= 1024.0
    return "%.2f GB/s" % v


seconds = 12
rows = nic.select_rows(nic.read_rows())
print("NIC BACKGROUND PROBE  %s" % time.strftime("%Y-%m-%d %H:%M:%S"))
print("  sizeof(MIB_IF_ROW2)=%d  eligible_ifaces=%d" % (ctypes.sizeof(nic.MIB_IF_ROW2), len(rows)))
for r in rows:
    print("  iface idx=%-3d type=%-3d mtu=%-5d %s" % (r.InterfaceIndex, r.Type, r.Mtu, r.Alias))
if not rows:
    print("RESULT: BLOCKED (no eligible interface)")
    sys.exit(2)

pin, pout = nic.totals(rows)
print("")
print("  t_s   down            up")
t_prev = time.time()
tot = 0
for i in range(seconds):
    time.sleep(1.0)
    cur_in, cur_out = nic.totals(nic.select_rows(nic.read_rows()))
    now = time.time()
    el = max(1e-3, now - t_prev)
    t_prev = now
    d_in = max(0, cur_in - pin)
    d_out = max(0, cur_out - pout)
    pin, pout = cur_in, cur_out
    tot += d_in / el
    print("  %-5d %-15s %s" % (i + 1, human(d_in / el), human(d_out / el)))

avg = tot / seconds
print("")
print("METRIC: background_down_bps=%d (%.2f KB/s) over %ds" % (avg, avg / 1024, seconds))
print("  any AC-3 sample below this floor is unreachable on this host")
print("RESULT: PASS (background measured)")
