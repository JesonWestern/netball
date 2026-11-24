#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
AC-3 independent network-speed ground truth (does not use the product's code).

Reads MIB_IF_ROW2 via iphlpapi!GetIfTable2 (x64-correct layout, sizeof == 1352),
excludes software loopback (IFTYPE 24) and tunnel (IFTYPE 131), keeps only
OperStatus == 1 (IfOperStatusUp), and prints the aggregate download rate in B/s.

Modes
  (default)            live monitor: one line per second with down/up B/s
  --seconds N          monitor duration (default 10)
  --compare            generate real traffic via curl and print
                       GROUND_TRUTH_DOWN_BPS=<value> for manual comparison
                       with the number shown on the floating ball
  --measured BPS       used with --compare: compare a hand-read value and
                       assert relative error <= --tol (default 20 %)
  --json               dump the matched interface rows and exit

Exit codes: 0 PASS / 1 FAIL / 2 SKIP
"""

import argparse
import ctypes
import ctypes.wintypes as wt
import json
import os
import subprocess
import sys
import tempfile
import time


def _force_utf8():
    """Keep non-ASCII paths readable when PowerShell captures the pipe (cp936)."""
    for stream in (sys.stdout, sys.stderr):
        try:
            stream.reconfigure(encoding="utf-8", errors="replace")
        except Exception:
            pass


_force_utf8()

# ---------------------------------------------------------------- structures
IF_MAX_STRING_SIZE = 256


class MIB_IF_ROW2_flags(ctypes.Structure):
    _fields_ = [
        ("HardwareInterface", ctypes.c_uint8, 1),
        ("FilterInterface", ctypes.c_uint8, 1),
        ("ConnectorPresent", ctypes.c_uint8, 1),
        ("NotAuthenticated", ctypes.c_uint8, 1),
        ("NotMediaConnected", ctypes.c_uint8, 1),
        ("Paused", ctypes.c_uint8, 1),
        ("LowPower", ctypes.c_uint8, 1),
        ("EndPointInterface", ctypes.c_uint8, 1),
    ]


class MIB_IF_ROW2(ctypes.Structure):
    _fields_ = [
        ("InterfaceLuid", ctypes.c_uint64),
        ("InterfaceIndex", ctypes.c_uint32),
        ("InterfaceGuid", ctypes.c_byte * 16),
        ("Alias", ctypes.c_wchar * (IF_MAX_STRING_SIZE + 1)),
        ("Description", ctypes.c_wchar * (IF_MAX_STRING_SIZE + 1)),
        ("PhysicalAddressLength", ctypes.c_uint32),
        ("PhysicalAddress", ctypes.c_uint8 * 32),
        ("PermanentPhysicalAddress", ctypes.c_uint8 * 32),
        ("Mtu", ctypes.c_uint32),
        ("Type", ctypes.c_uint32),
        ("TunnelType", ctypes.c_uint32),
        ("MediaType", ctypes.c_uint32),
        ("PhysicalMediumType", ctypes.c_uint32),
        ("AccessType", ctypes.c_uint32),
        ("DirectionType", ctypes.c_uint32),
        ("InterfaceAndOperStatusFlags", MIB_IF_ROW2_flags),
        ("OperStatus", ctypes.c_uint32),
        ("AdminStatus", ctypes.c_uint32),
        ("MediaConnectState", ctypes.c_uint32),
        ("NetworkGuid", ctypes.c_byte * 16),
        ("ConnectionType", ctypes.c_uint32),
        ("TransmitLinkSpeed", ctypes.c_uint64),
        ("ReceiveLinkSpeed", ctypes.c_uint64),
        ("InOctets", ctypes.c_uint64),
        ("InUcastPkts", ctypes.c_uint64),
        ("InNUcastPkts", ctypes.c_uint64),
        ("InDiscards", ctypes.c_uint64),
        ("InErrors", ctypes.c_uint64),
        ("InUnknownProtos", ctypes.c_uint64),
        ("InUcastOctets", ctypes.c_uint64),
        ("InMulticastOctets", ctypes.c_uint64),
        ("InBroadcastOctets", ctypes.c_uint64),
        ("OutOctets", ctypes.c_uint64),
        ("OutUcastPkts", ctypes.c_uint64),
        ("OutNUcastPkts", ctypes.c_uint64),
        ("OutDiscards", ctypes.c_uint64),
        ("OutErrors", ctypes.c_uint64),
        ("OutUcastOctets", ctypes.c_uint64),
        ("OutMulticastOctets", ctypes.c_uint64),
        ("OutBroadcastOctets", ctypes.c_uint64),
        ("OutQLen", ctypes.c_uint64),
    ]


EXPECTED_SIZE = 1352
IFTYPE_SOFTWARE_LOOPBACK = 24
IFTYPE_TUNNEL = 131
IF_OPER_STATUS_UP = 1

iphlpapi = ctypes.WinDLL("iphlpapi.dll")
iphlpapi.GetIfTable2.argtypes = [ctypes.POINTER(ctypes.c_void_p)]
iphlpapi.GetIfTable2.restype = wt.DWORD

# the MIB_IF_TABLE2 buffer is released with FreeMibTable (FreeIfTable2 is NOT exported)
_free_mib_table = None
for _name in ("FreeMibTable", "FreeIfTable2"):
    try:
        _fn = getattr(iphlpapi, _name)
        _fn.argtypes = [ctypes.c_void_p]
        _fn.restype = None
        _free_mib_table = _fn
        break
    except AttributeError:
        continue


def read_rows():
    """Return list of MIB_IF_ROW2 copies."""
    table = ctypes.c_void_p()
    rc = iphlpapi.GetIfTable2(ctypes.byref(table))
    if rc != 0:
        raise OSError("GetIfTable2 failed, rc=%d" % rc)
    try:
        num = ctypes.c_uint32.from_address(table.value).value
        base = table.value + 8  # ULONG NumEntries + 4 bytes padding (align 8)
        stride = ctypes.sizeof(MIB_IF_ROW2)
        rows = []
        for i in range(num):
            rows.append(MIB_IF_ROW2.from_address(base + i * stride))
        return rows
    finally:
        if _free_mib_table is not None:
            _free_mib_table(table)


def select_rows(rows):
    """Physical/protocol interfaces only.

    Windows exposes one MIB_IF_ROW2 per NDIS filter layer (WFP / QoS scheduler),
    all reporting the SAME octet counters as their parent NIC. Summing them
    multiplies the true rate by 2-6x, so the FilterInterface bit must be dropped.
    """
    out = []
    for r in rows:
        if r.Type in (IFTYPE_SOFTWARE_LOOPBACK, IFTYPE_TUNNEL):
            continue
        if r.OperStatus != IF_OPER_STATUS_UP:
            continue
        if r.InterfaceAndOperStatusFlags.FilterInterface:
            continue
        if not r.InterfaceAndOperStatusFlags.HardwareInterface and r.Mtu == 0:
            continue
        out.append(r)
    return out


def totals(rows):
    return (sum(int(r.InOctets) for r in rows), sum(int(r.OutOctets) for r in rows))


def human(bps):
    v = float(bps)
    for unit in ("B/s", "KB/s", "MB/s", "GB/s"):
        if v < 1024.0 or unit == "GB/s":
            return "%.2f %s" % (v, unit)
        v /= 1024.0


def cmd_json(rows, kept):
    data = []
    for r in kept:
        data.append({
            "index": r.InterfaceIndex,
            "alias": r.Alias,
            "description": r.Description,
            "type": r.Type,
            "oper_status": r.OperStatus,
            "mtu": r.Mtu,
            "in_octets": int(r.InOctets),
            "out_octets": int(r.OutOctets),
            "recv_link_speed": int(r.ReceiveLinkSpeed),
            "hardware": bool(r.InterfaceAndOperStatusFlags.HardwareInterface),
            "filter_layer": bool(r.InterfaceAndOperStatusFlags.FilterInterface),
        })
    print(json.dumps({"total_rows": len(rows), "kept_rows": len(kept), "interfaces": data},
                     ensure_ascii=False, indent=2))
    print("")
    print("METRIC: total_rows=%d aggregated_rows=%d" % (len(rows), len(kept)))
    if not kept:
        print("RESULT: SKIP (no interface left after excluding loopback/tunnel/filter layers)")
        return 2
    print("RESULT: PASS (interface table parsed, %d interface(s) eligible for aggregation)" % len(kept))
    return 0


def cmd_monitor(seconds):
    rows0 = select_rows(read_rows())
    if not rows0:
        print("RESULT: SKIP (no matching interface: loopback/tunnel excluded, need OperStatus=Up)")
        return 2
    print("AC-3 NETWORK GROUND TRUTH (GetIfTable2, 1 Hz)")
    print("  sizeof(MIB_IF_ROW2) = %d bytes%s" % (ctypes.sizeof(MIB_IF_ROW2),
          "" if ctypes.sizeof(MIB_IF_ROW2) == EXPECTED_SIZE else "  <-- WARNING: expected %d, layout may be wrong" % EXPECTED_SIZE))
    for r in rows0:
        print("  iface idx=%-3d type=%-3d mtu=%-5d %s" % (r.InterfaceIndex, r.Type, r.Mtu, r.Alias))
    print("")
    print("  t_s   down_B/s        down_human     up_B/s")
    pin, pout = totals(rows0)
    peak = 0
    t0 = time.time()
    for i in range(seconds):
        time.sleep(1.0)
        cur_in, cur_out = totals(select_rows(read_rows()))
        el = time.time() - t0
        t0 = time.time()
        din = max(0, cur_in - pin)
        dout = max(0, cur_out - pout)
        pin, pout = cur_in, cur_out
        rate = din / el
        peak = max(peak, rate)
        print("  %-4d  %-14d  %-14s  %d" % (i + 1, int(rate), human(rate), int(dout / el)))
    print("")
    print("METRIC: peak_down_bps=%d" % int(peak))
    print("RESULT: PASS (baseline read successfully; %d interface(s) aggregated)" % len(rows0))
    return 0


def cmd_compare(seconds, measured, tol, url):
    """Ground truth = bytes curl actually received / curl's own wall clock.

    Two numbers are always reported and written to docs/verify/net.txt:
      GROUND_TRUTH_DOWN_BPS  - application-layer bytes (curl -w %{size_download}/%{time_total})
      NIC_AGG_DOWN_BPS       - this script's GetIfTable2 aggregation over the same window
    NIC is expected to be a few percent HIGHER (TCP/IP + ethernet framing overhead).
    The AC-3 verdict itself compares GROUND_TRUTH vs the number read off the floating
    ball (-measured), never against a second in-house counter.
    """
    rows0 = select_rows(read_rows())
    if not rows0:
        print("RESULT: SKIP (no matching interface)")
        return 2

    print("AC-3 COMPARE MODE (independent ground truth via curl)")
    # ---- build freshness gate: a stale exe makes every number meaningless ----
    try:
        import nb_build_info
        bi = nb_build_info.build_info()
        if bi.get("stale"):
            print("RESULT: BLOCKED (被测二进制早于源码，需重新构建 - exe older than src)")
            return 3
    except ImportError:
        print("  WARNING: nb_build_info.py missing, build freshness NOT verified")
    print("  url    : %s" % url)
    print("  window : up to %ds (stops as soon as curl exits)" % seconds)
    print("  ifaces : %d aggregated (OperStatus=Up, type not 24/131, FilterInterface=0)" % len(rows0))

    null_sink = "NUL" if os.name == "nt" else "/dev/null"
    wf = os.path.join(tempfile.gettempdir(), "nb_ac3_curl.txt")
    if os.path.exists(wf):
        try:
            os.remove(wf)
        except Exception:
            pass
    cmd = ["curl.exe" if os.name == "nt" else "curl", "-s", "-L",
           "--max-time", str(max(5, seconds)),
           "-o", null_sink,
           "-w", "%{size_download} %{time_total} %{speed_download} %{http_code}",
           url]
    print("  cmd    : %s" % " ".join(cmd))
    try:
        fout = open(wf, "w")
        pin, _ = totals(rows0)
        t_start = time.time()
        proc = subprocess.Popen(cmd, stdout=fout, stderr=subprocess.PIPE)
    except FileNotFoundError:
        print("RESULT: BLOCKED (curl.exe not found; cannot obtain ground truth)")
        return 3

    samples = []
    t_prev = t_start
    while True:
        time.sleep(0.5)
        now = time.time()
        cur_in, _ = totals(select_rows(read_rows()))
        delta = max(0, cur_in - pin)
        pin = cur_in
        samples.append((max(1e-3, now - t_prev), delta))
        t_prev = now
        print("  t=%4.1fs  nic_down=%s" % (now - t_start, human(delta / max(1e-3, now - t_prev))))
        if proc.poll() is not None:
            break
        if now - t_start > seconds:
            print("  (window exhausted, killing curl)")
            proc.kill()
            break

    rc = proc.wait()
    try:
        fout.close()
    except Exception:
        pass

    # one final drain so bytes of the last packet burst are counted
    time.sleep(0.6)
    cur_in, _ = totals(select_rows(read_rows()))
    samples.append((0.6, max(0, cur_in - pin)))

    nic_bytes = sum(d for _, d in samples)
    nic_sec = sum(t for t, _ in samples)
    nic_bps = nic_bytes / nic_sec if nic_sec > 0 else 0.0

    raw = ""
    try:
        with open(wf, "r", encoding="utf-8", errors="replace") as f:
            raw = f.read().strip()
    except Exception:
        pass
    print("")
    print("  curl rc      : %d" % rc)
    print("  curl -w out  : %r   (size_download time_total speed_download http_code)" % raw)

    parts = raw.split()
    if rc != 0 or len(parts) < 2:
        print("RESULT: BLOCKED (curl failed: rc=%d, output=%r -> no ground truth)" % (rc, raw))
        return 3
    try:
        size_dl = float(parts[0])
        time_tot = float(parts[1])
    except ValueError:
        print("RESULT: BLOCKED (cannot parse curl -w output: %r)" % raw)
        return 3
    if size_dl <= 0 or time_tot <= 0:
        print("RESULT: BLOCKED (curl transferred %s bytes in %s s -> network unavailable)" % (parts[0], parts[1]))
        return 3
    if size_dl < 1000000:
        # cloudflare caps the endpoint: bytes=104857600 silently returns 1 byte.
        # Never derive a verdict from a sub-megabyte "download".
        print("RESULT: BLOCKED-流量未建立 (curl got only %d bytes, need >= 1000000; "
              "check --url, the endpoint caps large bytes= values)" % int(size_dl))
        return 3

    truth_bps = size_dl / time_tot
    overhead = (nic_bps - truth_bps) / truth_bps * 100.0 if truth_bps > 0 else 0.0

    print("")
    print("  curl bytes   : %d B in %.3f s  (http %s)" % (int(size_dl), time_tot, parts[3] if len(parts) > 3 else "?"))
    print("  NIC window   : %d B over %.2f s" % (nic_bytes, nic_sec))
    print("GROUND_TRUTH_DOWN_BPS=%d" % int(truth_bps))
    print("GROUND_TRUTH_DOWN_HUMAN=%s" % human(truth_bps))
    print("NIC_AGG_DOWN_BPS=%d" % int(nic_bps))
    print("  nic vs truth : %+.2f %%  (2-8 %% protocol overhead is normal)" % overhead)
    if overhead < -5.0:
        print("  WARNING: NIC aggregation is BELOW application-layer bytes - sampling window mismatch")
    if overhead > 40.0:
        print("  WARNING: NIC aggregation far above truth - duplicated counter rows (FilterInterface) suspected")

    # ---- evidence file: both numbers, per leader's AC-3 rule ----
    net_txt = os.path.join(os.path.dirname(os.path.abspath(__file__)), "net.txt")
    globals()["_net_record"] = {
        "url": url, "curl_bytes": int(size_dl), "curl_seconds": time_tot,
        "truth_bps": int(truth_bps), "nic_bps": int(nic_bps),
        "overhead_pct": overhead, "measured_bps": measured, "tol_pct": tol,
    }
    write_net_txt(net_txt)

    if measured is None:
        print("")
        print("RESULT: SKIP (ground truth written to net.txt; pass --measured <B/s read off the ball> for the verdict)")
        return 2

    err = abs(measured - truth_bps) / truth_bps * 100.0
    print("")
    print("  ball reading : %d B/s (%s)" % (measured, human(measured)))
    print("  relative err : %.2f %%  (tolerance %.1f %%)" % (err, tol))
    print("METRIC: truth_bps=%d nic_bps=%d measured_bps=%d error_pct=%.2f" % (int(truth_bps), int(nic_bps), measured, err))
    globals()["_net_record"]["error_pct"] = err
    write_net_txt(net_txt)
    if err <= tol:
        print("RESULT: PASS (ball reading within %.1f%% of curl ground truth)" % tol)
        return 0
    print("RESULT: FAIL (ball reading off by %.2f%% > %.1f%%)" % (err, tol))
    return 1


def write_net_txt(path):
    rec = globals().get("_net_record") or {}
    lines = ["# NetBall AC-3 evidence  %s" % time.strftime("%Y-%m-%d %H:%M:%S"),
             "# mode: independent ground truth = curl bytes / curl wall clock",
             "# NIC aggregation = GetIfTable2, OperStatus=1, type not in {24,131}, FilterInterface=0",
             ""]
    for k in ("url", "curl_bytes", "curl_seconds", "truth_bps", "nic_bps", "overhead_pct",
              "measured_bps", "error_pct", "tol_pct"):
        if k in rec:
            v = rec[k]
            if isinstance(v, float):
                v = round(v, 3)
            lines.append("%s=%s" % (k, v))
    lines.append("")
    lines.append("# GROUND_TRUTH_DOWN_BPS and the ball reading MUST both appear in the final report")
    with open(path, "w", encoding="utf-8") as f:
        f.write("\n".join(lines) + "\n")


def main():
    ap = argparse.ArgumentParser(description="AC-3 network speed ground truth via GetIfTable2")
    ap.add_argument("--seconds", type=int, default=10)
    ap.add_argument("--compare", action="store_true")
    ap.add_argument("--measured", type=int, default=None, help="B/s read from the floating ball")
    ap.add_argument("--tol", type=float, default=20.0)
    # NOTE: cloudflare's endpoint caps out - bytes=104857600 returns 1 byte only.
    # 20,000,000 is stable (~5 s) and is the value agreed with builder-data-1.
    ap.add_argument("--url", default="https://speed.cloudflare.com/__down?bytes=20000000")
    ap.add_argument("--json", action="store_true")
    args = ap.parse_args()

    if ctypes.sizeof(MIB_IF_ROW2) != EXPECTED_SIZE:
        print("WARNING: sizeof(MIB_IF_ROW2)=%d, expected %d (layout risk)" %
              (ctypes.sizeof(MIB_IF_ROW2), EXPECTED_SIZE))

    try:
        all_rows = read_rows()
    except OSError as e:
        print("RESULT: FAIL (%s)" % e)
        return 1

    kept = select_rows(all_rows)
    if args.json:
        return cmd_json(all_rows, kept)
    if args.compare:
        return cmd_compare(args.seconds, args.measured, args.tol, args.url)
    return cmd_monitor(args.seconds)


if __name__ == "__main__":
    sys.exit(main())
