#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Shared preflight: which binary am I measuring, and is it newer than the sources?

Every verdict must be traceable to a build. A stale exe once made a fixed defect
look live (and vice versa), so AC-3 refuses to run when the exe predates its source.
"""
import os
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
DEFAULT_EXE = os.path.join(ROOT, "build", "out", "NetBall.exe")
SOURCE_WATCH = [
    os.path.join(ROOT, "src", "metrics.cpp"),
    os.path.join(ROOT, "src", "config.cpp"),
    os.path.join(ROOT, "src", "ball_window.cpp"),
    os.path.join(ROOT, "src", "render.cpp"),
    os.path.join(ROOT, "src", "main.cpp"),
]


def _fmt(ts):
    return time.strftime("%Y-%m-%d %H:%M:%S", time.localtime(ts))


def build_info(exe=None, quiet=False):
    """Print one BUILD: line and return a dict. Never raises."""
    info = {"exe": exe or DEFAULT_EXE, "exe_mtime": None, "exe_size": None,
            "newest_src": None, "newest_src_path": None, "stale": None}
    try:
        st = os.stat(info["exe"])
        info["exe_mtime"] = st.st_mtime
        info["exe_size"] = st.st_size
    except OSError:
        if not quiet:
            print("BUILD: exe=%s MISSING" % info["exe"])
        return info

    newest = 0.0
    newest_path = None
    for p in SOURCE_WATCH:
        try:
            m = os.stat(p).st_mtime
        except OSError:
            continue
        if m > newest:
            newest, newest_path = m, p

    info["newest_src"] = newest or None
    info["newest_src_path"] = newest_path
    if newest > 0:
        info["stale"] = info["exe_mtime"] < newest

    if not quiet:
        print("BUILD: exe=%s bytes=%s mtime=%s"
              % (os.path.basename(info["exe"]), info["exe_size"], _fmt(info["exe_mtime"])))
        if newest_path:
            print("BUILD: newest source=%s mtime=%s"
                  % (os.path.relpath(newest_path, ROOT).replace("\\", "/"), _fmt(newest)))
            print("BUILD: fresh=%s%s"
                  % ("NO -> exe predates the source, rebuild first" if info["stale"] else "yes",
                     "" if not info["stale"] else " (BLOCKED condition)"))
    return info
