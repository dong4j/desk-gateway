#!/usr/bin/env python3
"""Generate Desk Gateway architecture PNGs (dark theme, no drawio).

Why this script exists:
    README and architecture docs need raster diagrams that render on GitHub
    without extra viewers. The user asked for PNG, not drawio/HTML. Keeping
    a generator next to the images is the only way to restyle labels without
    redrawing by hand.

Constraint:
    Colors, spacing and region/security treatment follow the architecture-diagram
    skill palette. Output is PNG only.

Usage (from repo root or this directory):
    python3 docs/architecture/images/generate_architecture_pngs.py
"""

from __future__ import annotations

import math
from dataclasses import dataclass
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

# Logical canvas is designed ~1200px wide so GitHub's ~900px README column
# still keeps component names around 12px. SCALE=2 is for retina sharpness.
SCALE = 2
OUT_DIR = Path(__file__).resolve().parent

BG = (2, 6, 23, 255)  # #020617
GRID = (30, 41, 59, 255)  # #1e293b
MASK = (15, 23, 42, 255)  # #0f172a opaque underlay so arrows do not show through
WHITE = (255, 255, 255, 255)
MUTED = (148, 163, 184, 255)  # #94a3b8
AMBER = (251, 191, 36, 255)  # region boundary
ARROW = (100, 116, 139, 255)  # #64748b

FRONTEND = ((8, 51, 68, 102), (34, 211, 238, 255))
BACKEND = ((6, 78, 59, 102), (52, 211, 153, 255))
DATABASE = ((76, 29, 149, 102), (167, 139, 250, 255))
SECURITY = ((136, 19, 55, 102), (251, 113, 133, 255))
BUS = ((251, 146, 60, 76), (251, 146, 60, 255))
EXTERNAL = ((30, 41, 59, 128), (148, 163, 184, 255))

FONT_SANS = "/System/Library/Fonts/Hiragino Sans GB.ttc"
FONT_MONO = "/System/Library/Fonts/SFNSMono.ttf"


def _font(path: str, size: int, index: int = 0) -> ImageFont.FreeTypeFont:
    """Load a scaled TrueType face. Index picks weight inside a .ttc."""
    return ImageFont.truetype(path, size * SCALE, index=index)


def font_title() -> ImageFont.FreeTypeFont:
    return _font(FONT_SANS, 22, index=2)  # W6


def font_name() -> ImageFont.FreeTypeFont:
    return _font(FONT_SANS, 14, index=2)


def font_sub() -> ImageFont.FreeTypeFont:
    return _font(FONT_SANS, 11, index=0)  # W3


def font_tiny() -> ImageFont.FreeTypeFont:
    return _font(FONT_SANS, 10, index=0)


def font_mono() -> ImageFont.FreeTypeFont:
    return _font(FONT_MONO, 12, index=0)


def font_mono_small() -> ImageFont.FreeTypeFont:
    return _font(FONT_MONO, 10, index=0)


def px(v: float) -> int:
    return int(round(v * SCALE))


@dataclass(frozen=True)
class Box:
    """Axis-aligned component in logical (unscaled) coordinates."""

    x: float
    y: float
    w: float
    h: float

    @property
    def cx(self) -> float:
        return self.x + self.w / 2

    @property
    def cy(self) -> float:
        return self.y + self.h / 2

    @property
    def bottom(self) -> float:
        return self.y + self.h

    @property
    def right(self) -> float:
        return self.x + self.w


class Canvas:
    """Pillow surface with architecture-diagram helpers."""

    def __init__(self, width: int, height: int) -> None:
        self.w = width
        self.h = height
        self.im = Image.new("RGBA", (px(width), px(height)), BG)
        self.draw = ImageDraw.Draw(self.im)
        self._grid()

    def _grid(self) -> None:
        step = 40
        for x in range(0, self.w + 1, step):
            self.draw.line([(px(x), 0), (px(x), px(self.h))], fill=GRID, width=1)
        for y in range(0, self.h + 1, step):
            self.draw.line([(0, px(y)), (px(self.w), px(y))], fill=GRID, width=1)

    def rounded(self, box: Box, fill, outline, width: float = 1.5, radius: float = 6) -> None:
        xy = [px(box.x), px(box.y), px(box.right), px(box.bottom)]
        self.draw.rounded_rectangle(xy, radius=px(radius), fill=fill, outline=outline, width=max(1, px(width)))

    def component(self, box: Box, palette, title: str, subtitle: str = "", mono_title: bool = False) -> None:
        """Opaque mask + translucent fill so arrows behind the box stay hidden."""
        fill, stroke = palette
        self.rounded(box, MASK, None, width=0)
        self.rounded(box, fill, stroke, width=1.5)
        title_font = font_mono() if mono_title else font_name()
        if subtitle:
            self.text(box.cx, box.y + 24, title, WHITE, title_font)
            self.text(box.cx, box.y + 44, subtitle, MUTED, font_sub())
        else:
            self.text(box.cx, box.cy, title, WHITE, title_font)

    def region(self, box: Box, label: str) -> None:
        """Amber dashed cluster. Label sits on the top-left edge."""
        self._dashed_rounded(box, AMBER, radius=12, dash=8, gap=4, width=1.5)
        self.text(box.x + 16, box.y + 14, label, AMBER, font_tiny(), anchor="lm")

    def security(self, box: Box, label: str) -> None:
        self._dashed_rounded(box, SECURITY[1], radius=8, dash=4, gap=4, width=1.5)
        self.text(box.x + 14, box.y + 12, label, SECURITY[1], font_tiny(), anchor="lm")

    def bus(self, box: Box, label: str) -> None:
        fill, stroke = BUS
        self.rounded(box, MASK, None, width=0, radius=4)
        self.rounded(box, fill, stroke, width=1, radius=4)
        self.text(box.cx, box.cy, label, stroke, font_mono_small())

    def text(self, x: float, y: float, text: str, fill, font, anchor: str = "mm") -> None:
        self.draw.text((px(x), px(y)), text, fill=fill, font=font, anchor=anchor)

    def header(self, title: str, subtitle: str) -> None:
        # Cyan status dot next to the title (static; PNG cannot pulse).
        r = 6
        self.draw.ellipse([px(40 - r), px(32 - r), px(40 + r), px(32 + r)], fill=(34, 211, 238, 255))
        self.text(56, 32, title, WHITE, font_title(), anchor="lm")
        self.text(56, 58, subtitle, MUTED, font_sub(), anchor="lm")

    def footer(self, text: str) -> None:
        self.text(self.w / 2, self.h - 22, text, MUTED, font_tiny())

    def legend(self, y: float, items: list[tuple[tuple, str]]) -> None:
        """Legend must sit below every region/security boundary."""
        x = 48
        for palette, label in items:
            sw = Box(x, y, 18, 12)
            self.rounded(sw, palette[0], palette[1], width=1.5, radius=3)
            self.text(x + 26, y + 6, label, MUTED, font_tiny(), anchor="lm")
            x += 210

    def arrow_down(self, x: float, y0: float, y1: float) -> None:
        self._arrow((x, y0), (x, y1))

    def arrow_right(self, x0: float, x1: float, y: float) -> None:
        self._arrow((x0, y), (x1, y))

    def arrow_left(self, x0: float, x1: float, y: float) -> None:
        self._arrow((x0, y), (x1, y))

    def _arrow(self, start: tuple[float, float], end: tuple[float, float]) -> None:
        x0, y0 = start
        x1, y1 = end
        self.draw.line([(px(x0), px(y0)), (px(x1), px(y1))], fill=ARROW, width=max(2, px(1.5)))
        angle = math.atan2(y1 - y0, x1 - x0)
        length = 8
        spread = 0.45
        p1 = (x1 - length * math.cos(angle - spread), y1 - length * math.sin(angle - spread))
        p2 = (x1 - length * math.cos(angle + spread), y1 - length * math.sin(angle + spread))
        self.draw.polygon(
            [(px(x1), px(y1)), (px(p1[0]), px(p1[1])), (px(p2[0]), px(p2[1]))],
            fill=ARROW,
        )

    def _dashed_rounded(self, box: Box, color, radius: float, dash: float, gap: float, width: float) -> None:
        """Approximate a dashed rounded-rect by walking the perimeter.

        Pillow has no dashed stroke; short segments are enough at 2x.
        """
        pts = _rounded_rect_points(box, radius, step=2.0)
        draw_on = True
        traveled = 0.0
        for i in range(len(pts) - 1):
            x0, y0 = pts[i]
            x1, y1 = pts[i + 1]
            seg = ((x1 - x0) ** 2 + (y1 - y0) ** 2) ** 0.5
            if draw_on:
                self.draw.line([(px(x0), px(y0)), (px(x1), px(y1))], fill=color, width=max(1, px(width)))
            traveled += seg
            if traveled >= (dash if draw_on else gap):
                traveled = 0.0
                draw_on = not draw_on

    def save(self, name: str) -> Path:
        path = OUT_DIR / name
        # Flatten onto the same dark background so GitHub does not composite
        # a checkerboard behind the translucent fills.
        rgb = Image.new("RGB", self.im.size, BG[:3])
        rgb.paste(self.im, mask=self.im.split()[3])
        rgb.save(path, "PNG", optimize=True)
        return path


def _rounded_rect_points(box: Box, radius: float, step: float) -> list[tuple[float, float]]:
    import math

    r = min(radius, box.w / 2, box.h / 2)
    x0, y0, x1, y1 = box.x, box.y, box.right, box.bottom
    pts: list[tuple[float, float]] = []

    def arc(cx: float, cy: float, a0: float, a1: float) -> None:
        n = max(4, int(abs(a1 - a0) * r / step))
        for i in range(n + 1):
            a = a0 + (a1 - a0) * i / n
            pts.append((cx + r * math.cos(a), cy + r * math.sin(a)))

    pts.append((x0 + r, y0))
    pts.append((x1 - r, y0))
    arc(x1 - r, y0 + r, -math.pi / 2, 0)
    pts.append((x1, y1 - r))
    arc(x1 - r, y1 - r, 0, math.pi / 2)
    pts.append((x0 + r, y1))
    arc(x0 + r, y1 - r, math.pi / 2, math.pi)
    pts.append((x0, y0 + r))
    arc(x0 + r, y0 + r, math.pi, 3 * math.pi / 2)
    pts.append((x0 + r, y0))
    return pts


def _row(x: float, y: float, w: float, h: float, n: int, gap: float) -> list[Box]:
    """n equal boxes in a row. Total width is n*w + (n-1)*gap."""
    return [Box(x + i * (w + gap), y, w, h) for i in range(n)]


def render_software() -> Path:
    """Control-plane stack: clients → desk_core → driver → mxtark → controller."""
    c = Canvas(1200, 1080)
    c.header(
        "Desk Gateway · Software Architecture",
        "All control paths share desk_core. Vendor I²C stays behind desk_driver.",
    )

    clients_region = Box(36, 84, 1128, 200)
    c.region(clients_region, "CLIENTS")
    top = _row(56, 112, 258, 70, 4, 18)
    bot = _row(56, 196, 258, 70, 4, 18)
    labels = [
        (top[0], FRONTEND, "Web UI", "局域网控制台"),
        (top[1], FRONTEND, "Mobile App", "iPhone · Android"),
        (top[2], FRONTEND, "Apple Watch", "Crown · BLE"),
        (top[3], FRONTEND, "REST / scripts", "desk-preset.sh"),
        (bot[0], FRONTEND, "UART console", "up / down / stop"),
        (bot[1], FRONTEND, "BLE accessories", "LightBlue · 旋钮"),
        (bot[2], FRONTEND, "Voice", "小智 · GoatRemote"),
        (bot[3], FRONTEND, "Ulanzi D200H", "Stream Deck 插件"),
    ]
    for box, pal, title, sub in labels:
        c.component(box, pal, title, sub)

    bus = Box(200, 304, 800, 26)
    c.bus(bus, "REST  ·  BLE GATT  ·  UART  ·  SoftAP / LAN")
    c.arrow_down(600, clients_region.bottom, bus.y)
    c.arrow_down(600, bus.bottom, 348)

    fw = Box(36, 348, 1128, 430)
    c.region(fw, "ESP32-S3 FIRMWARE")

    sec = Box(56, 378, 1088, 148)
    c.security(sec, "safety plane")
    core = Box(80, 408, 340, 92)
    c.component(core, BACKEND, "desk_core", "统一命令 · 运动超时", mono_title=True)
    chips = _row(448, 418, 162, 72, 4, 14)
    chip_labels = [
        ("STOP", "任意来源始终有效"),
        ("child-lock", "全局童锁"),
        ("source ACL", "REST / BLE / Panel"),
        ("ToF", "高度 · 右侧障碍"),
    ]
    for box, (title, sub) in zip(chips, chip_labels):
        c.component(box, SECURITY, title, sub)

    driver = Box(360, 554, 480, 56)
    c.component(driver, BACKEND, "desk_driver API", "")
    c.arrow_down(600, sec.bottom, driver.y)

    # Live path stays on the center spine; vendor stubs sit on the sides.
    drivers = _row(120, 638, 280, 76, 3, 50)
    c.component(drivers[0], EXTERNAL, "loctek*", "stub · not shipped", mono_title=True)
    c.component(drivers[1], BACKEND, "mxtark", "I²C Slave @0x24", mono_title=True)
    c.component(drivers[2], EXTERNAL, "jiecang*", "stub · not shipped", mono_title=True)
    c.arrow_down(600, driver.bottom, drivers[1].y)

    hw = Box(36, 798, 1128, 96)
    c.region(hw, "DESK HARDWARE")
    ctrl = Box(360, 822, 480, 56)
    c.component(ctrl, EXTERNAL, "Desk controller", "I²C Master  ·  GPIO4/5")
    c.arrow_down(600, drivers[1].bottom, ctrl.y)

    c.legend(
        916,
        [
            (FRONTEND, "Frontend / client"),
            (BACKEND, "Firmware / driver"),
            (SECURITY, "Safety / ACL"),
            (EXTERNAL, "External hardware"),
            (BUS, "Transport"),
        ],
    )
    c.footer("Desk Gateway  ·  ESP-IDF v6.0.2  ·  Phase 1 complete, Phase 2 panel proxy accepted")
    return c.save("software-architecture.png")


def render_hardware() -> Path:
    """Phase 2 box: original panel, ESP32-S3 gateway, controller, ToF, OLED."""
    c = Canvas(1200, 1020)
    c.header(
        "Desk Gateway · Hardware Topology",
        "Active man-in-the-middle: original panel and desk controller stay isolated buses.",
    )

    wireless = Box(36, 84, 1128, 120)
    c.region(wireless, "WIRELESS CLIENTS")
    clients = _row(80, 112, 240, 70, 4, 24)
    for box, title, sub in zip(
        clients,
        ["Web UI", "iPhone / Android", "Apple Watch", "REST / UART"],
        ["SoftAP / LAN", "BLE · Wi-Fi fallback", "BLE Central", "USB-C serial"],
    ):
        c.component(box, FRONTEND, title, sub)

    bus = Box(300, 226, 600, 26)
    c.bus(bus, "Wi-Fi  ·  BLE  ·  LAN")
    c.arrow_down(600, wireless.bottom, bus.y)
    c.arrow_down(600, bus.bottom, 280)

    c.region(Box(360, 268, 480, 384), "ESP32-S3 GATEWAY")
    c.component(Box(400, 300, 400, 64), BACKEND, "desk_core", "仲裁 · 童锁 · 断线 STOP")
    slave = Box(400, 380, 400, 64)
    c.component(slave, BACKEND, "I²C Slave @0x24", "GPIO4 CLK · GPIO5 DAT", mono_title=True)
    proxy = Box(400, 460, 400, 64)
    c.component(proxy, BACKEND, "Panel proxy", "GPIO6 CLK · GPIO7 DAT")
    c.component(Box(400, 540, 400, 72), DATABASE, "Peripheral I²C1", "GPIO10 SCL · GPIO11 SDA")

    panel = Box(40, 430, 280, 120)
    c.component(panel, EXTERNAL, "Original panel", "RJ45 PANEL · TM1650")
    ctrl = Box(880, 350, 280, 120)
    c.component(ctrl, EXTERNAL, "Desk controller", "RJ45 DESK · I²C Master")
    # Panel talks to the software proxy; controller talks to the hardware slave.
    c.arrow_right(panel.right, 360, proxy.cy)
    c.arrow_right(840, ctrl.x, slave.cy)
    c.text(180, 412, "GPIO6/7  9.6 kHz proxy", MUTED, font_tiny())
    c.text(1020, 332, "GPIO4/5  hardware slave", MUTED, font_tiny())

    usb = Box(400, 672, 400, 44)
    c.component(usb, EXTERNAL, "USB-C  ·  独立供电 / 烧录 / UART", "")

    sensors_region = Box(36, 740, 1128, 120)
    c.region(sensors_region, "SENSORS / DISPLAY  ·  shared I2C1")
    sensors = _row(80, 768, 320, 70, 3, 40)
    c.component(sensors[0], DATABASE, "TOF400C", "产品高度源")
    c.component(sensors[1], DATABASE, "TOF050C", "右侧障碍 · 上升保护")
    c.component(sensors[2], DATABASE, "SSD1306 OLED", "高度 / 状态轮播")
    c.arrow_down(600, 716, sensors_region.y)

    c.legend(
        884,
        [
            (FRONTEND, "Wireless clients"),
            (BACKEND, "ESP32-S3 firmware"),
            (DATABASE, "ToF / OLED bus"),
            (EXTERNAL, "Panel / controller / USB"),
            (BUS, "Radio / LAN"),
        ],
    )
    c.footer("Do not jumper panel CLK/DAT onto the controller bus  ·  do not tie RJ45 3.3V to ESP32 3V3")
    return c.save("hardware-topology.png")


def main() -> None:
    software = render_software()
    hardware = render_hardware()
    print(f"wrote {software}")
    print(f"wrote {hardware}")


if __name__ == "__main__":
    main()
