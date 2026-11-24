#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""tools/make_wizard.py -- NetBall 安装向导视觉素材生成器（纯脚本自绘，无外部素材）

产出（写入 assets/installer/）:
    wizard-large.bmp   164 x 314  24bpp  (Inno Setup WizardImageFile)
    wizard-small.bmp    55 x 58   24bpp  (Inno Setup WizardSmallImageFile)

设计（与 assets/app.ico 同一套视觉语言）:
    * 背景：竖直深色渐变 RGB(28,32,42) #1C202A -> RGB(14,16,22) #0E1016
    * 中央偏上：径向渐变圆底徽标（与 make_icon.py 相同的径向渐变数学：
      中心亮、边缘暗，d *= 0.60 + 0.40*d 缓动）+ 细描边 + 左上高光弧
    * 徽标下方：上箭头 青色 #22D3EE (34,211,238) / 下箭头 绿色 #34D399 (52,211,153)
      箭头几何沿用 make_icon.py 的比例（tip 0.185 / base 0.335 / tail 0.815 /
      三角半宽 0.112 / 箭杆半宽 0.046），保证与图标同源
    * 底部：白色 ASCII 文字 "NetBall"（PIL 内置字体，不依赖系统中文字体）
    * 55x58 小图：圆底 + 双箭头剪影，无文字
    * 4x 超采样后 LANCZOS 下采样；落盘前合成到不透明深色底并转 RGB
      （BMP V3 header / 24 位 / 无 alpha，Inno 对尺寸与色深敏感）

要点:
    * Inno Setup 的 WizardImageFile 要求精确 164x314，WizardSmallImageFile 要求
      精确 55x58；尺寸差 1 像素即被拉伸变形，这里由断言强制约束。
    * 幂等：同参数重复运行，输出字节完全一致（无时间戳写入）。

用法:
    python tools/make_wizard.py
    python tools/make_wizard.py --out <目录>
"""

import argparse
import math
import os
import struct
import sys

from PIL import Image, ImageDraw, ImageFont

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT_DIR = os.path.join(ROOT, "assets", "installer")

LARGE_SIZE = (164, 314)
SMALL_SIZE = (55, 58)

SUPERSAMPLE = 4
GRAD_MASTER = 1024          # 径向渐变母版边长

BG_TOP_RGB = (0x1C, 0x20, 0x2A)      # #1C202A
BG_BOTTOM_RGB = (0x0E, 0x10, 0x16)   # #0E1016
ARROW_UP_RGB = (0x22, 0xD3, 0xEE)    # #22D3EE 青
ARROW_DOWN_RGB = (0x34, 0xD3, 0x99)  # #34D399 绿
TEXT_RGB = (0xFF, 0xFF, 0xFF)
RIM_RGB = (0x4A, 0x55, 0x6E)
HILITE_RGB = (0x9F, 0xB4, 0xD4)

BADGE_CENTER_RGB = (0x3E, 0x47, 0x5E)   # 比背景亮一档，让圆底浮起来
BADGE_EDGE_RGB = (0x11, 0x14, 0x1B)

_radial_cache = {}


def _radial(n, center_rgb, edge_rgb):
    """n x n 径向渐变 RGB（与 make_icon._radial_gradient 同一数学，颜色可配）。"""
    key = (n, center_rgb, edge_rgb)
    cached = _radial_cache.get(key)
    if cached is not None:
        return cached

    cx = (n - 1) / 2.0
    inv = 2.0 / n
    xs = [((x - cx) * inv) ** 2 for x in range(n)]
    cr, cg, cb = center_rgb
    dr = edge_rgb[0] - cr
    dg = edge_rgb[1] - cg
    db = edge_rgb[2] - cb

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
            d *= 0.60 + 0.40 * d
            buf[o] = int(cr + dr * d)
            buf[o + 1] = int(cg + dg * d)
            buf[o + 2] = int(cb + db * d)
            o += 3

    img = Image.frombytes("RGB", (n, n), bytes(buf))
    _radial_cache[key] = img
    return img


def _vertical_bg(w, h):
    """竖直深色渐变 #1C202A -> #0E1016（先在 1 x h 上算，再横向拉满）。"""
    col = Image.new("RGB", (1, h))
    px = col.load()
    tr, tg, tb = BG_TOP_RGB
    br, bg, bb = BG_BOTTOM_RGB
    denom = float(max(1, h - 1))
    for y in range(h):
        t = y / denom
        t = t * t * (3.0 - 2.0 * t) * 0.45 + t * 0.55     # 轻微 S 曲线，底部更深
        px[0, y] = (
            int(round(tr + (br - tr) * t)),
            int(round(tg + (bg - tg) * t)),
            int(round(tb + (bb - tb) * t)),
        )
    return col.resize((w, h), Image.Resampling.NEAREST)


def _draw_badge(canvas, cx, cy, radius):
    """圆底徽标：径向渐变 + 细描边 + 左上高光弧。坐标均为画布像素。"""
    d = int(round(radius * 2))
    if d < 2:
        return
    grad = _radial(GRAD_MASTER, BADGE_CENTER_RGB, BADGE_EDGE_RGB)
    if d != GRAD_MASTER:
        grad = grad.resize((d, d), Image.Resampling.BOX)

    mask = Image.new("L", (d, d), 0)
    ImageDraw.Draw(mask).ellipse([0, 0, d - 1, d - 1], fill=255)

    left = int(round(cx - radius))
    top = int(round(cy - radius))
    canvas.paste(grad, (left, top), mask)

    draw = ImageDraw.Draw(canvas)
    box = [left, top, left + d - 1, top + d - 1]

    # 描边（1.6% 直径，最少 2px）
    lw = max(2, int(round(d * 0.016)))
    draw.ellipse(box, outline=RIM_RGB, width=lw)

    # 左上高光弧：在上半圆偏左范围内画一段更亮的弧
    draw.arc(box, start=196, end=284, fill=HILITE_RGB, width=max(2, lw))

    # 左上游离高光点：让圆底看起来是球体而非空心圈
    hr = radius * 0.42
    hx = cx - radius * 0.30
    hy = cy - radius * 0.30
    hl = Image.new("RGBA", (int(hr * 2) + 2, int(hr * 2) + 2), (0, 0, 0, 0))
    hd = ImageDraw.Draw(hl)
    steps = 18
    for i in range(steps, 0, -1):
        rr = hr * i / steps
        a = int(26 * (1.0 - i / steps) ** 1.6) + 1
        hd.ellipse([hr + 1 - rr, hr + 1 - rr, hr + 1 + rr, hr + 1 + rr],
                   fill=(0xD8, 0xE6, 0xFF, a))
    canvas.alpha_composite(hl, (int(round(hx - hr)), int(round(hy - hr))))


def _arrow(draw, cx, tip_y, base_y, tail_y, half_w, half_shaft, up, rgb):
    """箭头多边形（与 make_icon.py 的同构比例）。up=True 箭头朝上。"""
    w = half_w
    s = half_shaft
    if up:
        pts = [
            (cx, tip_y),
            (cx + w, base_y),
            (cx + s, base_y),
            (cx + s, tail_y),
            (cx - s, tail_y),
            (cx - s, base_y),
            (cx - w, base_y),
        ]
    else:
        pts = [
            (cx, tip_y),
            (cx + w, base_y),
            (cx + s, base_y),
            (cx + s, tail_y),
            (cx - s, tail_y),
            (cx - s, base_y),
            (cx - w, base_y),
        ]
    draw.polygon(pts, fill=rgb + (255,))


def _text_mask(text, font_px, canvas_size):
    """用 PIL 内置字体渲染文字，返回 (RGBA 层, 粘贴位置)。"""
    font = ImageFont.load_default(size=font_px)
    probe = Image.new("L", (1, 1))
    bbox = ImageDraw.Draw(probe).textbbox((0, 0), text, font=font)
    tw = bbox[2] - bbox[0]
    th = bbox[3] - bbox[1]

    layer = Image.new("RGBA", (tw + 8, th + 8), (0, 0, 0, 0))
    ImageDraw.Draw(layer).text((4 - bbox[0], 4 - bbox[1]), text, font=font,
                               fill=TEXT_RGB + (255,))
    x = (canvas_size[0] - layer.size[0]) // 2
    return layer, (x, 0)


def _render_large():
    """164 x 314：渐变底 + 圆底徽标 + 下箭头组 + 底部 NetBall 白字。"""
    fw, fh = LARGE_SIZE
    S = SUPERSAMPLE
    w, h = fw * S, fh * S

    canvas = _vertical_bg(w, h).convert("RGBA")

    # 径向柔光：徽标后方一层极淡的亮区，避免圆底与背景糊在一起
    glow_r = int(74 * S)
    glow = Image.new("RGBA", (glow_r * 2, glow_r * 2), (0, 0, 0, 0))
    gd = ImageDraw.Draw(glow)
    steps = 34
    for i in range(steps, 0, -1):
        r = glow_r * i / steps
        a = int(10 * (1.0 - i / steps) + 2)
        gd.ellipse([glow_r - r, glow_r - r, glow_r + r, glow_r + r],
                   fill=(0x3A, 0x46, 0x5E, a))
    canvas.alpha_composite(glow, (int(82 * S) - glow_r, int(96 * S) - glow_r))

    _draw_badge(canvas, 82 * S, 96 * S, 47 * S)
    draw = ImageDraw.Draw(canvas)

    # 双箭头：左青上 / 右绿下，tip -> base -> tail
    up_cx = 64.0 * S
    dn_cx = 100.0 * S
    tip_y = 158.0 * S
    base_y = 193.0 * S
    tail_y = 232.0 * S
    hw = 14.0 * S
    sw = 5.5 * S
    _arrow(draw, up_cx, tip_y, base_y, tail_y, hw, sw, True, ARROW_UP_RGB)
    _arrow(draw, dn_cx, tail_y, base_y, tip_y, hw, sw, False, ARROW_DOWN_RGB)

    # 底部字标
    text_layer, (tx, _ty) = _text_mask("NetBall", 22 * S, (w, h))
    ty = int(248 * S)
    canvas.alpha_composite(text_layer, (tx, ty))

    out = canvas.convert("RGB").resize((fw, fh), Image.Resampling.LANCZOS)
    return out.convert("RGB")


def _render_small():
    """55 x 58：渐变底 + 圆底 + 双箭头剪影（55px 下可辨识为准，无文字）。"""
    fw, fh = SMALL_SIZE
    S = SUPERSAMPLE
    w, h = fw * S, fh * S

    canvas = _vertical_bg(w, h).convert("RGBA")

    cx, cy, r = 27.5, 28.5, 21.5
    _draw_badge(canvas, cx * S, cy * S, r * S)
    draw = ImageDraw.Draw(canvas)

    tip_y = (cy - 11.5) * S
    base_y = (cy - 2.0) * S
    tail_y = (cy + 11.5) * S
    hw = 6.6 * S
    sw = 2.8 * S
    _arrow(draw, 20.0 * S, tip_y, base_y, tail_y, hw, sw, True, ARROW_UP_RGB)
    _arrow(draw, 35.0 * S, tail_y, base_y, tip_y, hw, sw, False, ARROW_DOWN_RGB)

    out = canvas.convert("RGB").resize((fw, fh), Image.Resampling.LANCZOS)
    return out.convert("RGB")


def _probe_bmp(path):
    """解析 BMP 文件头 + BITMAPINFOHEADER，返回字典。"""
    with open(path, "rb") as fh:
        data = fh.read()
    if len(data) < 54:
        raise ValueError("%s: 文件过短 (%d B)" % (path, len(data)))
    magic = data[0:2]
    file_size, _r1, _r2, off_bits = struct.unpack_from("<IHHI", data, 2)
    hdr_size, bi_w, bi_h, planes, bpp, compression = struct.unpack_from("<IiiHHI", data, 14)
    return {
        "magic": magic,
        "file_size": file_size,
        "off_bits": off_bits,
        "hdr_size": hdr_size,
        "width": bi_w,
        "height": bi_h,
        "planes": planes,
        "bpp": bpp,
        "compression": compression,
        "actual_size": len(data),
    }


def _assert_bmp(path, expect_size):
    info = _probe_bmp(path)
    ew, eh = expect_size
    name = os.path.basename(path)
    problems = []
    if info["magic"] != b"BM":
        problems.append("magic=%r 应为 b'BM'" % (info["magic"],))
    if info["bpp"] != 24:
        problems.append("biBitCount=%d 应为 24" % info["bpp"])
    if (info["width"], info["height"]) != (ew, eh):
        problems.append("尺寸 %dx%d 应为 %dx%d"
                        % (info["width"], info["height"], ew, eh))
    if info["hdr_size"] != 40:
        problems.append("biSize=%d 应为 40 (BMP V3)" % info["hdr_size"])
    if info["compression"] != 0:
        problems.append("biCompression=%d 应为 0 (BI_RGB)" % info["compression"])
    if info["planes"] != 1:
        problems.append("biPlanes=%d 应为 1" % info["planes"])
    if info["file_size"] != info["actual_size"]:
        problems.append("bfSize=%d 与实际 %d 不符"
                        % (info["file_size"], info["actual_size"]))
    # 24bpp 行跨度必须 4 字节对齐
    row = ((ew * 3 + 3) // 4) * 4
    if info["off_bits"] + row * eh != info["actual_size"]:
        problems.append("像素偏移/行跨度不自洽: off=%d row=%d h=%d total=%d"
                        % (info["off_bits"], row, eh, info["actual_size"]))
    print("  %-18s %dx%d %dbpp hdr=%d comp=%d bytes=%d -> %s"
          % (name, info["width"], info["height"], info["bpp"], info["hdr_size"],
             info["compression"], info["actual_size"],
             "OK" if not problems else "FAIL"))
    return problems


def main(argv=None):
    parser = argparse.ArgumentParser(description="NetBall 安装向导图像生成器（164x314 / 55x58 24bpp BMP）")
    parser.add_argument("--out", default=OUT_DIR, help="输出目录（默认 assets/installer）")
    args = parser.parse_args(argv)

    out_dir = os.path.abspath(args.out)
    os.makedirs(out_dir, exist_ok=True)

    large_path = os.path.join(out_dir, "wizard-large.bmp")
    small_path = os.path.join(out_dir, "wizard-small.bmp")

    _render_large().save(large_path, format="BMP")
    _render_small().save(small_path, format="BMP")

    print("[make_wizard] out=%s" % os.path.relpath(out_dir, ROOT).replace("\\", "/"))
    fails = _assert_bmp(large_path, LARGE_SIZE)
    fails += _assert_bmp(small_path, SMALL_SIZE)

    if fails:
        for p in fails:
            print("[FAIL] %s" % p, file=sys.stderr)
        return 1

    print("[ok] wizard-large.bmp 164x314 / wizard-small.bmp 55x58 均为 24bpp 无 alpha BMP")
    return 0


if __name__ == "__main__":
    sys.exit(main())
