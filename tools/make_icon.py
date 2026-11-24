#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""tools/make_icon.py -- NetBall 图标生成器（纯脚本自绘，不依赖任何外部素材）

用法:
    python tools/make_icon.py             生成 assets/app.ico
    python tools/make_icon.py --preview   额外导出 assets/icon-preview-256.png

设计:
    * 深色圆底：径向渐变 RGB(28,32,42) -> RGB(14,16,22)
    * 尺寸 >= 32：内嵌上下双箭头表示网速
        上箭头 青色 #22D3EE (34,211,238)
        下箭头 绿色 #34D399 (52,211,153)
        两支箭头左右分立，中心留白干净
    * 尺寸 <= 24：单独绘制简化版（深色圆底 + 单条白色上升折线），保证小尺寸可辨识
    * 所有尺寸按 4x 超采样绘制，再 resize(LANCZOS) 抗锯齿
    * 幂等：同参数重复运行，输出字节完全一致（无时间戳字段）
    * 保存使用 Pillow 的 sizes=[...] + append_images（按尺寸精确匹配，各尺寸分辨率独立）
"""

import argparse
import hashlib
import math
import os
import struct
import sys

from PIL import Image, ImageDraw

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ASSETS_DIR = os.path.join(ROOT, "assets")
ICO_PATH = os.path.join(ASSETS_DIR, "app.ico")
PREVIEW_PATH = os.path.join(ASSETS_DIR, "icon-preview-256.png")

SIZES = [16, 24, 32, 48, 64, 128, 256]
SUPERSAMPLE = 4          # 4x 超采样
SMALL_MAX = 24           # <= 该尺寸使用简化图形
GRAD_MASTER = 1024       # 径向渐变母版边长（= 256 * 4）

CENTER_RGB = (28, 32, 42)
EDGE_RGB = (14, 16, 22)
ARROW_UP_RGB = (34, 211, 238)     # #22D3EE
ARROW_DOWN_RGB = (52, 211, 153)   # #34D399
LINE_RGB = (255, 255, 255)        # 小尺寸折线

_grad_cache = {}


def _radial_gradient(n):
    """生成 n x n 的径向渐变 RGB 图（中心 CENTER_RGB -> 边缘 EDGE_RGB）。"""
    cached = _grad_cache.get(n)
    if cached is not None:
        return cached

    cx = (n - 1) / 2.0
    inv = 2.0 / n
    xs = [((x - cx) * inv) ** 2 for x in range(n)]
    cr, cg, cb = CENTER_RGB
    dr = EDGE_RGB[0] - cr
    dg = EDGE_RGB[1] - cg
    db = EDGE_RGB[2] - cb

    buf = bytearray(n * n * 3)
    o = 0
    sqrt = math.sqrt
    for y in range(n):
        dy = (y - cx) * inv
        dy2 = dy * dy
        for x in range(n):
            d = sqrt(xs[x] + dy2)
            if d > 1.0:
                d = 1.0
            d *= 0.60 + 0.40 * d          # 轻微缓动，让中心亮区更集中、边缘过渡更柔和
            buf[o] = int(cr + dr * d)
            buf[o + 1] = int(cg + dg * d)
            buf[o + 2] = int(cb + db * d)
            o += 3

    img = Image.frombytes("RGB", (n, n), bytes(buf))
    _grad_cache[n] = img
    return img


def _gradient_for(work):
    """取 work x work 的渐变（母版等比缩放到目标超采样尺寸）。"""
    master = _radial_gradient(GRAD_MASTER)
    if work == GRAD_MASTER:
        return master
    return master.resize((work, work), Image.Resampling.BOX)


def _draw_arrows(draw, s):
    """大尺寸：上箭头(青) + 下箭头(绿)，左右分立，中心留白。"""
    up_cx = s * 0.345
    dn_cx = s * 0.655
    tip = s * 0.185
    base = s * 0.335
    tail = s * 0.815
    hw = s * 0.112            # 箭头三角半宽
    sw = s * 0.046            # 箭杆半宽

    draw.polygon(
        [
            (up_cx, tip),
            (up_cx + hw, base),
            (up_cx + sw, base),
            (up_cx + sw, tail),
            (up_cx - sw, tail),
            (up_cx - sw, base),
            (up_cx - hw, base),
        ],
        fill=ARROW_UP_RGB + (255,),
    )
    draw.polygon(
        [
            (dn_cx, s - tip),
            (dn_cx + hw, s - base),
            (dn_cx + sw, s - base),
            (dn_cx + sw, s - tail),
            (dn_cx - sw, s - tail),
            (dn_cx - sw, s - base),
            (dn_cx - hw, s - base),
        ],
        fill=ARROW_DOWN_RGB + (255,),
    )


def _draw_polyline(draw, s):
    """小尺寸：单条白色上升折线（简化版）。"""
    pts = [
        (s * 0.235, s * 0.700),
        (s * 0.415, s * 0.500),
        (s * 0.575, s * 0.620),
        (s * 0.775, s * 0.330),
    ]
    w = max(SUPERSAMPLE, int(round(s * 0.100)))
    draw.line(pts, fill=LINE_RGB + (255,), width=w, joint="curve")
    r = w / 2.0
    for px, py in (pts[0], pts[-1]):
        draw.ellipse([px - r, py - r, px + r, py + r], fill=LINE_RGB + (255,))


def render(size):
    """绘制单个尺寸的图标（4x 超采样 -> LANCZOS 下采样）。"""
    work = size * SUPERSAMPLE

    canvas = Image.new("RGBA", (work, work), (0, 0, 0, 0))
    mask = Image.new("L", (work, work), 0)
    ImageDraw.Draw(mask).ellipse([0, 0, work - 1, work - 1], fill=255)
    canvas.paste(_gradient_for(work).convert("RGBA"), (0, 0), mask)

    draw = ImageDraw.Draw(canvas)
    if size <= SMALL_MAX:
        _draw_polyline(draw, work)
    else:
        _draw_arrows(draw, work)

    return canvas.resize((size, size), Image.Resampling.LANCZOS)


def _probe_ico(path):
    """解析 ICO 目录，返回 (总字节数, [(w, h, payload_bytes), ...])。"""
    with open(path, "rb") as fh:
        data = fh.read()
    reserved, rtype, count = struct.unpack_from("<HHH", data, 0)
    if reserved != 0 or rtype != 1:
        raise ValueError("not a valid .ico header: reserved=%d type=%d" % (reserved, rtype))
    entries = []
    for i in range(count):
        w, h, _colors, _res, _planes, _bits, nbytes, off = struct.unpack_from(
            "<BBBBHHII", data, 6 + i * 16
        )
        if off + nbytes > len(data):
            raise ValueError("frame #%d payload out of range" % i)
        entries.append(((w or 256), (h or 256), nbytes))
    entries.sort()
    return len(data), entries


def main(argv=None):
    parser = argparse.ArgumentParser(description="NetBall 图标生成器（自绘多尺寸 ICO）")
    parser.add_argument(
        "--preview",
        action="store_true",
        help="额外导出 assets/icon-preview-256.png 供人工查看",
    )
    args = parser.parse_args(argv)

    os.makedirs(ASSETS_DIR, exist_ok=True)

    frames = {size: render(size) for size in SIZES}
    master = frames[256]
    others = [frames[size] for size in SIZES if size != 256]

    # Pillow 按 sizes 逐项匹配 append_images 中「尺寸完全相同」的图，否则从主图缩放。
    # 这样 16/24 的简化版不会被 256 的复杂版下采样糊掉。
    master.save(
        ICO_PATH,
        format="ICO",
        sizes=[(size, size) for size in SIZES],
        append_images=others,
        bitmap_format="png",
    )

    total_bytes, entries = _probe_ico(ICO_PATH)
    with open(ICO_PATH, "rb") as fh:
        digest = hashlib.sha256(fh.read()).hexdigest()

    with Image.open(ICO_PATH) as im:
        reported = sorted((w, h) for (w, h) in im.ico.sizes())

    found = [(w, h) for w, h, _nbytes in entries]
    missing = [s for s in SIZES if (s, s) not in found]

    rel = os.path.relpath(ICO_PATH, ROOT).replace("\\", "/")
    print("[make_icon] %s" % rel)
    print("  bytes      : %d" % total_bytes)
    print("  frames     : %d" % len(entries))
    for w, h, nbytes in entries:
        print("    %3dx%-3d  %8d bytes" % (w, h, nbytes))
    print("  sizes      : %s" % ", ".join("%dx%d" % (w, h) for w, h in found))
    print("  pil.sizes  : %s" % ", ".join("%dx%d" % (w, h) for w, h in reported))
    print("  sha256     : %s" % digest)

    if missing:
        print("[FAIL] 缺少尺寸: %s" % missing, file=sys.stderr)
        return 1
    if reported != sorted((s, s) for s in SIZES):
        print("[FAIL] Pillow 报告的尺寸不一致: %s" % (reported,), file=sys.stderr)
        return 1
    if any(nbytes <= 0 for _w, _h, nbytes in entries):
        print("[FAIL] 存在空白帧", file=sys.stderr)
        return 1

    if args.preview:
        master.save(PREVIEW_PATH, format="PNG")
        print("  preview    : %s" % os.path.relpath(PREVIEW_PATH, ROOT).replace("\\", "/"))

    print("[ok] 7 个尺寸齐全，图标生成成功")
    return 0


if __name__ == "__main__":
    sys.exit(main())
