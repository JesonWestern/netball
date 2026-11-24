#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
AC-9 config persistence check (%APPDATA%\\NetBall\\config.ini).

Modes
  (default)            assert all required keys exist, print the fetched values
  --snapshot FILE      write the current config as JSON
  --diff FILE          compare the current config against a snapshot
                       (used for the "change setting -> restart -> compare" flow)

Required keys: shape size opacity bgcolor textauto textcolor shownet showmem
               refreshms alwaysontop posx posy

Exit codes: 0 PASS / 1 FAIL / 2 SKIP
"""

import argparse
import ctypes
import json
import os
import sys


def _force_utf8():
    """Keep non-ASCII paths readable when PowerShell captures the pipe (cp936)."""
    for stream in (sys.stdout, sys.stderr):
        try:
            stream.reconfigure(encoding="utf-8", errors="replace")
        except Exception:
            pass


_force_utf8()

REQUIRED = ["shape", "size", "opacity", "bgcolor", "textauto", "textcolor",
            "shownet", "showmem", "refreshms", "alwaysontop", "posx", "posy"]

# loose validation: key present *and* parseable; a wrong value type is a real bug
VALIDATORS = {
    "shape": lambda v: v in ("circle", "square", "rounded", "ellipse", "rect") or v.isdigit(),
    "size": lambda v: _num(v) is not None,
    "opacity": lambda v: _num(v) is not None,
    "bgcolor": lambda v: v.startswith("#") or v.startswith("0x") or "," in v or _num(v) is not None,
    "textauto": lambda v: _bool(v) is not None,
    "textcolor": lambda v: True,   # may legitimately be empty when textauto=1
    "shownet": lambda v: _bool(v) is not None,
    "showmem": lambda v: _bool(v) is not None,
    "refreshms": lambda v: _num(v) is not None,
    "alwaysontop": lambda v: _bool(v) is not None,
    "posx": lambda v: _num(v) is not None,
    "posy": lambda v: _num(v) is not None,
}
OPTIONAL_OK = {"textcolor"}


def _num(v):
    try:
        float(v)
        return float(v)
    except Exception:
        return None


def _bool(v):
    v = v.strip().lower()
    if v in ("1", "true", "yes", "on"):
        return True
    if v in ("0", "false", "no", "off"):
        return False
    return None


def default_path():
    appdata = os.environ.get("APPDATA") or os.path.expanduser("~")
    return os.path.join(appdata, "NetBall", "config.ini")


def parse_ini(path):
    """Tolerant INI reader: handles flat key=value and [section] layouts."""
    data = {}
    sections = []
    cur = None
    with open(path, "r", encoding="utf-8-sig", errors="replace") as f:
        for raw in f:
            line = raw.strip()
            if not line or line[0] in ";#":
                continue
            if line.startswith("[") and line.endswith("]"):
                cur = line[1:-1].strip()
                sections.append(cur)
                continue
            if "=" in line:
                k, v = line.split("=", 1)
                k = k.strip().lower()
                v = v.strip()
                if cur:
                    data["%s.%s" % (cur.lower(), k)] = v
                data.setdefault(k, v)  # flat name wins for lookups
            elif ":" in line:
                k, v = line.split(":", 1)
                data.setdefault(k.strip().lower(), v.strip())
    return data, sections


def load(path):
    if not os.path.isfile(path):
        return None, None
    try:
        return parse_ini(path)
    except Exception as e:
        print("  read error: %s" % e)
        return None, None


def cmd_snapshot(path, out):
    data, sections = load(path)
    if data is None:
        print("AC-9 CONFIG SNAPSHOT")
        print("  path : %s" % path)
        print("RESULT: SKIP (config.ini not found - run the app once and change a setting)")
        return 2
    payload = {"path": path, "sections": sections, "values": data}
    with open(out, "w", encoding="utf-8") as f:
        json.dump(payload, f, ensure_ascii=False, indent=2)
    print("AC-9 CONFIG SNAPSHOT")
    print("  path     : %s" % path)
    print("  snapshot : %s (%d keys)" % (out, len(data)))
    for k in sorted(data):
        print("    %-24s = %s" % (k, data[k]))
    print("RESULT: PASS (snapshot written)")
    return 0


def cmd_diff(path, snap):
    print("AC-9 CONFIG DIFF (change setting -> restart -> compare)")
    if not os.path.isfile(snap):
        print("  snapshot missing: %s" % snap)
        print("RESULT: FAIL (take a snapshot first with --snapshot)")
        return 1
    data, _ = load(path)
    if data is None:
        print("  config missing: %s" % path)
        print("RESULT: FAIL (config.ini disappeared after restart)")
        return 1
    with open(snap, "r", encoding="utf-8") as f:
        old = json.load(f)["values"]

    print("  current : %s" % path)
    print("  snapshot: %s" % snap)
    print("")
    print("  %-24s %-24s %-24s %s" % ("key", "snapshot", "current", "changed"))
    print("  " + "-" * 78)
    changed = []
    lost = []
    for k in sorted(set(old) | set(data)):
        a = old.get(k, "<absent>")
        b = data.get(k, "<absent>")
        flag = ""
        if a != b:
            flag = "CHANGED"
            changed.append(k)
            if b == "<absent>":
                lost.append(k)
        print("  %-24s %-24s %-24s %s" % (k, a, b, flag))
    print("")
    if lost:
        print("RESULT: FAIL (keys lost after restart: %s)" % ", ".join(lost))
        return 1
    if not changed:
        print("RESULT: FAIL (nothing changed - the edited setting was not persisted)")
        return 1
    print("METRIC: changed_keys=%d (%s)" % (len(changed), ",".join(changed)))
    print("RESULT: PASS (edited setting survived the restart)")
    return 0


def cmd_check(path):
    print("AC-9 CONFIG CHECK (all required keys present)")
    print("  path : %s" % path)
    data, sections = load(path)
    if data is None:
        print("  NOT FOUND")
        here = os.path.dirname(os.path.abspath(__file__))
        root = os.path.dirname(os.path.dirname(here))
        exe = os.path.join(root, "build", "out", "NetBall.exe")
        if not os.path.isfile(exe):
            print("RESULT: SKIP (app not built yet, so config.ini was never created)")
            return 2
        print("RESULT: FAIL (config.ini missing although the app exists - settings were never persisted)")
        return 1
    print("  sections : %s" % (sections if sections else "<flat key=value>"))
    print("")
    print("  %-14s %-24s %-9s %s" % ("key", "value", "present", "valid"))
    print("  " + "-" * 62)
    missing = []
    invalid = []
    for k in REQUIRED:
        v = data.get(k)
        present = v is not None
        if not present:
            missing.append(k)
            print("  %-14s %-24s %-9s %s" % (k, "<MISSING>", "NO", "-"))
            continue
        ok = VALIDATORS[k](v) if k in VALIDATORS else True
        if not ok and k not in OPTIONAL_OK:
            invalid.append(k)
        print("  %-14s %-24s %-9s %s" % (k, v, "YES", "OK" if ok else "SUSPECT"))

    extra = [k for k in data if k not in REQUIRED and "." not in k]
    if extra:
        print("")
        print("  extra keys (informational): %s" % ", ".join(sorted(extra)))

    print("")
    if missing:
        print("RESULT: FAIL (missing keys: %s)" % ", ".join(missing))
        return 1
    if invalid:
        print("RESULT: FAIL (unparseable values: %s)" % ", ".join(invalid))
        return 1
    print("METRIC: keys=%d required=%d" % (len(data), len(REQUIRED)))
    print("RESULT: PASS (all %d required keys present and parseable)" % len(REQUIRED))
    return 0


def _win32():
    import ctypes.wintypes as wt
    u = ctypes.WinDLL("user32", use_last_error=True)
    u.EnumWindows.argtypes = [ctypes.WINFUNCTYPE(wt.BOOL, wt.HWND, wt.LPARAM), wt.LPARAM]
    u.GetClassNameW.argtypes = [wt.HWND, wt.LPWSTR, ctypes.c_int]
    u.GetWindowThreadProcessId.argtypes = [wt.HWND, ctypes.POINTER(wt.DWORD)]
    u.GetWindowRect.argtypes = [wt.HWND, ctypes.POINTER(wt.RECT)]
    u.IsWindowVisible.argtypes = [wt.HWND]
    u.GetSystemMetrics.argtypes = [ctypes.c_int]
    return u, wt


def _windows_of_pid(pid):
    """Return [(hwnd, class, rect, visible)] for top-level windows of pid."""
    u, wt = _win32()
    out = []
    CB = ctypes.WINFUNCTYPE(wt.BOOL, wt.HWND, wt.LPARAM)

    def cb(h, l):
        p = wt.DWORD(0)
        u.GetWindowThreadProcessId(h, ctypes.byref(p))
        if p.value == pid:
            buf = ctypes.create_unicode_buffer(256)
            u.GetClassNameW(h, buf, 256)
            r = wt.RECT()
            u.GetWindowRect(h, ctypes.byref(r))
            out.append((int(h), buf.value, (r.left, r.top, r.right, r.bottom),
                        bool(u.IsWindowVisible(h))))
        return True

    u.EnumWindows(CB(cb), 0)
    return out


def cmd_corrupt(path, exe):
    """AC-9 robustness: hand-corrupted config must fall back to defaults, not crash."""
    import shutil
    import subprocess
    import time
    import tempfile

    print("AC-9 CONFIG CORRUPTION ROBUSTNESS")
    print("  path : %s" % path)
    print("  exe  : %s" % exe)

    if not os.path.isfile(exe):
        print("RESULT: SKIP (app not built yet: %s)" % exe)
        return 2
    if not os.path.isfile(path):
        print("RESULT: SKIP (config.ini missing - run the app once first)")
        return 2

    # a leftover instance wins the single-instance race and makes the new process
    # exit 0 at once - which would look like "config corruption kills startup".
    deadline = time.time() + 8.0
    while time.time() < deadline:
        stale = []
        try:
            out = subprocess.run(["tasklist", "/FI", "IMAGENAME eq NetBall.exe", "/FO", "CSV", "/NH"],
                                 capture_output=True, text=True, timeout=15).stdout
            for line in out.splitlines():
                if line.lower().startswith('"netball.exe"'):
                    try:
                        stale.append(int(line.split('","')[1]))
                    except (IndexError, ValueError):
                        pass
        except Exception:
            pass
        if not stale:
            break
        print("  pre-existing instance(s) %s -> killing for a clean test" % stale)
        for pid in stale:
            subprocess.run(["taskkill", "/PID", str(pid), "/F"], capture_output=True)
        time.sleep(0.7)
    else:
        print("  WARNING: NetBall.exe still alive after 8s; the launch test would be invalid")
        print("RESULT: BLOCKED (cannot get a clean single-instance state)")
        return 3

    backup = os.path.join(tempfile.gettempdir(), "nb_config_backup.ini")
    shutil.copy2(path, backup)
    print("  backup: %s" % backup)

    # corrupt: out-of-range opacity, non-numeric refreshms, absurd position, missing key
    raw = []
    seen = set()
    with open(path, "r", encoding="utf-8-sig", errors="replace") as f:
        for line in f.read().splitlines():
            low = line.strip().lower()
            if low.startswith("opacity="):
                line = "opacity=999"
            elif low.startswith("refreshms="):
                line = "refreshms=abc"
            elif low.startswith("posx="):
                line = "posx=999999"
            elif low.startswith("posy="):
                line = "posy=999999"
            elif low.startswith("textauto="):
                seen.add("textauto")
                continue          # delete the key entirely
            raw.append(line)
    with open(path, "w", encoding="utf-8") as f:
        f.write("\n".join(raw) + "\n")
    print("  corrupted keys : opacity=999, refreshms=abc, posx=999999, posy=999999, textauto=<deleted>")

    proc = None
    fails = []
    alive = False
    dialogs = []
    balls = []
    # make THIS process per-monitor-v2 aware, otherwise GetSystemMetrics is DPI
    # virtualised and cannot be compared with the physical coords of GetWindowRect
    try:
        ctypes.WinDLL("user32", use_last_error=True).SetProcessDpiAwarenessContext(ctypes.c_void_p(-4))
    except Exception:
        pass
    try:
        proc = subprocess.Popen([exe])
        time.sleep(4.0)
        alive = proc.poll() is None
        rc = proc.poll()
        wins = _windows_of_pid(proc.pid) if alive else []
        dialogs = [w for w in wins if w[1] == "#32770" and w[3]]
        balls = [w for w in wins if w[1] == "NetBallBallWnd"]
        u, wt = _win32()
        vx = u.GetSystemMetrics(76)   # SM_XVIRTUALSCREEN
        vy = u.GetSystemMetrics(77)
        vw = u.GetSystemMetrics(78)
        vh = u.GetSystemMetrics(79)

        print("  process alive  : %s  (exit code %s)" % (alive, rc if rc is not None else "still running"))
        print("  virtual screen : L=%d T=%d W=%d H=%d" % (vx, vy, vw, vh))
        print("  dialogs(#32770): %d" % len(dialogs))
        for w in wins:
            print("    win class=%-18s rect=%s visible=%s" % (w[1], w[2], w[3]))

        if not alive:
            fails.append("process died while loading a corrupted config (crash)")
        if dialogs:
            fails.append("%d modal dialog(s) popped up (must not show an error box)" % len(dialogs))
        if alive and not balls:
            fails.append("no ball window created -> config fallback broke startup")
        for w in balls:
            l, t, r, b = w[2]
            on_screen = (r > vx and b > vy and l < vx + vw and t < vy + vh)
            print("  ball rect      : L=%d T=%d R=%d B=%d visible=%s on_screen=%s"
                  % (l, t, r, b, w[3], on_screen))
            if not on_screen:
                fails.append("ball placed off-screen (L=%d T=%d): posx/posy fallback failed" % (l, t))
            if (r - l) < 16 or (b - t) < 16:
                fails.append("ball window size %dx%d too small" % (r - l, b - t))
            if not w[3]:
                fails.append("ball window exists but is not visible")
        try:
            with open(path, "r", encoding="utf-8-sig", errors="replace") as f:
                after = f.read()
            print("  ini after run  : %s"
                  % ("rewritten by app" if "opacity=999" not in after else "unchanged (clamped in memory)"))
        except Exception:
            pass
    finally:
        try:
            if proc is not None and proc.poll() is None:
                proc.kill()
                proc.wait(timeout=5)
        except Exception:
            pass
        time.sleep(0.3)
        shutil.copy2(backup, path)
        print("  config restored: %s" % path)

    if fails:
        for f_ in fails:
            print("  VIOLATION: %s" % f_)
        print("METRIC: alive=%d dialogs=%d ball_windows=%d violations=%d"
              % (1 if alive else 0, len(dialogs), len(balls), len(fails)))
        print("RESULT: FAIL (corrupted config not handled gracefully)")
        return 1
    print("METRIC: alive=1 dialogs=0 ball_windows=%d" % len(balls))
    print("RESULT: PASS (corrupted config: no crash, no dialog, window still on-screen)")
    return 0


RUN_KEY = r"Software\Microsoft\Windows\CurrentVersion\Run"
RUN_VALUE = "NetBall"


def _read_run_value():
    """Return the raw HKCU Run\\NetBall string, or None. Never raises."""
    try:
        import winreg
        with winreg.OpenKey(winreg.HKEY_CURRENT_USER, RUN_KEY, 0, winreg.KEY_QUERY_VALUE) as k:
            val, typ = winreg.QueryValueEx(k, RUN_VALUE)
            return val
    except FileNotFoundError:
        return None
    except OSError:
        return None


def _autostart_password_ok(raw, exe):
    """The stored data must be a quoted, existing NetBall.exe path."""
    if raw is None:
        return True, "value absent (autostart off)"
    s = raw.strip()
    quoted = s.startswith('"') and s.endswith('"') and len(s) > 2
    path = s[1:-1] if quoted else s
    if not quoted:
        return False, "value is not quoted (%r) - Run values with spaces need quotes" % s
    if not path.lower().endswith("netball.exe"):
        return False, "value points to %r, not NetBall.exe" % path
    if not os.path.isfile(path):
        return False, "value points to a missing file: %r" % path
    return True, "value=%r (file exists)" % path


def cmd_autostart(path):
    """AC-9 autostart: the registry is the source of truth, the ini key is only a snapshot."""
    print("AC-9 AUTOSTART CHECK (registry is authoritative)")
    print("  key   : HKCU\\%s\\%s" % (RUN_KEY, RUN_VALUE))
    here = os.path.dirname(os.path.abspath(__file__))
    root = os.path.dirname(os.path.dirname(here))
    exe = os.path.join(root, "build", "out", "NetBall.exe")
    raw = _read_run_value()
    print("  value : %s" % ("<absent>" if raw is None else repr(raw)))
    data, _ = load(path)
    ini_val = (data or {}).get("autostart")
    print("  ini   : autostart=%s (snapshot only - cfg::load() overwrites it from the registry)"
          % ("<absent>" if ini_val is None else ini_val))

    ok, why = _autostart_password_ok(raw, exe)
    print("  check : %s" % why)
    if raw is not None and ini_val is not None and ini_val.strip() == "0":
        print("  NOTE  : ini says autostart=0 while the registry has a value; the app will")
        print("          follow the registry. This is expected, not a defect.")
    p = os.path.abspath(os.path.join(root, "build", "out", "NetBall.exe"))
    if raw is not None and p.lower() not in raw.lower().replace("/", "\\"):
        print("  NOTE  : registry path differs from the current artifact (%s); acceptable if")
        print("          autostart was enabled from another build, but re-check after a move." % p)
    if ok:
        print("METRIC: run_value_present=%d" % (0 if raw is None else 1))
        print("RESULT: PASS (registry state is self-consistent)")
        return 0
    print("  VIOLATION: %s" % why)
    print("METRIC: run_value_present=1")
    print("RESULT: FAIL (registry autostart value is invalid)")
    return 1


def main():
    ap = argparse.ArgumentParser(description="AC-9 config.ini verification")
    ap.add_argument("--path", default=None, help="override config.ini path")
    ap.add_argument("--snapshot", metavar="FILE", default=None)
    ap.add_argument("--diff", metavar="FILE", default=None)
    ap.add_argument("--corrupt", action="store_true",
                    help="corrupt config.ini, launch the app, assert graceful fallback")
    ap.add_argument("--autostart", action="store_true",
                    help="verify HKCU Run\\NetBall instead of trusting the ini snapshot")
    ap.add_argument("--exe", default=None, help="artifact path used by --corrupt")
    args = ap.parse_args()

    try:
        import nb_build_info
        nb_build_info.build_info()
    except Exception:
        pass

    path = args.path or default_path()
    here = os.path.dirname(os.path.abspath(__file__))
    root = os.path.dirname(os.path.dirname(here))
    exe = args.exe or os.path.join(root, "build", "out", "NetBall.exe")
    if args.snapshot:
        return cmd_snapshot(path, args.snapshot)
    if args.diff:
        return cmd_diff(path, args.diff)
    if args.corrupt:
        return cmd_corrupt(path, os.path.abspath(exe))
    if args.autostart:
        return cmd_autostart(path)
    return cmd_check(path)


if __name__ == "__main__":
    sys.exit(main())
