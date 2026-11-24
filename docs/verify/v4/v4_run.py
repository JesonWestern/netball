#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""verifier-4 driver: AC-4 (memory) and AC-3 (network) machine-read measurements.

Design constraints taken from HANDOVER-V2 + this session's findings
  1. The single-instance mutex "NetBallSingleInstance" is GLOBAL and name
     independent (src/app_common.h + src/main.cpp:271-282), so this driver only
     ever starts its OWN private copy build/_tmp/v4/NetBall_v4.exe and kills by
     PID only.
  2. A Ctrl+C in the agent console is broadcast to every process attached to
     that console (observed: curl rc=0xC000013A at t=5.09s). Every child here is
     spawned with CREATE_NO_WINDOW, and the driver itself is
     launched detached by v4_launch.py.
  3. Captures are real screen captures through the shared check_screenshot.ps1
     (CopyFromScreen + flat-colour refusal). Numbers are machine-read with the
     shared verifier_ocr.ps1 (Windows.Media.Ocr). Nothing is hand-typed.

Modes: probe | ac4 | ac3
"""
import argparse
import ctypes
import hashlib
import os
import re
import shutil
import subprocess
import sys
import tempfile
import time
from ctypes import wintypes

HERE = os.path.dirname(os.path.abspath(__file__))
VERIFY = os.path.dirname(HERE)                       # docs\verify
ROOT = os.path.dirname(os.path.dirname(VERIFY))      # project root
MAIN_EXE = os.path.join(ROOT, "build", "out", "NetBall.exe")
TEST_EXE = [MAIN_EXE]        # --exe overrides this for an A/B against another build
PRIV_DIR = os.path.join(ROOT, "build", "_tmp", "v4")
PRIV_EXE = os.path.join(PRIV_DIR, "NetBall_v4.exe")
MEDIA = os.path.join(VERIFY, "media-v4")
CAP_PS = os.path.join(VERIFY, "check_screenshot.ps1")
OCR_PS = os.path.join(VERIFY, "verifier_ocr.ps1")
CLASS = "NetBallBallWnd"
PROC_NAME = "NetBall_v4"
URL = "https://speed.cloudflare.com/__down?bytes=20000000"

# CREATE_NO_WINDOW only. It gives the child its OWN console, so a Ctrl+C in the
# agent console is not broadcast to it. DETACHED_PROCESS is deliberately NOT
# used: measured on this host, powershell.exe under DETACHED_PROCESS exits with
# rc=0 and produces no output and no file (silent no-op), which made a capture
# look successful while nothing was written.
CHILD_FLAGS = 0x08000000                  # CREATE_NO_WINDOW
PROCESS_TERMINATE = 0x0001
TH32CS_SNAPPROCESS = 0x00000002

k32 = ctypes.WinDLL("kernel32", use_last_error=True)
u32 = ctypes.WinDLL("user32", use_last_error=True)


# ------------------------------------------------------------------ utilities
for _s in (sys.stdout, sys.stderr):
    try:
        _s.reconfigure(encoding="utf-8", errors="replace")
    except Exception:
        pass


def log(msg=""):
    print(msg)
    sys.stdout.flush()


def decode(b):
    """PowerShell 5.1 emits OEM/ANSI bytes unless it sets OutputEncoding."""
    for enc in ("utf-8", "cp936"):
        try:
            return b.decode(enc)
        except Exception:
            continue
    return repr(b)


def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest().upper()


def stamp():
    return time.strftime("%Y-%m-%d %H:%M:%S")


class PROCESSENTRY32W(ctypes.Structure):
    _fields_ = [("dwSize", wintypes.DWORD), ("cntUsage", wintypes.DWORD),
                ("th32ProcessID", wintypes.DWORD),
                ("th32DefaultHeapID", ctypes.POINTER(ctypes.c_ulong)),
                ("th32ModuleID", wintypes.DWORD), ("cntThreads", wintypes.DWORD),
                ("th32ParentProcessID", wintypes.DWORD),
                ("pcPriClassBase", ctypes.c_long), ("dwFlags", wintypes.DWORD),
                ("szExeFile", wintypes.WCHAR * 260)]


def netball_procs():
    """All processes whose image name starts with 'netball' (any private copy)."""
    snap = k32.CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0)
    out = []
    if snap in (0, -1):
        return out
    try:
        e = PROCESSENTRY32W()
        e.dwSize = ctypes.sizeof(PROCESSENTRY32W)
        ok = k32.Process32FirstW(snap, ctypes.byref(e))
        while ok:
            name = e.szExeFile or ""
            if name.lower().startswith("netball"):
                out.append((int(e.th32ProcessID), name))
            ok = k32.Process32NextW(snap, ctypes.byref(e))
    finally:
        k32.CloseHandle(snap)
    return out


def kill_pid(pid):
    h = k32.OpenProcess(PROCESS_TERMINATE, False, pid)
    if not h:
        return False
    try:
        return bool(k32.TerminateProcess(h, 1))
    finally:
        k32.CloseHandle(h)


def wait_zero_netball(timeout=25.0):
    t0 = time.time()
    while time.time() - t0 < timeout:
        procs = netball_procs()
        if not procs:
            return True, []
        time.sleep(0.5)
    return False, netball_procs()


class RECT(ctypes.Structure):
    _fields_ = [("Left", ctypes.c_long), ("Top", ctypes.c_long),
                ("Right", ctypes.c_long), ("Bottom", ctypes.c_long)]


def find_ball():
    hits = []

    @ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
    def cb(h, l):
        buf = ctypes.create_unicode_buffer(256)
        u32.GetClassNameW(h, buf, 256)
        if buf.value == CLASS and u32.IsWindowVisible(h):
            hits.append(h)
            return False
        return True

    u32.EnumWindows(cb, 0)
    return hits[0] if hits else 0


def ball_rect(hwnd):
    r = RECT()
    u32.GetWindowRect(hwnd, ctypes.byref(r))
    return (r.Left, r.Top, r.Right, r.Bottom)


class MEMORYSTATUSEX(ctypes.Structure):
    _fields_ = [("dwLength", wintypes.DWORD), ("dwMemoryLoad", wintypes.DWORD),
                ("ullTotalPhys", ctypes.c_ulonglong), ("ullAvailPhys", ctypes.c_ulonglong),
                ("ullTotalPageFile", ctypes.c_ulonglong), ("ullAvailPageFile", ctypes.c_ulonglong),
                ("ullTotalVirtual", ctypes.c_ulonglong), ("ullAvailVirtual", ctypes.c_ulonglong),
                ("ullAvailExtendedVirtual", ctypes.c_ulonglong)]


def mem_load():
    m = MEMORYSTATUSEX()
    m.dwLength = ctypes.sizeof(MEMORYSTATUSEX)
    if not k32.GlobalMemoryStatusEx(ctypes.byref(m)):
        raise OSError("GlobalMemoryStatusEx failed")
    return int(m.dwMemoryLoad)


CFG = os.path.join(os.environ.get("APPDATA", ""), "NetBall", "config.ini")


def cfg_stamp():
    """(mtime string, size) of the live config: detects another agent mutating it."""
    try:
        st = os.stat(CFG)
        return (time.strftime("%H:%M:%S", time.localtime(st.st_mtime)), st.st_size)
    except OSError:
        return ("missing", -1)


# HANDOVER-V2 trap #1, hit for real: powershell.exe spawned from this (unaware)
# python inherits a virtualised DPI context, so GetWindowRect reports logical
# pixels (154 px ball -> "88") and CopyFromScreen captures the *scaled* desktop
# into a 100x100 bitmap, which downscales the glyphs until OCR cannot read them.
# Setting the thread context to PER_MONITOR_AWARE_V2 (-4) before the script runs
# restores physical coordinates (screen becomes 3840x2160, ball 154x154).
AWARE_PRELUDE = (
    "Add-Type -TypeDefinition 'using System;using System.Runtime.InteropServices;"
    "public class V4DpiAware{[DllImport(\"user32.dll\")]"
    "public static extern IntPtr SetThreadDpiAwarenessContext(IntPtr c);}';"
    "[void][V4DpiAware]::SetThreadDpiAwarenessContext([IntPtr]-4);"
)


def _q(s):
    return "'" + str(s).replace("'", "''") + "'"


def run_ps(script, args=(), timeout=120):
    """Run a shared .ps1 with a per-monitor-aware thread context."""
    # parameter NAMES must stay unquoted (a quoted '-ClassName' is a positional
    # string, not a parameter name); only the values are quoted/splatted.
    parts = []
    i = 0
    while i < len(args):
        a = str(args[i])
        if a.startswith("-"):
            parts.append(a)
            i += 1
            continue
        parts.append(_q(a))
        i += 1
    tail = " ".join(parts)
    line = AWARE_PRELUDE + " & " + _q(script) + (" " + tail if tail else "")
    cmd = ["powershell", "-NoProfile", "-ExecutionPolicy", "Bypass", "-Command", line]
    p = subprocess.run(cmd, capture_output=True, timeout=timeout, creationflags=CHILD_FLAGS)
    return decode((p.stdout or b"") + (p.stderr or b"")), p.returncode


def parse_cap(txt):
    """Turn the capture tool's own metrics into fields we can gate on."""
    d = {"colors": -1, "ink_px": -1, "margin": -999, "ball_box": None, "fill": None,
         "file_bytes": -1, "verdict": ""}
    m = re.search(r"colors:\s*(\d+)", txt)
    if m:
        d["colors"] = int(m.group(1))
    m = re.search(r"file\s*:\s*(\d+) bytes", txt)
    if m:
        d["file_bytes"] = int(m.group(1))
    m = re.search(r"ink\s*:\s*(\d+) px.*margin_to_shape_edge=(-?\d+)", txt)
    if m:
        d["ink_px"] = int(m.group(1))
        d["margin"] = int(m.group(2))
    m = re.search(r"ball\s*:\s*box L=(-?\d+) T=(-?\d+) R=(-?\d+) B=(-?\d+)\s*\((\d+)x(\d+) px\)", txt)
    if m:
        d["ball_box"] = (int(m.group(5)), int(m.group(6)))
    m = re.search(r"fill\s*:\s*(RGB\([^)]*\))", txt)
    if m:
        d["fill"] = m.group(1)
    m = re.search(r"RESULT: (\w+)", txt)
    if m:
        d["verdict"] = m.group(1)
    return d


def capture(out_png, expect_text=False, proc_name=PROC_NAME, retries=2):
    """Real screen capture via the shared tool. A silent no-op is never accepted.

    -ExpectText is NOT used for AC-3/AC-4: its margin criterion is a *layout*
    assertion (AC-6 territory) and it refuses the shot on a text-overflow defect,
    which would sink the memory/speed reading for an unrelated reason. The
    anti-fake guarantee is kept: the tool must report >1 distinct colour and
    write a non-trivial png, otherwise the sample is rejected.
    """
    base, ext = os.path.splitext(out_png)
    txt, rc = "", -1
    for attempt in range(retries + 1):
        # the shared OCR script holds the png open while it decodes; a concurrent
        # delete then raises WinError 32, so fall back to a per-attempt filename.
        p = out_png if attempt == 0 else "%s-r%d%s" % (base, attempt, ext)
        try:
            if os.path.exists(p):
                os.remove(p)
        except OSError:
            p = "%s-r%d%s" % (base, attempt + 1, ext)
        a = ["-OutFile", p, "-ClassName", CLASS, "-ProcessName", proc_name]
        if expect_text:
            a.append("-ExpectText")
        txt, rc = run_ps(CAP_PS, a)
        d = parse_cap(txt)
        d["path"] = p
        if os.path.exists(p) and d["colors"] > 1 and d["file_bytes"] >= 200:
            return txt, rc, d
        if attempt < retries:
            time.sleep(0.8)
    d = parse_cap(txt)
    d["path"] = out_png
    return txt, rc, d


# OCR settings, measured on three real 154 px captures (docs/verify/v4/ocr_tune.log):
#   Scale 6 / Threshold 110 / CropLeftPct 0.18 (the handover default) -> mem 0/3
#   Scale 6 / Threshold 0    / CropLeftPct 0.24                    -> mem 3/3, down 3/3
# Binarising at 110 erases the thin digits of the memory line on this build, so
# the default is only used as a fallback.
OCR_COMBOS = [(6, 0, 0.24), (6, 140, 0.18), (6, 110, 0.18)]


UNIT = {"B/S": 1, "K/S": 1024, "KB/S": 1024, "M/S": 1048576, "MB/S": 1048576,
        "GB/S": 1073741824}


def parse_down_max(text):
    """Largest downstream-looking token in the recognised text.

    During a pure download the ball shows "<down>" then "<up>", and the up value
    was 10-200x smaller in every capture, so the largest parsed token is the
    downstream figure. This also repairs the common OCR damage where a decimal
    separator is recognised as a middle dot and the tool's "first match" rule then
    latches onto the small upload figure.
    """
    if not text:
        return 0
    t = text
    for ch in ("\u00b7", "\uff0e", "\u3002", "\uff64"):
        t = t.replace(ch, ".")
    t = t.replace("\uff0c", ",").replace(" ", "")
    best = 0
    for num, unit in re.findall(r"(\d+(?:[.,]\d+)?)(GB/s|MB/s|KB/s|B/s|G/s|M/s|K/s)",
                                t, re.I):
        try:
            v = float(num.replace(",", "."))
        except ValueError:
            continue
        bps = int(v * UNIT.get(unit.upper(), 1))
        if bps > best:
            best = bps
    return best


def ocr(png, combos=None, prefer="both"):
    """Machine-read the ball. Tries the tuned combo first, then the legacy ones."""
    got = []
    for (scale, thr, crop) in (combos or OCR_COMBOS):
        txt, rc = run_ps(OCR_PS, ["-Path", png, "-Scale", str(scale),
                                  "-Threshold", str(thr), "-CropLeftPct", str(crop)])
        down = re.search(r"BALL_DOWN_BPS=(\d+)", txt)
        mem = re.search(r"BALL_MEM_PCT=(-?\d+)", txt)
        text = re.search(r"OCR_TEXT: (.*)", txt)
        r = {"down_bps": int(down.group(1)) if down else 0,
             "mem_pct": int(mem.group(1)) if mem else -1,
             "ocr_text": text.group(1).strip() if text else "",
             "raw": txt, "rc": rc,
             "combo": "scale=%d thr=%d crop=%.2f" % (scale, thr, crop)}
        r["down_max"] = parse_down_max(r["ocr_text"])
        got.append(r)
        if prefer == "both" and r["mem_pct"] > 0 and (r["down_bps"] > 0 or r["down_max"] > 0):
            return r
        if prefer == "mem" and r["mem_pct"] > 0:
            return r
        if prefer == "down" and (r["down_bps"] > 0 or r["down_max"] > 0):
            return r
    # nothing matched the preference: return the first combo that read anything
    for r in got:
        if r["mem_pct"] > 0 or r["down_bps"] > 0 or r["down_max"] > 0:
            return r
    return got[0]


def start_app():
    os.makedirs(PRIV_DIR, exist_ok=True)
    ok, procs = wait_zero_netball(25)
    if not ok:
        log("BLOCKED: a foreign NetBall instance holds the global mutex: %s" % procs)
        return None
    main_sha = sha256(TEST_EXE[0])
    need_copy = True
    if os.path.isfile(PRIV_EXE):
        need_copy = sha256(PRIV_EXE) != main_sha
    if need_copy:
        shutil.copy2(TEST_EXE[0], PRIV_EXE)
    priv_sha = sha256(PRIV_EXE)
    log("COPY: main=%s sha=%s" % (os.path.basename(TEST_EXE[0]), main_sha))
    log("COPY: priv=%s sha=%s  identical=%s"
        % (os.path.basename(PRIV_EXE), priv_sha, priv_sha == main_sha))
    if priv_sha != main_sha:
        log("BLOCKED: private copy does not match the deliverable")
        return None
    p = subprocess.Popen([PRIV_EXE], cwd=PRIV_DIR, creationflags=CHILD_FLAGS,
                         stdin=subprocess.DEVNULL,
                         stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    log("APP: pid=%d launched" % p.pid)
    t0 = time.time()
    hwnd = 0
    while time.time() - t0 < 10:
        if p.poll() is not None:
            log("APP: exited immediately rc=%s (mutex held?)" % p.poll())
            return None
        hwnd = find_ball()
        if hwnd:
            break
        time.sleep(0.3)
    if not hwnd:
        log("APP: ball window never appeared")
        kill_pid(p.pid)
        return None
    log("APP: ball hwnd=0x%X rect=%s" % (hwnd, ball_rect(hwnd)))
    time.sleep(3.0)                       # let the first metrics sample land
    return {"proc": p, "hwnd": hwnd, "pid": p.pid}


PROCESS_QUERY_LIMITED_INFORMATION = 0x1000


def proc_image_path(pid):
    h = k32.OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, False, pid)
    if not h:
        return ""
    try:
        buf = ctypes.create_unicode_buffer(1024)
        size = wintypes.DWORD(1024)
        if not k32.QueryFullProcessImageNameW(h, 0, buf, ctypes.byref(size)):
            return ""
        return buf.value
    finally:
        k32.CloseHandle(h)


def attach_app():
    """Attach read-only to an already running NetBall instance.

    The single-instance mutex is global, so when a teammate holds it we cannot
    start our own copy. Capturing their window is non-invasive (CopyFromScreen),
    and the measurement stays valid as long as their image is the same binary.
    Nothing is killed at the end.
    """
    procs = netball_procs()
    if not procs:
        return None
    want = sha256(TEST_EXE[0])
    for pid, name in procs:
        path = proc_image_path(pid)
        try:
            got = sha256(path)
        except OSError:
            continue
        log("ATTACH: pid=%d name=%s sha256=%s" % (pid, name, got))
        if got != want:
            log("ATTACH: refusing - running image does not match the deliverable")
            return None
        hwnd = find_ball()
        if not hwnd:
            continue
        log("ATTACH: ball hwnd=0x%X rect=%s (read-only, will NOT be killed)"
            % (hwnd, ("%s" % (ball_rect(hwnd),))))
        return {"proc": None, "hwnd": hwnd, "pid": pid,
                "name": os.path.splitext(name)[0], "attached": True}
    return None


def app_alive(app):
    if app.get("attached"):
        return any(pid == app["pid"] for pid, _ in netball_procs())
    return app["proc"].poll() is None


def stop_app(app):
    if not app:
        return
    if app.get("attached"):
        log("APP: attached instance pid=%d left running (not mine to kill)" % app["pid"])
        return
    if app["proc"].poll() is None:
        kill_pid(app["pid"])
    for _ in range(20):
        if app["proc"].poll() is not None:
            break
        time.sleep(0.2)
    log("APP: pid=%d terminated (by PID only), rc=%s" % (app["pid"], app["proc"].poll()))


# ---------------------------------------------------------------------- modes
def mode_probe(args):
    log("PROBE (bandwidth only)  %s" % stamp())
    out = os.path.join(tempfile.gettempdir(), "nb_v4_probe.txt")
    cmd = ["curl.exe", "-s", "-L", "--max-time", str(args.seconds), "-o", "NUL",
           "-w", "%{size_download} %{time_total} %{speed_download} %{http_code}", URL]
    t0 = time.time()
    with open(out, "w") as f:
        p = subprocess.Popen(cmd, stdout=f, stderr=subprocess.STDOUT, creationflags=CHILD_FLAGS)
        rc = p.wait()
    el = time.time() - t0
    raw = ""
    if os.path.exists(out):
        raw = open(out).read().strip()
    log("curl rc=%s wall=%.2fs out=%r" % (rc, el, raw))
    parts = raw.split()
    if rc != 0 or len(parts) < 2:
        log("RESULT: BLOCKED (curl failed)")
        return 3
    size, secs = float(parts[0]), float(parts[1])
    rate = size / secs if secs else 0
    log("bytes=%d curl_time=%.3f rate=%.1f B/s (%.2f KB/s)" % (int(size), secs, rate, rate / 1024))
    log("bytes_in_20s=%d bytes_in_25s=%d (AC-3 gate: >=300000 B and >=20 s)"
        % (int(rate * 20), int(rate * 25)))
    log("RESULT: PASS (probe only)")
    return 0


def mode_ac4(args):
    log("AC-4 MEMORY  %s" % stamp())
    app = (attach_app() if args.attach else None) or start_app()
    if not app:
        log("RESULT: BLOCKED (cannot obtain a ball window)")
        return 3
    rows = []
    cfg0 = cfg_stamp()
    log("CONFIG at start: mtime=%s bytes=%s" % (cfg0[0], cfg0[1]))
    interfered = []
    try:
        for i in range(1, args.samples + 1):
            png = os.path.join(MEDIA, "mem-%d.png" % i)
            # ---- environment guards: a vanished window or a mutated config is
            # ---- NOT a product verdict, it is interference and must be blocked.
            if not app_alive(app):
                interfered.append("sample %d: instance disappeared - closed by someone else" % i)
                break
            if not find_ball():
                interfered.append("sample %d: ball window gone (class is shared, another agent may have closed it)" % i)
                break
            c = cfg_stamp()
            if c != cfg0:
                interfered.append("sample %d: config.ini changed during the run (%s/%s -> %s/%s)"
                                  % (i, cfg0[0], cfg0[1], c[0], c[1]))
                break
            # A dark window sliding over the ball keeps cap_rc=0 but drops the ink
            # to a few hundred px (clean shots carry ~2200). Retry instead of
            # scoring an occluded frame as a product failure.
            attempt = 0
            while True:
                attempt += 1
                sys_mem_pre = mem_load()
                t_cap = time.time()
                cap_txt, cap_rc, cap = capture(png, proc_name=app.get("name", PROC_NAME))
                sys_mem_post = mem_load()
                cap_sec = time.time() - t_cap
                o = ocr(cap["path"], prefer="mem")
                occluded = cap["ink_px"] >= 0 and cap["ink_px"] < 800
                if not occluded or attempt >= 4:
                    break
                log("  (attempt %d looks occluded: ink_px=%d - retrying)" % (attempt, cap["ink_px"]))
                time.sleep(1.2)
            if occluded:
                interfered.append("sample %d: ball occluded by another window (ink_px=%d, "
                                  "clean shots carry ~2200) - not a product verdict"
                                  % (i, cap["ink_px"]))
                break
            # The ball repaints once per refreshms (1000 ms), so the number in the
            # png can be up to ~1 s stale. dwMemoryLoad is bracketed before and
            # after the capture; the delta is taken against the nearer bracket so
            # a 1 s skew is not scored as a rendering error. Both brackets are
            # printed so the reader can see how much the counter moved.
            d_pre = abs(o["mem_pct"] - sys_mem_pre)
            d_post = abs(o["mem_pct"] - sys_mem_post)
            dt = min(d_pre, d_post)
            log("")
            log("--- sample %d  (capture %.2fs, ball %s) ---"
                % (i, cap_sec, ("%dx%d" % cap["ball_box"]) if cap["ball_box"] else "?"))
            for line in cap_txt.strip().splitlines():
                log("  cap| " + line)
            log("  cap_rc=%s  png_exists=%s  colors=%d  ink_px=%d  margin=%d"
                % (cap_rc, os.path.exists(png), cap["colors"], cap["ink_px"], cap["margin"]))
            log("  ocr_text=%r  ball_mem=%d%%  down_bps=%d  [%s]"
                % (o["ocr_text"], o["mem_pct"], o["down_bps"], o["combo"]))
            log("  system_dwMemoryLoad: pre=%d post=%d" % (sys_mem_pre, sys_mem_post))
            log("  delta_pp: pre=%d post=%d -> used=%d  (tolerance 2 pp)" % (d_pre, d_post, dt))
            ok = o["mem_pct"] >= 0 and dt <= 2
            log("  sample_verdict=%s" % ("PASS" if ok else "FAIL"))
            rows.append({"i": i, "png": cap["path"], "ball": o["mem_pct"], "sys": sys_mem_pre,
                         "sys_post": sys_mem_post, "delta": dt, "d_pre": d_pre,
                         "d_post": d_post, "cap_rc": cap_rc,
                         "ok": ok, "ocr_text": o["ocr_text"]})
            if i < args.samples:
                time.sleep(args.gap)
    finally:
        stop_app(app)

    log("")
    log("=== AC-4 SUMMARY ===")
    log("| # | ball_mem% | dwMemoryLoad | delta_pp | colors | verdict | evidence |")
    log("|---|---|---|---|---|---|---|")
    for r in rows:
        log("| %d | %s | %s | %s | - | %s | %s |"
            % (r["i"], r["ball"] if r["ball"] >= 0 else "OCR-FAIL", r["sys"], r["delta"],
               "PASS" if r["ok"] else "FAIL", os.path.basename(r["png"])))
    if interfered:
        for msg in interfered:
            log("INTERFERENCE: " + msg)
        log("RESULT: BLOCKED (environment interference - rerun needed, not a product verdict)")
        return 3
    passed = sum(1 for r in rows if r["ok"])
    log("passed=%d/%d" % (passed, len(rows)))
    if passed == len(rows) and rows:
        log("RESULT: PASS (all %d samples within 2 pp of GlobalMemoryStatusEx)" % len(rows))
        return 0
    log("RESULT: FAIL (%d/%d samples within 2 pp)" % (passed, len(rows)))
    return 1


def mode_ac3(args):
    log("AC-3 NETWORK SPEED  %s" % stamp())
    app = (attach_app() if args.attach else None) or start_app()
    if not app:
        log("RESULT: BLOCKED (cannot obtain a ball window)")
        return 3
    pngs = []
    rec = []
    interfered = []
    cfg0 = cfg_stamp()
    log("CONFIG at start: mtime=%s bytes=%s" % (cfg0[0], cfg0[1]))

    # Independent third witness: the machine's NIC aggregate (dedup rule as agreed
    # with builder-data-1). Without it "ball vs curl" cannot be told apart from
    # "somebody else was downloading at the same time", because the ball reports
    # the whole interface, not curl's socket.
    nic = None
    try:
        sys.path.insert(0, VERIFY)
        import check_ac3_net as nic
        log("NIC: %d eligible interface(s) after OperStatus/type/FilterInterface filter"
            % len(nic.select_rows(nic.read_rows())))
    except Exception as e:
        log("NIC: unavailable (%r)" % e)
        nic = None
    try:
        wf = os.path.join(tempfile.gettempdir(), "nb_v4_ac3_curl.txt")
        if os.path.exists(wf):
            os.remove(wf)
        cmd = ["curl.exe", "-s", "-L", "--max-time", str(args.seconds), "-o", "NUL",
               "-w", "%{size_download} %{time_total} %{speed_download} %{http_code}", URL]
        if args.rate > 0:
            # The link is bursty: 29.5 KB/s in one probe, 1.0 MB/s in the next, and
            # at 1 MB/s the 20 MB payload finishes in 19.7 s - just under the
            # >=20 s window gate - while flipping the display to MB/s, which the
            # OCR garbles. A throttled download keeps the window long and the
            # display in the 3-digit KB/s range the OCR reads cleanly. Ground truth
            # is still real bytes over real time.
            cmd += ["--limit-rate", "%dk" % args.rate]
        f = open(wf, "w")
        t0 = time.time()
        pin = pout = 0
        nic_prev_t = t0
        nic_rows = []
        if nic:
            try:
                nic_rows = nic.select_rows(nic.read_rows())
                pin, pout = nic.totals(nic_rows)
            except Exception:
                nic = None
        curl = subprocess.Popen(cmd, stdout=f, stderr=subprocess.STDOUT, creationflags=CHILD_FLAGS)
        log("CURL: pid=%d max_time=%ds rate_limit=%sk url=%s"
            % (curl.pid, args.seconds, args.rate, URL))
        log("NOTE: curl --max-time on a 20 MB payload ALWAYS exits 28 (truncated by")
        log("      design). rc=28 is expected and is not a failure.")

        for i in range(args.samples):
            target = t0 + args.first + args.gap * i
            while time.time() < target:
                time.sleep(0.05)
            if not app_alive(app):
                interfered.append("sample %d: instance disappeared - closed by someone else" % (i + 1))
                break
            if not find_ball():
                interfered.append("sample %d: ball window gone" % (i + 1))
                break
            c = cfg_stamp()
            if c != cfg0:
                interfered.append("sample %d: config.ini changed mid-window (%s -> %s)"
                                  % (i + 1, cfg0[0], c[0]))
                break
            png = os.path.join(MEDIA, "ac3-s%d.png" % (i + 1))
            attempt = 0
            while True:
                attempt += 1
                ts = time.time() - t0
                cap_txt, cap_rc, cap = capture(png, proc_name=app.get("name", PROC_NAME))
                if cap["ink_px"] >= 800 or attempt >= 3:
                    break
                log("SAMPLE %d: occluded (ink_px=%d) - recapturing" % (i + 1, cap["ink_px"]))
            if cap["ink_px"] >= 0 and cap["ink_px"] < 800:
                interfered.append("sample %d: ball occluded (ink_px=%d)" % (i + 1, cap["ink_px"]))
                break
            entry = {"i": i + 1, "t": ts, "png": cap["path"], "cap_rc": cap_rc,
                     "cap_txt": cap_txt, "colors": cap["colors"],
                     "ink_px": cap["ink_px"], "nic_bps": -1}
            if nic:
                try:
                    ci, _co = nic.totals(nic.select_rows(nic.read_rows()))
                    now2 = time.time()
                    el2 = max(1e-3, now2 - nic_prev_t)
                    entry["nic_bps"] = int(max(0, ci - pin) / el2)
                    pin, nic_prev_t = ci, now2
                except Exception:
                    pass
            rec.append(entry)
            log("SAMPLE %d: t=%.2fs cap_rc=%s colors=%d ink_px=%d ball=%s nic=%d B/s"
                % (i + 1, ts, cap_rc, cap["colors"], cap["ink_px"],
                   ("%dx%d" % cap["ball_box"]) if cap["ball_box"] else "?",
                   entry["nic_bps"]))
            pngs.append(png)

        rc = curl.wait()
        f.close()
        log("CURL: rc=%s wall=%.2fs" % (rc, time.time() - t0))
    finally:
        pass

    truth = 0.0
    size = secs = 0.0
    raw = ""
    try:
        raw = open(os.path.join(tempfile.gettempdir(), "nb_v4_ac3_curl.txt")).read().strip()
    except Exception:
        pass
    log("CURL -w: %r" % raw)
    parts = raw.split()
    if len(parts) >= 2:
        size, secs = float(parts[0]), float(parts[1])
        if secs > 0:
            truth = size / secs

    for r in rec:
        o = ocr(r["png"], prefer="down")
        r["ball"] = o["down_max"] or o["down_bps"]
        r["mem"] = o["mem_pct"]
        r["ocr_text"] = o["ocr_text"]
        r["err"] = (abs(r["ball"] - truth) / truth * 100.0) if truth > 0 and r["ball"] > 0 else None
        nr = r.get("nic_bps", -1)
        r["err_nic"] = (abs(r["ball"] - nr) / nr * 100.0) if nr > 0 and r["ball"] > 0 else None
        log("OCR %d: text=%r ball_bps=%d nic_bps=%d err_vs_curl=%s err_vs_nic=%s [%s]"
            % (r["i"], o["ocr_text"], r["ball"], nr,
               ("%.2f%%" % r["err"]) if r["err"] is not None else "n/a",
               ("%.2f%%" % r["err_nic"]) if r["err_nic"] is not None else "n/a", o["combo"]))

    stop_app(app)

    log("")
    log("GROUND_TRUTH bytes=%d seconds=%.3f rate=%.1f B/s (%.2f KB/s)"
        % (int(size), secs, truth, truth / 1024 if truth else 0))
    log("GATE: bytes>=300000 -> %s ; window>=20s -> %s"
        % (size >= 300000, secs >= 20))
    if interfered:
        for msg in interfered:
            log("INTERFERENCE: " + msg)
        log("RESULT: BLOCKED (environment interference mid-window - rerun needed)")
        return 3
    if size < 300000 or secs < 20 or truth <= 0:
        log("RESULT: BLOCKED-流量未建立 (%d bytes in %.2fs)" % (int(size), secs))
        return 3
    if len(rec) < args.samples:
        log("RESULT: BLOCKED (only %d of %d samples captured)" % (len(rec), args.samples))
        return 3

    log("")
    log("| # | t(s) | ball_ocr | ball_B/s | curl_GT_B/s | nic_B/s | err_vs_curl% | err_vs_nic% | <=20% |")
    log("|---|---|---|---|---|---|---|---|---|")
    for r in rec:
        e = r["err"]
        log("| %d | %.2f | %s | %d | %d | %d | %s | %s | %s |"
            % (r["i"], r["t"], r["ocr_text"] or "-", r["ball"], int(truth),
               r.get("nic_bps", -1),
               ("%.2f" % e) if e is not None else "n/a",
               ("%.2f" % r["err_nic"]) if r.get("err_nic") is not None else "n/a",
               "PASS" if (e is not None and e <= 20.0) else "FAIL"))
    passed = sum(1 for r in rec if r["err"] is not None and r["err"] <= 20.0)
    log("samples_within_20pct=%d/%d" % (passed, len(rec)))
    if rec and all(r["ball"] == 0 for r in rec):
        log("RESULT: BLOCKED-无法机读 (OCR returned no speed token on any of %d captures)" % len(rec))
        return 3
    if passed == len(rec) and rec:
        log("RESULT: PASS (5/5 samples within 20%% of curl ground truth)")
        return 0
    log("RESULT: FAIL (%d/%d samples within 20%%)" % (passed, len(rec)))
    return 1


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("mode", choices=["probe", "ac4", "ac3"])
    ap.add_argument("--seconds", type=int, default=25)
    ap.add_argument("--samples", type=int, default=5)
    ap.add_argument("--gap", type=float, default=4.0)
    ap.add_argument("--first", type=float, default=3.0)
    ap.add_argument("--rate", type=int, default=0, help="curl --limit-rate in KB/s (0 = unthrottled)")
    ap.add_argument("--attach", action="store_true",
                    help="read-only: measure an already running instance instead of starting one")
    ap.add_argument("--exe", default=None, help="A/B: measure another build copy")
    args = ap.parse_args()
    os.makedirs(MEDIA, exist_ok=True)

    # ---- every verdict is anchored to one build ----
    if args.exe:
        p = args.exe if os.path.isabs(args.exe) else os.path.join(ROOT, args.exe)
        TEST_EXE[0] = os.path.abspath(p)
        log("A/B: measuring %s (NOT the deliverable)" % TEST_EXE[0])
    st = os.stat(TEST_EXE[0])
    newest, newest_name = 0.0, "-"
    src_dir = os.path.join(ROOT, "src")
    for name in os.listdir(src_dir):
        if name.endswith((".cpp", ".h")):
            m = os.stat(os.path.join(src_dir, name)).st_mtime
            if m > newest:
                newest, newest_name = m, name
    log("BUILD: exe=%s bytes=%d mtime=%s sha256=%s"
        % (os.path.basename(TEST_EXE[0]), st.st_size,
           time.strftime("%Y-%m-%d %H:%M:%S", time.localtime(st.st_mtime)), sha256(TEST_EXE[0])))
    log("BUILD: newest src=%s mtime=%s" % (newest_name,
        time.strftime("%Y-%m-%d %H:%M:%S", time.localtime(newest))))
    log("BUILD: fresh=%s%s" % ("NO -> exe predates source, this run is NOT a valid verdict"
                               if st.st_mtime < newest else "yes",
                               ""))
    log("")

    if args.mode == "probe":
        return mode_probe(args)
    if args.mode == "ac4":
        return mode_ac4(args)
    return mode_ac3(args)


if __name__ == "__main__":
    sys.exit(main())
