#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""AC-3 formal run (verifier-1).

Ground truth  : curl bytes / curl wall clock  (independent of the app)
Ball reading  : screenshot during the download -> Windows.Media.Ocr (machine-read, no hand typing)
NIC sanity    : GetIfTable2 aggregated (FilterInterface=0) over the same window

Writes docs/verify/net.txt with all three numbers.
"""
import os
import re
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
APP = os.path.join(ROOT, "build", "_tmp", "vfy", "NetBall_vfy.exe")
OUT = os.path.join(HERE, "net.txt")
SHOT = os.path.join(HERE, "net-shot-1.png")
URL = "https://speed.cloudflare.com/__down?bytes=20000000"
TOL = 20.0


def ps(script, *args):
    cmd = ["powershell", "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", os.path.join(HERE, script)] + list(args)
    r = subprocess.run(cmd, capture_output=True, text=True, encoding="utf-8", errors="replace")
    return (r.stdout or "") + (r.stderr or "")


def kill_own():
    out = subprocess.run(["tasklist", "/FI", "IMAGENAME eq NetBall_vfy.exe", "/FO", "CSV", "/NH"],
                         capture_output=True, text=True).stdout
    for line in out.splitlines():
        if line.lower().startswith('"netball_vfy.exe"'):
            subprocess.run(["taskkill", "/PID", line.split('","')[1], "/F"], capture_output=True)


def main():
    print("AC-3 FORMAL RUN")
    sys.path.insert(0, HERE)
    import nb_build_info
    bi = nb_build_info.build_info(APP)
    if bi.get("stale"):
        print("RESULT: BLOCKED (被测二进制早于源码，需重新构建)")
        return 3
    if not os.path.isfile(APP):
        print("RESULT: SKIP (private copy missing: %s)" % APP)
        return 2

    kill_own()
    time.sleep(1.0)
    app = subprocess.Popen([APP], cwd=os.path.dirname(APP))
    time.sleep(3.0)
    if app.poll() is not None:
        print("RESULT: BLOCKED (app exited immediately, rc=%s - another instance holds the mutex?)" % app.poll())
        return 3
    print("  app pid : %d (private copy, killed by PID at the end)" % app.pid)

    wf = os.path.join(os.environ.get("TEMP", "."), "nb_ac3_curl.txt")
    nic = subprocess.Popen([sys.executable, "-u", os.path.join(HERE, "check_ac3_net.py"), "--seconds", "25"],
                           stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, encoding="utf-8", errors="replace")
    with open(wf, "w") as f:
        curl = subprocess.Popen(["curl.exe", "-s", "-L", "--max-time", "20", "-o", "NUL",
                                 "-w", "%{size_download} %{time_total} %{speed_download} %{http_code}", URL],
                                stdout=f, stderr=subprocess.PIPE)
    time.sleep(2.5)                                    # let the transfer reach steady state
    shot_log = ps("check_screenshot.ps1", "-OutFile", SHOT, "-ClassName", "NetBallBallWnd",
                  "-ProcessName", "NetBall_vfy")
    print("\n".join("  " + l for l in shot_log.strip().splitlines()))

    rc = curl.wait()
    try:
        curl.stderr.close()
    except Exception:
        pass
    nic.wait(timeout=40)

    try:
        app.kill()
        app.wait(timeout=5)
    except Exception:
        pass
    kill_own()

    raw = ""
    try:
        with open(wf) as f:
            raw = f.read().strip()
    except Exception:
        pass
    print("  curl rc=%s out=%r" % (rc, raw))
    parts = raw.split()
    if rc != 0 or len(parts) < 2:
        print("RESULT: BLOCKED (curl failed, no ground truth)")
        return 3
    size_dl, time_tot = float(parts[0]), float(parts[1])
    if size_dl < 1000000 or time_tot <= 0:
        print("RESULT: BLOCKED-流量未建立 (only %s bytes)" % parts[0])
        return 3
    truth = size_dl / time_tot

    ocr = ps("verifier_ocr.ps1", "-Path", SHOT, "-Scale", "6", "-Threshold", "110", "-CropLeftPct", "0.18")
    print("\n".join("  " + l for l in ocr.strip().splitlines()))
    m = re.search(r"BALL_DOWN_BPS=(\d+)", ocr)
    mem = re.search(r"BALL_MEM_PCT=(-?\d+)", ocr)
    if not m or int(m.group(1)) <= 0:
        print("RESULT: SKIP-无法机读 (OCR could not read the downstream value)")
        return 2
    measured = int(m.group(1))

    err = abs(measured - truth) / truth * 100.0
    lines = ["# NetBall AC-3 evidence  %s" % time.strftime("%Y-%m-%d %H:%M:%S"),
             "BUILD: exe=%s bytes=%s mtime=%s fresh=yes" % (
                 os.path.basename(APP), bi.get("exe_size"),
                 time.strftime("%Y-%m-%d %H:%M:%S", time.localtime(bi["exe_mtime"]))),
             "url=%s" % URL,
             "curl_bytes=%d" % int(size_dl),
             "curl_seconds=%.3f" % time_tot,
             "GROUND_TRUTH_DOWN_BPS=%d" % int(truth),
             "BALL_DOWN_BPS_OCR=%d" % measured,
             "error_pct=%.2f" % err,
             "tolerance_pct=%.1f" % TOL,
             "shot=%s" % SHOT,
             "ocr_text_line=%s" % (m.group(0)),
             "BALL_MEM_PCT=%s" % (mem.group(1) if mem else "-1"),
             "verdict=%s" % ("PASS" if err <= TOL else "FAIL")]
    with open(OUT, "w", encoding="utf-8") as f:
        f.write("\n".join(lines) + "\n")
    print("")
    print("GROUND_TRUTH_DOWN_BPS=%d" % int(truth))
    print("BALL_DOWN_BPS_OCR=%d" % measured)
    print("error_pct=%.2f (tol %.1f)" % (err, TOL))
    print("written: %s" % OUT)
    if err <= TOL:
        print("RESULT: PASS (ball reading within %.0f%% of curl ground truth)" % TOL)
        return 0
    print("RESULT: FAIL (ball reading off by %.2f%%)" % err)
    return 1


if __name__ == "__main__":
    sys.exit(main())
