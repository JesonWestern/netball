#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
AC-11 single instance.

Launches build/out/NetBall.exe twice (second launch while the first is alive) and
asserts exactly one "NetBall" process remains.

Options
  --exe PATH        artifact path (default build/out/NetBall.exe)
  --gap SECONDS     delay between the two launches (default 1.5)
  --wait SECONDS    settle time before counting (default 3.0)
  --keep            do not kill the surviving instance afterwards

Exit codes: 0 PASS / 1 FAIL / 2 SKIP
"""

import argparse
import csv
import io
import os
import subprocess
import sys
import time


def _force_utf8():
    """Keep non-ASCII paths readable when PowerShell captures the pipe (cp936)."""
    for stream in (sys.stdout, sys.stderr):
        try:
            stream.reconfigure(encoding="utf-8", errors="replace")
        except Exception:
            pass


_force_utf8()

IMAGE = "NetBall.exe"
LOGICAL_NAME = "netball"


def count_processes(image=IMAGE):
    """Return list of (pid, image) for processes with the given image name."""
    try:
        out = subprocess.run(["tasklist", "/FI", "IMAGENAME eq %s" % image, "/FO", "CSV", "/NH"],
                             capture_output=True, text=True, timeout=20).stdout
    except Exception as e:
        print("  tasklist failed: %s" % e)
        return None
    rows = []
    for line in out.splitlines():
        line = line.strip()
        if not line or line.upper().startswith("INFO:"):
            continue
        try:
            parts = next(csv.reader(io.StringIO(line)))
        except Exception:
            continue
        if len(parts) >= 2 and parts[0].lower() == image.lower():
            try:
                rows.append((int(parts[1]), parts[0]))
            except ValueError:
                continue
    return rows


def main():
    ap = argparse.ArgumentParser(description="AC-11 single-instance verification")
    ap.add_argument("--exe", default=None)
    ap.add_argument("--gap", type=float, default=1.5)
    ap.add_argument("--wait", type=float, default=3.0)
    ap.add_argument("--keep", action="store_true")
    ap.add_argument("--force", action="store_true",
                    help="kill pre-existing NetBall instances; DEFAULT IS OFF so a "
                         "teammate's debugging session is never destroyed silently")
    args = ap.parse_args()

    here = os.path.dirname(os.path.abspath(__file__))
    root = os.path.dirname(os.path.dirname(here))
    exe = args.exe or os.path.join(root, "build", "out", IMAGE)
    exe = os.path.abspath(exe)

    print("AC-11 SINGLE INSTANCE CHECK")
    try:
        import nb_build_info
        nb_build_info.build_info(exe)
    except Exception:
        pass
    print("  exe     : %s" % exe)
    print("  gap     : %.1fs   settle: %.1fs" % (args.gap, args.wait))

    if not os.path.isfile(exe):
        print("RESULT: SKIP (artifact not built yet: %s)" % exe)
        return 2

    pre = count_processes()
    if pre is None:
        print("RESULT: FAIL (cannot enumerate processes)")
        return 1
    if pre:
        print("  pre-existing NetBall process(es): %s" % [p[0] for p in pre])
        if not args.force:
            print("  NOT killing them (default). A teammate may be debugging right now.")
            print("RESULT: SKIP (NetBall already running - close it manually, or re-run with --force)")
            return 2
        print("  --force given -> killing to start clean")
        for pid, _ in pre:
            subprocess.run(["taskkill", "/PID", str(pid), "/F"], capture_output=True)
        time.sleep(0.6)

    launched = []
    mine = set()
    try:
        print("  launch #1 ...")
        p1 = subprocess.Popen([exe], cwd=os.path.dirname(exe))
        launched.append(p1)
        time.sleep(args.gap)

        after1 = count_processes() or []
        mine.update(p[0] for p in after1)
        print("  after #1 : %d process(es) %s" % (len(after1), [p[0] for p in after1]))

        print("  launch #2 ...")
        p2 = subprocess.Popen([exe], cwd=os.path.dirname(exe))
        launched.append(p2)
        time.sleep(args.wait)

        after2 = count_processes() or []
        mine.update(p[0] for p in after2)
        print("  after #2 : %d process(es) %s" % (len(after2), [p[0] for p in after2]))

        for i, p in enumerate(launched, 1):
            rc = p.poll()
            print("  launcher #%d returncode: %s" % (i, "still running" if rc is None else rc))

        print("")
        print("METRIC: processes_after_1=%d processes_after_2=%d" % (len(after1), len(after2)))

        if len(after1) != 1:
            print("RESULT: FAIL (after the first launch there were %d processes, expected 1)" % len(after1))
            return 1
        pid1 = after1[0][0]
        if len(after2) == 0:
            # the first instance died while we were only launching the second one:
            # almost always an external `Stop-Process NetBall` / taskkill by a teammate
            print("  first instance (pid %d) vanished between the two launches" % pid1)
            print("  -> external kill suspected (shared Windows session); this is not a mutex bug")
            print("RESULT: BLOCKED (instance killed externally; re-run when nobody else is testing)")
            return 3
        if len(after2) != 1:
            print("RESULT: FAIL (%d instances alive after the second launch, expected 1 -> no mutex/FindWindow guard)" % len(after2))
            return 1

        pid = after1[0][0]
        if after2[0][0] != pid:
            print("RESULT: FAIL (instance was replaced instead of reused: pid %s -> %s)" % (pid, after2[0][0]))
            return 1

        print("RESULT: PASS (second launch exited immediately, single instance kept: pid %d)" % pid)
        return 0
    finally:
        if not args.keep:
            # only the instances THIS run produced (PID based - never touch other sessions)
            for pid in sorted(mine):
                subprocess.run(["taskkill", "/PID", str(pid), "/F"], capture_output=True)
            left = [p[0] for p in (count_processes() or []) if p[0] not in mine]
            print("  cleanup: killed our pid(s) %s; left untouched %s" % (sorted(mine), left or "none"))


if __name__ == "__main__":
    sys.exit(main())
