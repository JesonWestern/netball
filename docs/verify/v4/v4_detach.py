#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Generic detached runner: v4_detach.py <log> <script> [args...]

The agent console broadcasts Ctrl+C to attached processes; anything longer than
a few seconds must run with its own (hidden) console.
"""
import os
import subprocess
import sys

DETACH = 0x00000008 | 0x08000000
CHILD = 0x08000000          # CREATE_NO_WINDOW survives; DETACHED breaks powershell

if len(sys.argv) < 3:
    print("usage: v4_detach.py <log> <script.py> [args...]")
    sys.exit(2)

log_path = os.path.abspath(sys.argv[1])
script = os.path.abspath(sys.argv[2])
args = sys.argv[3:]
f = open(log_path, "w", encoding="utf-8")
p = subprocess.Popen([sys.executable, "-u", script] + args,
                     stdout=f, stderr=subprocess.STDOUT, stdin=subprocess.DEVNULL,
                     creationflags=CHILD, close_fds=True)
print("LAUNCHED pid=%d script=%s log=%s" % (p.pid, os.path.basename(script), log_path))
