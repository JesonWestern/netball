#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Detached launcher for v4_run.py.

The agent console broadcasts Ctrl+C to every attached process, which killed a
25 s curl at t=5.09 s (rc=0xC000013A). This launcher starts the driver with
DETACHED_PROCESS so it has no console at all, then returns immediately; the
caller polls the log file.
"""
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
DETACH = 0x00000008 | 0x08000000

if len(sys.argv) < 3:
    print("usage: v4_launch.py <log> <driver args...>")
    sys.exit(2)

log_path = sys.argv[1]
driver_args = sys.argv[2:]
f = open(log_path, "w", encoding="utf-8")
p = subprocess.Popen([sys.executable, "-u", os.path.join(HERE, "v4_run.py")] + driver_args,
                     stdout=f, stderr=subprocess.STDOUT, stdin=subprocess.DEVNULL,
                     creationflags=DETACH, cwd=HERE, close_fds=True)
print("LAUNCHED pid=%d log=%s args=%s" % (p.pid, log_path, " ".join(driver_args)))
