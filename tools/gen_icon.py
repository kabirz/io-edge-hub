#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
生成 io-edge-hub 上位机占位图标 (多尺寸 .ico).

设计: 工业风数据采集卡 — 深蓝圆角底 + 卡片轮廓 + 4 路 I/O 信号条
      (代表 16DI/8DO/4AI 的采集节点).
输出: resources/app.ico (含 16/32/48/256 四尺寸, PNG 压缩 256).

风格: 简洁、几何、高对比. 颜色取工业 HMI 常见深蓝 (#1F3A5F) + 信号绿 (#00C896)
      + 高亮白. 后续可随时替换为正式美术资源.
"""
import sys
from PIL import Image, ImageDraw

# 配色
BG = (31, 58, 95, 255)        # 深蓝底
BG_HI = (52, 92, 138, 255)    # 顶部渐变亮色
CARD = (235, 240, 248, 255)   # 卡片白
CARD_EDGE = (40, 60, 90, 255) # 卡片边
GREEN = (0, 200, 150, 255)    # 信号绿
GREEN_HI = (90, 230, 180, 255)
WHITE = (255, 255, 255, 255)
AMBER = (255, 180, 60, 255)   # AI 强调色


def _round_rect(d, xy, r, fill, outline=None, width=1):
    """画圆角矩形 (PIL 的 round_corner 较新版本才有 rounded_rectangle, 这里手画兼容)."""
    try:
        d.rounded_rectangle(xy, radius=r, fill=fill, outline=outline, width=width)
    except AttributeError:
        # 兜底: 老版本 PIL 用 rectangle
        d.rectangle(xy, fill=fill, outline=outline, width=width)


def draw_icon(size: int) -> Image.Image:
    """渲染指定尺寸的图标. 以 256 为基准做按比例缩放."""
    s = 256
    img = Image.new("RGBA", (s, s), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)

    # 1. 外层圆角深蓝底 (留 4px 透明边)
    pad = 8
    _round_rect(d, [pad, pad, s - pad, s - pad], r=40, fill=BG)

    # 顶部高光 (简单两条线模拟渐变)
    _round_rect(d, [pad, pad, s - pad, pad + 50], r=40, fill=BG_HI)

    # 2. 卡片 (居中白色圆角矩形)
    cx, cy = s // 2, s // 2
    cw, ch = 180, 200
    cxy = [cx - cw // 2, cy - ch // 2 - 6, cx + cw // 2, cy + ch // 2 - 6]
    _round_rect(d, cxy, r=12, fill=CARD, outline=CARD_EDGE, width=3)

    # 3. 卡片顶部标题条 (深蓝细条, 模拟"设备标识")
    bar_y0 = cxy[1] + 10
    _round_rect(d, [cxy[0] + 14, bar_y0, cxy[2] - 14, bar_y0 + 18], r=4, fill=BG)
    # 标题条上的小指示灯 (绿)
    d.ellipse([cxy[0] + 22, bar_y0 + 5, cxy[0] + 30, bar_y0 + 13], fill=GREEN)

    # 4. 4 路 I/O 信号条 (3 绿 = DI/DO, 1 橙 = AI)
    sig_y0 = bar_y0 + 34
    sig_h = 18
    sig_gap = 12
    bar_left = cxy[0] + 20
    bar_right = cxy[2] - 20
    for i in range(4):
        y = sig_y0 + i * (sig_h + sig_gap)
        color = AMBER if i == 3 else GREEN
        hi = GREEN_HI if i == 3 else GREEN_HI
        # 背景 (暗色槽)
        _round_rect(d, [bar_left, y, bar_right, y + sig_h], r=4, fill=(220, 225, 232, 255))
        # 信号填充 (不同长度, 模拟实时数据)
        fill_w = int((bar_right - bar_left) * (0.78 - i * 0.12))
        _round_rect(d, [bar_left, y, bar_left + fill_w, y + sig_h], r=4, fill=color)
        # 高光顶线
        d.line([bar_left + 4, y + 2, bar_left + fill_w - 4, y + 2], fill=hi, width=2)

    # 5. 卡片底部连接器条 (金色针脚模拟)
    conn_y = cxy[3] - 24
    _round_rect(d, [cxy[0] + 14, conn_y, cxy[2] - 14, conn_y + 12], r=3, fill=(180, 185, 195, 255))
    for px in range(cxy[0] + 24, cxy[2] - 20, 14):
        d.rectangle([px, conn_y + 3, px + 5, conn_y + 9], fill=(255, 200, 80, 255))

    # 缩放到目标尺寸 (高质量 LANCZOS)
    if size != s:
        img = img.resize((size, size), Image.LANCZOS)
    return img


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else "resources/app.ico"
    sizes = [256, 48, 32, 16]  # 大尺寸在前 (PIL 按 append_images 自身尺寸嵌入)
    imgs = [draw_icon(sz) for sz in sizes]
    # 只传 append_images, 每张图自身的 size 决定嵌入的 ICO entry.
    # 不传 sizes= (与 append_images 冲突, 会导致只嵌入首张).
    imgs[0].save(out, format="ICO", append_images=imgs[1:])
    print(f"已生成 {out} (尺寸 {sizes})")


if __name__ == "__main__":
    main()
