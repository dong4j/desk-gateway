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
AMP = ((88, 28, 135, 120), (192, 132, 252, 255))  # purple MAX98357A module
BOARD = ((15, 23, 42, 255), (71, 85, 105, 255))

# Fritzing-style wire colors, shifted for the dark canvas.
WIRE_5V = (239, 68, 68, 255)       # red
WIRE_GND = (148, 163, 184, 255)    # slate; true black disappears on #020617
WIRE_BCLK = (156, 163, 175, 255)   # grey
WIRE_LRC = (168, 85, 247, 255)     # purple
WIRE_DIN = (226, 232, 240, 255)    # white
WIRE_SPK = (248, 113, 113, 255)    # speaker pair
WIRE_CLK = (34, 211, 238, 255)     # cyan CLK
WIRE_DAT = (251, 191, 36, 255)     # amber DAT
WIRE_3V3 = (167, 139, 250, 255)    # violet ESP 3V3, not desk RJ45 red
WIRE_SHUT050 = (251, 146, 60, 255) # orange TOF050C XSHUT
WIRE_SHUT400 = (52, 211, 153, 255) # emerald TOF400C XSHUT
LED_RED = (239, 68, 68, 255)
LED_YELLOW = (250, 204, 21, 255)
LED_BLUE = (59, 130, 246, 255)
PIN_GOLD = (251, 191, 36, 255)
NC = (71, 85, 105, 255)

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

    def arrow_down(self, x: float, y0: float, y1: float, color=ARROW) -> None:
        self._arrow((x, y0), (x, y1), color)

    def arrow_right(self, x0: float, x1: float, y: float, color=ARROW) -> None:
        self._arrow((x0, y), (x1, y), color)

    def arrow_left(self, x0: float, x1: float, y: float, color=ARROW) -> None:
        self._arrow((x0, y), (x1, y), color)

    def arrow_up(self, x: float, y0: float, y1: float, color=ARROW) -> None:
        self._arrow((x, y0), (x, y1), color)

    def _arrow(self, start: tuple[float, float], end: tuple[float, float], color=ARROW) -> None:
        x0, y0 = start
        x1, y1 = end
        self.draw.line([(px(x0), px(y0)), (px(x1), px(y1))], fill=color, width=max(2, px(1.5)))
        angle = math.atan2(y1 - y0, x1 - x0)
        length = 8
        spread = 0.45
        p1 = (x1 - length * math.cos(angle - spread), y1 - length * math.sin(angle - spread))
        p2 = (x1 - length * math.cos(angle + spread), y1 - length * math.sin(angle + spread))
        self.draw.polygon(
            [(px(x1), px(y1)), (px(p1[0]), px(p1[1])), (px(p2[0]), px(p2[1]))],
            fill=color,
        )

    def circle(self, x: float, y: float, r: float, fill, outline=None, width: float = 1.5) -> None:
        xy = [px(x - r), px(y - r), px(x + r), px(y + r)]
        self.draw.ellipse(xy, fill=fill, outline=outline, width=max(1, px(width)) if outline else 0)

    def pin(self, x: float, y: float, fill=PIN_GOLD, r: float = 5) -> None:
        """Header pin: gold pad with a dark via so wires have a clear endpoint."""
        self.circle(x, y, r, fill, WHITE, 1.0)
        self.circle(x, y, 1.6, MASK, None)

    def resistor(self, cx: float, cy: float, label: str, *, label_above: bool = False) -> Box:
        """Series current-limit resistor. Label is the value, not a pin name.

        label_above keeps the ohm text off the conductor when a wire tees
        into the body at cy.
        """
        box = Box(cx - 46, cy - 16, 92, 32)
        self.rounded(box, (30, 41, 59, 255), MUTED, width=1.5, radius=4)
        ty = box.y - 8 if label_above else cy
        self.text(cx, ty, label, WHITE, font_mono_small())
        return box

    def rj45_jack(self, box: Box, title: str, subtitle: str, active: bool) -> None:
        """Ethernet jack on the dual-port breakout. Inactive jack stays muted."""
        fill = (15, 23, 42, 255) if active else (15, 23, 42, 255)
        stroke = PIN_GOLD if active else NC
        self.rounded(box, fill, stroke, width=2, radius=8)
        slot = Box(box.right - 34, box.cy - 26, 26, 52)
        self.rounded(slot, MASK, stroke, width=1.5, radius=4)
        self.text(box.x + 12, box.y + 18, title, WHITE if active else NC, font_name(),
                  anchor="lm")
        self.text(box.x + 12, box.y + 38, subtitle, MUTED if active else NC, font_tiny(),
                  anchor="lm")

    def tof_module(self, box: Box, title: str, chip: str, facing: str) -> None:
        """VL53L1X / VL6180X breakout. Facing text is the install direction."""
        self.rounded(box, MASK, None, width=0, radius=10)
        self.rounded(box, DATABASE[0], DATABASE[1], width=2, radius=10)
        self.text(box.cx, box.y + 22, title, WHITE, font_name())
        self.text(box.cx, box.y + 42, chip, MUTED, font_tiny())
        self.text(box.cx, box.y + 58, facing, MUTED, font_tiny())
        lens = Box(box.right - 36, box.cy - 22, 20, 44)
        self.rounded(lens, MASK, DATABASE[1], width=1.5, radius=4)
        self.circle(lens.cx, lens.cy, 6, DATABASE[1], WHITE, 1.0)

    def oled_module(self, box: Box, compact: bool = False) -> None:
        """0.91 inch SSD1306 128x32. Display only; not a control path.

        compact=True drops the fake glass so the overview can put I²C pins
        on the left edge without covering HEIGHT/GAP text.
        """
        self.rounded(box, MASK, None, width=0, radius=10)
        self.rounded(box, FRONTEND[0], FRONTEND[1], width=2, radius=10)
        self.text(box.cx, box.y + 20, "0.91\" OLED", WHITE, font_name())
        if compact:
            self.text(box.cx, box.y + 40, "SSD1306  ·  0x3C / 0x3D", MUTED, font_tiny())
            self.text(box.cx, box.y + 58, "只显示 · 不控桌", MUTED, font_tiny())
            return
        self.text(box.cx, box.y + 38, "SSD1306  ·  128×32  ·  0x3C / 0x3D", MUTED, font_tiny())
        glass = Box(box.x + 48, box.y + 52, box.w - 68, 52)
        self.rounded(glass, (2, 6, 23, 255), FRONTEND[1], width=1.5, radius=4)
        self.text(glass.cx, glass.cy - 8, "HEIGHT  /  GAP", FRONTEND[1], font_mono_small())
        self.text(glass.cx, glass.cy + 10, "只显示  ·  不控桌", MUTED, font_tiny())


    def status_led(self, cx: float, cy: float, color, name: str) -> tuple[float, float]:
        """Discrete LED bead. Returns (anode_x, cathode_x) for wiring."""
        anode_x = cx - 26
        cathode_x = cx + 26
        self.circle(cx, cy, 20, (*color[:3], 90), color, 2)
        self.circle(cx, cy, 12, color, WHITE, 1.2)
        self.draw.line(
            [(px(cathode_x), px(cy - 12)), (px(cathode_x), px(cy + 12))],
            fill=WHITE,
            width=max(2, px(2)),
        )
        self.text(cx, cy + 38, name, MUTED, font_tiny())
        return anode_x, cathode_x

    def wire(self, points: list[tuple[float, float]], color, width: float = 3.5, label: str = "") -> None:
        """Orthogonal (or mixed) polyline. Label sits on the longest segment."""
        xy = [(px(x), px(y)) for x, y in points]
        self.draw.line(xy, fill=color, width=max(2, px(width)), joint="curve")
        if label and len(points) >= 2:
            mid = points[len(points) // 2]
            self.text(mid[0], mid[1] - 12, label, color, font_mono_small())

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


def render_audio_wiring() -> Path:
    """Fritzing-style I2S wiring: YD-ESP32-S3 → MAX98357A → speaker.

    Why this exists:
        The bring-up reference photo is a Raspberry Pi Zero + MAX98357A +
        SPH0645 mic. Desk Gateway only plays local WAV; there is no I2S
        microphone. GPIO14/15/16 and USB 5V are the frozen pin map.

    Layout mirrors the reference: MCU left, amp center, speaker right,
    color-coded wires, GAIN/SD left floating.
    """
    c = Canvas(1400, 900)
    c.header(
        "Desk Gateway · MAX98357A Wiring",
        "YD-ESP32-S3 I2S TX  →  MAX98357A  →  4 Ω / 3 W speaker. Playback only; no SPH0645 mic.",
    )

    # Pin rows shared by ESP right edge and amp left edge so I2S wires stay straight.
    y_lrc, y_bclk, y_din = 292, 336, 380
    y_gain, y_sd = 424, 456
    y_vin, y_gnd = 508, 548


    # --- ESP32-S3 development board ---
    esp = Box(48, 108, 380, 500)
    c.rounded(esp, BOARD[0], BOARD[1], width=2, radius=10)
    c.text(esp.cx, esp.y + 22, "YD-ESP32-S3  N16R8", WHITE, font_name())
    c.text(esp.cx, esp.y + 42, "USB-C 独立供电", MUTED, font_tiny())

    uart_usb = Box(esp.x + 28, esp.y + 58, 140, 28)
    otg_usb = Box(esp.x + 212, esp.y + 58, 140, 28)
    c.rounded(uart_usb, (30, 41, 59, 255), MUTED, width=1.2, radius=6)
    c.rounded(otg_usb, (30, 41, 59, 255), MUTED, width=1.2, radius=6)
    c.text(uart_usb.cx, uart_usb.cy, "USB UART", MUTED, font_tiny())
    c.text(otg_usb.cx, otg_usb.cy, "USB OTG", MUTED, font_tiny())

    module = Box(esp.x + 70, esp.y + 100, 240, 72)
    c.component(module, BACKEND, "ESP32-S3-WROOM-1", "I2S TX · no MCLK")

    # Occupied desk / sensor pins stay on the left so they are not reused for I2S.
    occupied = [
        (400, "GPIO4", "Desk CLK"),
        (424, "GPIO5", "Desk DAT"),
        (448, "GPIO6", "Panel CLK"),
        (472, "GPIO7", "Panel DAT"),
        (496, "GPIO10", "I2C1 SCL"),
        (520, "GPIO11", "I2C1 SDA"),
    ]
    left_x = esp.x + 18
    for y, gpio, use in occupied:
        c.pin(left_x, y, NC, r=4)
        c.text(left_x + 12, y, f"{gpio}  {use}", NC, font_mono_small(), anchor="lm")

    right_x = esp.right - 16
    esp_pins = {
        "lrc": (right_x, y_lrc, "GPIO15", "LRC / WS", WIRE_LRC),
        "bclk": (right_x, y_bclk, "GPIO14", "BCLK", WIRE_BCLK),
        "din": (right_x, y_din, "GPIO16", "DIN", WIRE_DIN),
        "5v": (right_x, y_vin, "5V", "USB VBUS", WIRE_5V),
        "gnd": (right_x, y_gnd, "GND", "common ground", WIRE_GND),
    }
    for x, y, gpio, role, color in esp_pins.values():
        c.pin(x, y, color)
        c.text(x - 12, y - 11, gpio, WHITE, font_mono(), anchor="rm")
        c.text(x - 12, y + 11, role, MUTED, font_tiny(), anchor="rm")

    c.text(esp.cx, esp.bottom - 18, "勿用 3V3 给功放供电", SECURITY[1], font_tiny())

    # --- MAX98357A breakout ---
    amp = Box(640, 148, 300, 460)
    c.rounded(amp, AMP[0], AMP[1], width=2, radius=10)
    c.text(amp.cx, amp.y + 20, "MAX98357A", WHITE, font_name())
    c.text(amp.cx, amp.y + 40, "I2S Mono Amp  ·  9 dB default", MUTED, font_tiny())

    term = Box(amp.x + 54, amp.y + 58, 192, 40)
    c.rounded(term, (22, 101, 52, 180), (52, 211, 153, 255), width=1.5, radius=4)
    c.text(term.x + 40, term.cy, "SPK −", WHITE, font_mono_small())
    c.text(term.right - 40, term.cy, "SPK +", WHITE, font_mono_small())

    amp_left = amp.x + 16
    amp_pins = {
        "lrc": (amp_left, y_lrc, "LRC", WIRE_LRC),
        "bclk": (amp_left, y_bclk, "BCLK", WIRE_BCLK),
        "din": (amp_left, y_din, "DIN", WIRE_DIN),
        "gain": (amp_left, y_gain, "GAIN", NC),
        "sd": (amp_left, y_sd, "SD", NC),
        "vin": (amp_left, y_vin, "Vin", WIRE_5V),
        "gnd": (amp_left, y_gnd, "GND", WIRE_GND),
    }
    for key, (x, y, name, color) in amp_pins.items():
        c.pin(x, y, color)
        suffix = "  悬空" if key in {"gain", "sd"} else ""
        c.text(x + 14, y, name + suffix, MUTED if suffix else WHITE, font_mono(), anchor="lm")

    c.text(amp.cx, amp.bottom - 18, "丝印顺序: LRC  BCLK  DIN  GAIN  SD  GND  Vin", MUTED, font_tiny())

    def hop(esp_key: str, amp_key: str, color, label: str) -> None:
        x0, y0 = esp_pins[esp_key][0], esp_pins[esp_key][1]
        x1, y1 = amp_pins[amp_key][0], amp_pins[amp_key][1]
        c.wire([(x0, y0), (x1, y1)], color)
        c.text((x0 + x1) / 2, y0 - 14, label, color, font_mono_small())

    hop("lrc", "lrc", WIRE_LRC, "LRC")
    hop("bclk", "bclk", WIRE_BCLK, "BCLK")
    hop("din", "din", WIRE_DIN, "DIN")
    hop("5v", "vin", WIRE_5V, "5V")
    hop("gnd", "gnd", WIRE_GND, "GND")

    # Speaker sits to the right of the amp, same as the Pi Fritzing photo.
    spk_cx, spk_cy = 1196, 240
    for r, col in ((76, (30, 41, 59, 255)), (56, (15, 23, 42, 255)), (20, (51, 65, 85, 255))):
        c.circle(spk_cx, spk_cy, r, col, MUTED, 1.5)
    c.circle(spk_cx, spk_cy, 8, PIN_GOLD, None)
    c.text(spk_cx, spk_cy + 98, "4 Ω / 3 W", WHITE, font_name())
    c.text(spk_cx, spk_cy + 118, "差分 · 两端不接 GND", MUTED, font_tiny())

    spk_left = (term.right, term.cy - 8)
    spk_right = (term.right, term.cy + 8)
    c.wire(
        [spk_left, (amp.right + 28, spk_left[1]), (amp.right + 28, spk_cy - 24), (spk_cx - 70, spk_cy - 24)],
        WIRE_SPK,
        width=3,
    )
    c.wire(
        [spk_right, (amp.right + 44, spk_right[1]), (amp.right + 44, spk_cy + 24), (spk_cx - 70, spk_cy + 24)],
        WIRE_5V,
        width=3,
    )
    c.text(amp.right + 36, spk_cy - 38, "SPK−", WIRE_SPK, font_mono_small())
    c.text(amp.right + 52, spk_cy + 40, "SPK+", WIRE_5V, font_mono_small())

    notes = Box(48, 632, 1304, 132)
    c.rounded(notes, MASK, GRID, width=1, radius=8)
    c.text(notes.x + 18, notes.y + 22, "接线约束", AMBER, font_sub(), anchor="lm")
    lines = [
        "GAIN / SD 悬空：模块默认 9 dB，声道 (L+R)/2。固件不占用 GPIO17。",
        "VIN 接开发板 USB 侧 5V，禁止接 3V3，禁止用桌子 RJ45 红线供电。",
        "SPK+ / SPK− 是 Class-D 差分输出，任意一端都不得接 GND。",
        "I2S 三根线尽量短，远离 GPIO4/5/6/7 桌控总线和板载天线。参考图中的 SPH0645 麦克风不属于本方案。",
    ]
    for i, line in enumerate(lines):
        c.text(notes.x + 18, notes.y + 48 + i * 20, line, MUTED, font_tiny(), anchor="lm")

    c.legend(
        788,
        [
            ((WIRE_5V, WIRE_5V), "5V / SPK+"),
            ((WIRE_GND, WIRE_GND), "GND"),
            ((WIRE_BCLK, WIRE_BCLK), "BCLK GPIO14"),
            ((WIRE_LRC, WIRE_LRC), "LRC GPIO15"),
            ((WIRE_DIN, WIRE_DIN), "DIN GPIO16"),
        ],
    )
    c.footer("First power-on: 20% Web volume  ·  full idf.py flash (audio partition)  ·  GAIN/SD floating")
    return c.save("max98357a-wiring.png")


def render_status_led_wiring() -> Path:
    """Fritzing-style discrete LED wiring: GPIO1/2/8 → resistor → LED → GND.

    Why this exists:
        Red / yellow / blue beads are at-a-glance status next to OLED.
        Firmware drives GPIO1/2/8; this PNG freezes polarity so bring-up
        does not steal I2S or desk-bus GPIOs. Hardware lighting is unaccepted.

    Constraint:
        Active-high, common-cathode to ESP32 GND. Never put the anode on 5V
        (ESP32-S3 is not 5V-tolerant). Leave GPIO17 for MAX98357A SD and
        GPIO48 for the unused onboard WS2812.
    """
    c = Canvas(1400, 900)
    c.header(
        "Desk Gateway · Status LED Wiring",
        "GPIO1 red · GPIO2 yellow · GPIO8 blue. Common cathode, active-high. Firmware drives; hardware unaccepted.",
    )

    y_red, y_yellow, y_blue, y_gnd = 268, 348, 428, 532

    esp = Box(48, 108, 400, 500)
    c.rounded(esp, BOARD[0], BOARD[1], width=2, radius=10)
    c.text(esp.cx, esp.y + 22, "YD-ESP32-S3  N16R8", WHITE, font_name())
    c.text(esp.cx, esp.y + 42, "USB-C 独立供电", MUTED, font_tiny())

    uart_usb = Box(esp.x + 28, esp.y + 58, 140, 28)
    otg_usb = Box(esp.x + 212, esp.y + 58, 140, 28)
    c.rounded(uart_usb, (30, 41, 59, 255), MUTED, width=1.2, radius=6)
    c.rounded(otg_usb, (30, 41, 59, 255), MUTED, width=1.2, radius=6)
    c.text(uart_usb.cx, uart_usb.cy, "USB UART", MUTED, font_tiny())
    c.text(otg_usb.cx, otg_usb.cy, "USB OTG", MUTED, font_tiny())

    module = Box(esp.x + 70, esp.y + 100, 240, 72)
    c.component(module, BACKEND, "ESP32-S3-WROOM-1", "GPIO out · 3.3 V logic")

    occupied = [
        (300, "GPIO4–7", "Desk / Panel"),
        (324, "GPIO10–13", "ToF I2C / SHUT"),
        (348, "GPIO14–16", "MAX98357A I2S"),
        (372, "GPIO17", "Amp SD 预留"),
        (396, "GPIO48", "板载 WS2812 空闲"),
    ]
    left_x = esp.x + 18
    for y, gpio, use in occupied:
        c.pin(left_x, y, NC, r=4)
        c.text(left_x + 12, y, f"{gpio}  {use}", NC, font_mono_small(), anchor="lm")

    right_x = esp.right - 16
    esp_pins = {
        "red": (right_x, y_red, "GPIO1", "红 · 童锁/故障", LED_RED),
        "yellow": (right_x, y_yellow, "GPIO2", "黄 · SoftAP/未连", LED_YELLOW),
        "blue": (right_x, y_blue, "GPIO8", "蓝 · 升降中", LED_BLUE),
        "gnd": (right_x, y_gnd, "GND", "common cathode", WIRE_GND),
    }
    for x, y, gpio, role, color in esp_pins.values():
        c.pin(x, y, color)
        c.text(x - 12, y - 11, gpio, WHITE, font_mono(), anchor="rm")
        c.text(x - 12, y + 11, role, MUTED, font_tiny(), anchor="rm")

    c.text(esp.cx, esp.bottom - 18, "禁止把灯正极接到 5V", SECURITY[1], font_tiny())

    r_red = c.resistor(720, y_red, "220 Ω")
    r_yellow = c.resistor(720, y_yellow, "220 Ω")
    r_blue = c.resistor(720, y_blue, "82 Ω")
    resistors = {"red": r_red, "yellow": r_yellow, "blue": r_blue}

    led_x = 980
    leds = {
        "red": c.status_led(led_x, y_red, LED_RED, "红灯 童锁 / 故障 / 上升被拦"),
        "yellow": c.status_led(led_x, y_yellow, LED_YELLOW, "黄灯 SoftAP / Wi-Fi 未连"),
        "blue": c.status_led(led_x, y_blue, LED_BLUE, "蓝灯 桌子正在升降"),
    }

    gnd_rail_x = 1188
    c.wire([(gnd_rail_x, y_red), (gnd_rail_x, y_gnd)], WIRE_GND, width=3)
    c.pin(gnd_rail_x, y_gnd, WIRE_GND)
    c.text(gnd_rail_x + 14, y_gnd, "GND", WHITE, font_mono(), anchor="lm")

    for key, color in (("red", LED_RED), ("yellow", LED_YELLOW), ("blue", LED_BLUE)):
        gpio_x, gpio_y = esp_pins[key][0], esp_pins[key][1]
        rbox = resistors[key]
        anode_x, cathode_x = leds[key]
        c.wire([(gpio_x, gpio_y), (rbox.x, gpio_y)], color)
        c.wire([(rbox.right, gpio_y), (anode_x, gpio_y)], color)
        c.wire([(cathode_x, gpio_y), (gnd_rail_x, gpio_y)], WIRE_GND, width=2.5)

    c.wire(
        [(esp_pins["gnd"][0], y_gnd), (gnd_rail_x, y_gnd)],
        WIRE_GND,
        width=3,
    )

    notes = Box(48, 632, 1304, 132)
    c.rounded(notes, MASK, GRID, width=1, radius=8)
    c.text(notes.x + 18, notes.y + 22, "接线约束", AMBER, font_sub(), anchor="lm")
    lines = [
        "共阴、有源高电平：GPIO → 限流电阻 → LED 正极，负极接 ESP32 GND。长脚是正极。",
        "只用开发板 3.3V 逻辑脚驱动；禁止把灯正极接到 5V，禁止用桌子 RJ45 红线。",
        "默认固件驱动 GPIO1/2/8。初始化失败只让灯不可用，不阻断控桌。GPIO48 板载 RGB 仍空闲。",
        "飞线远离 GPIO4–7 桌控总线和 GPIO14–16 I2S。GPIO17 仍留给 MAX98357A SD。",
    ]
    for i, line in enumerate(lines):
        c.text(notes.x + 18, notes.y + 48 + i * 20, line, MUTED, font_tiny(), anchor="lm")

    c.legend(
        788,
        [
            ((LED_RED, LED_RED), "GPIO1 红 220 Ω"),
            ((LED_YELLOW, LED_YELLOW), "GPIO2 黄 220 Ω"),
            ((LED_BLUE, LED_BLUE), "GPIO8 蓝 82 Ω"),
            ((WIRE_GND, WIRE_GND), "GND 共阴"),
            ((NC, NC), "已占用 / 预留"),
        ],
    )
    c.footer("Firmware drives GPIO1/2/8  ·  hardware lighting unaccepted  ·  GPIO17 amp SD  ·  GPIO48 unused")
    return c.save("status-leds-wiring.png")


def render_dual_rj45_left_wiring() -> Path:
    """Fritzing-style left RJ45: ESP32-S3 GPIO4/5 plus two 2 kΩ pull-ups.

    Why this exists:
        Phase 2 uses a dual-RJ45 breakout. This first drawing freezes only the
        controller-side (left) jack. The two 2 kΩ parts are pull-ups to the
        desk 3.3V rail, not series resistors, and that rail must not feed
        ESP32 3V3. Right-jack GPIO6/7 is the next drawing.
    """
    c = Canvas(1400, 900)
    c.header(
        "Desk Gateway · Dual RJ45 Left Jack",
        "YD-ESP32-S3  →  左口控制盒 GPIO4/5。两只 2 kΩ 上拉到桌子 3.3V，不接到 ESP32 3V3。",
    )

    # Extra pin pitch so the 2 kΩ bodies sit beside CLK/DAT, not on them.
    y_3v3, y_clk, y_gnd, y_dat = 240, 340, 420, 500

    def hwire(x0: float, x1: float, y: float, color, width: float = 3.5,
              hops: tuple[float, ...] = ()) -> None:
        """Horizontal run. hops are x positions to arc over (not a junction)."""
        radius = 11
        lo, hi = (x0, x1) if x0 <= x1 else (x1, x0)
        xs = [h for h in hops if lo + radius < h < hi - radius]
        cursor = lo
        for hop_x in xs:
            c.wire([(cursor, y), (hop_x - radius, y)], color, width=width)
            # PIL arcs are clockwise from 3 o'clock; 180→360 is the upper hop.
            c.draw.arc(
                [px(hop_x - radius), px(y - radius), px(hop_x + radius), px(y + radius)],
                start=180,
                end=360,
                fill=color,
                width=max(2, px(width)),
            )
            cursor = hop_x + radius
        c.wire([(cursor, y), (hi, y)], color, width=width)

    esp = Box(48, 108, 400, 500)
    c.rounded(esp, BOARD[0], BOARD[1], width=2, radius=10)
    c.text(esp.cx, esp.y + 22, "YD-ESP32-S3  N16R8", WHITE, font_name())
    c.text(esp.cx, esp.y + 42, "USB-C 独立供电", MUTED, font_tiny())

    uart_usb = Box(esp.x + 28, esp.y + 58, 140, 28)
    otg_usb = Box(esp.x + 212, esp.y + 58, 140, 28)
    c.rounded(uart_usb, (30, 41, 59, 255), MUTED, width=1.2, radius=6)
    c.rounded(otg_usb, (30, 41, 59, 255), MUTED, width=1.2, radius=6)
    c.text(uart_usb.cx, uart_usb.cy, "USB UART", MUTED, font_tiny())
    c.text(otg_usb.cx, otg_usb.cy, "USB OTG", MUTED, font_tiny())

    module = Box(esp.x + 70, esp.y + 100, 240, 72)
    c.component(module, BACKEND, "ESP32-S3-WROOM-1", "I²C Slave @0x24")

    occupied = [
        (300, "GPIO6/7", "右口 GPIO6/7"),
        (324, "GPIO10–13", "ToF / OLED"),
        (348, "GPIO14–16", "MAX98357A"),
        (372, "GPIO1/2/8", "状态灯"),
        (396, "3V3", "禁止接桌子红线"),
    ]
    left_x = esp.x + 18
    for y, gpio, use in occupied:
        c.pin(left_x, y, NC, r=4)
        c.text(left_x + 12, y, f"{gpio}  {use}", NC, font_mono_small(), anchor="lm")

    right_x = esp.right - 16
    esp_pins = {
        "clk": (right_x, y_clk, "GPIO4", "左口 CLK", WIRE_CLK),
        "gnd": (right_x, y_gnd, "GND", "common ground", WIRE_GND),
        "dat": (right_x, y_dat, "GPIO5", "左口 DAT", WIRE_DAT),
    }
    for x, y, gpio, role, color in esp_pins.values():
        c.pin(x, y, color)
        c.text(x - 12, y - 11, gpio, WHITE, font_mono(), anchor="rm")
        c.text(x - 12, y + 11, role, MUTED, font_tiny(), anchor="rm")

    c.text(esp.cx, esp.bottom - 18, "勿将 RJ45 红线接到 3V3", SECURITY[1], font_tiny())

    pcb = Box(508, 118, 844, 492)
    c.rounded(pcb, (12, 18, 34, 255), BOARD[1], width=2, radius=12)
    c.text(pcb.x + 20, pcb.y + 20, "双口 RJ45 转接板", WHITE, font_name(), anchor="lm")
    c.text(pcb.x + 20, pcb.y + 40, "左右插座彼此独立  ·  本图只接左口", MUTED, font_tiny(),
           anchor="lm")

    left_jack = Box(1000, 196, 156, 328)
    right_jack = Box(1176, 196, 156, 328)
    c.rj45_jack(left_jack, "左口", "控制盒", True)
    c.rj45_jack(right_jack, "右口", "本图不接", False)
    c.text(left_jack.cx, left_jack.bottom + 14, "网线 → 控制盒", WIRE_DAT, font_tiny())
    c.text(right_jack.cx, right_jack.bottom + 14, "右口未接", NC, font_tiny())

    pad_x = 548
    jack_pin_x = left_jack.x + 18
    rail_x = 880
    tee_x = 780
    pads = {
        "3v3": (pad_x, y_3v3, "pin1 红  3.3V", WIRE_5V),
        "clk": (pad_x, y_clk, "pin2 白  CLK", WIRE_CLK),
        "gnd": (pad_x, y_gnd, "pin3 绿  GND", WIRE_GND),
        "dat": (pad_x, y_dat, "pin4 黑  DAT", WIRE_DAT),
    }
    for x, y, name, color in pads.values():
        c.pin(x, y, color)
        c.text(x + 14, y - 12, name, WHITE, font_mono_small(), anchor="lm")

    for y, color in ((y_3v3, WIRE_5V), (y_clk, WIRE_CLK), (y_gnd, WIRE_GND), (y_dat, WIRE_DAT)):
        c.pin(jack_pin_x, y, color, r=4)
        c.pin(right_jack.x + 18, y, NC, r=4)

    # ESP only drives CLK / DAT / GND. Desk 3.3V never enters the MCU.
    c.wire([(esp_pins["clk"][0], y_clk), (pad_x, y_clk)], WIRE_CLK)
    c.wire([(esp_pins["gnd"][0], y_gnd), (pad_x, y_gnd)], WIRE_GND)
    c.wire([(esp_pins["dat"][0], y_dat), (pad_x, y_dat)], WIRE_DAT)

    hwire(pad_x, jack_pin_x, y_clk, WIRE_CLK, hops=(rail_x,))
    hwire(pad_x, jack_pin_x, y_gnd, WIRE_GND, hops=(rail_x,))
    hwire(pad_x, jack_pin_x, y_dat, WIRE_DAT, hops=(rail_x,))
    c.wire([(pad_x, y_3v3), (rail_x, y_3v3), (jack_pin_x, y_3v3)], WIRE_5V)

    r_clk = c.resistor(tee_x, y_clk - 50, "2 kΩ")
    r_dat = c.resistor(tee_x, y_dat + 50, "2 kΩ")
    c.wire([(rail_x, y_3v3), (rail_x, r_dat.cy)], WIRE_5V, width=3)
    c.text(tee_x + 56, y_3v3 - 16, "桌子 3.3V 上拉轨", WIRE_5V, font_tiny(), anchor="lm")

    c.wire([(r_clk.right, r_clk.cy), (rail_x, r_clk.cy), (rail_x, y_3v3)], WIRE_5V, width=2)
    c.wire([(tee_x, r_clk.bottom), (tee_x, y_clk)], WIRE_5V, width=2)
    c.wire([(r_dat.right, r_dat.cy), (rail_x, r_dat.cy)], WIRE_5V, width=2)
    c.wire([(tee_x, y_dat), (tee_x, r_dat.y)], WIRE_5V, width=2)

    c.pin(tee_x, y_clk, WIRE_CLK, r=4)
    c.pin(tee_x, y_dat, WIRE_DAT, r=4)
    c.pin(rail_x, y_3v3, WIRE_5V, r=4)
    c.pin(rail_x, r_clk.cy, WIRE_5V, r=4)
    c.pin(rail_x, r_dat.cy, WIRE_5V, r=4)
    c.text(r_clk.x - 8, r_clk.cy, "上拉 CLK", MUTED, font_tiny(), anchor="rm")
    c.text(r_dat.x - 8, r_dat.cy, "上拉 DAT", MUTED, font_tiny(), anchor="rm")

    c.text(pcb.x + 20, pcb.bottom - 18, "电阻并联在信号上，白/黑线仍直接进 GPIO4/5", MUTED,
           font_tiny(), anchor="lm")

    notes = Box(48, 632, 1304, 132)
    c.rounded(notes, MASK, GRID, width=1, radius=8)
    c.text(notes.x + 18, notes.y + 22, "接线约束", AMBER, font_sub(), anchor="lm")
    lines = [
        "左口 = 控制盒。GPIO4 CLK、GPIO5 DAT、GND 共地。硬件 I²C Slave @0x24。",
        "两只 2 kΩ（可用 2.2 kΩ）是上拉，不是串联：白线和黑线仍直接接到 GPIO。",
        "上拉电源只用来自控制盒的 RJ45 红线 3.3V。禁止接到 ESP32 3V3，避免两路电源反灌。",
        "右口本图不接。不要把左 pin 2/4 跳到右 pin 2/4。ESP32 用 USB-C 独立供电。",
    ]
    for i, line in enumerate(lines):
        c.text(notes.x + 18, notes.y + 48 + i * 20, line, MUTED, font_tiny(), anchor="lm")

    c.legend(
        788,
        [
            ((WIRE_5V, WIRE_5V), "桌子 3.3V 上拉"),
            ((WIRE_CLK, WIRE_CLK), "CLK GPIO4"),
            ((WIRE_DAT, WIRE_DAT), "DAT GPIO5"),
            ((WIRE_GND, WIRE_GND), "GND"),
            ((NC, NC), "右口未接"),
        ],
    )
    c.footer("Left jack only  ·  2 kΩ pull-ups to desk 3.3V  ·  never tie red to ESP32 3V3")
    return c.save("dual-rj45-left-wiring.png")


def render_dual_rj45_right_wiring() -> Path:
    """Fritzing-style right RJ45 pass-through: GPIO6/7 plus 3.3V/GND jumpers.

    Why this exists:
        Phase 2 keeps the original panel on the right jack. CLK/DAT must stay
        isolated so the ESP32 can proxy TM1650; only pin1 3.3V and pin3 GND
        jump left↔right. The panel already has ~1.99 kΩ pull-ups, so this
        drawing adds none.
    """
    c = Canvas(1400, 900)
    c.header(
        "Desk Gateway · Dual RJ45 Right Jack",
        "YD-ESP32-S3  →  右口原厂面板 GPIO6/7。只跳 3.3V / GND；CLK / DAT 禁止左右短接。",
    )

    y_3v3, y_clk, y_gnd, y_dat = 240, 340, 420, 500

    def forbid(x: float, y: float) -> None:
        """Rose X: CLK/DAT must not jumper across the two jacks."""
        s = 9
        c.wire([(x - s, y - s), (x + s, y + s)], SECURITY[1], width=2.5)
        c.wire([(x - s, y + s), (x + s, y - s)], SECURITY[1], width=2.5)

    esp = Box(48, 108, 400, 500)
    c.rounded(esp, BOARD[0], BOARD[1], width=2, radius=10)
    c.text(esp.cx, esp.y + 22, "YD-ESP32-S3  N16R8", WHITE, font_name())
    c.text(esp.cx, esp.y + 42, "USB-C 独立供电", MUTED, font_tiny())

    uart_usb = Box(esp.x + 28, esp.y + 58, 140, 28)
    otg_usb = Box(esp.x + 212, esp.y + 58, 140, 28)
    c.rounded(uart_usb, (30, 41, 59, 255), MUTED, width=1.2, radius=6)
    c.rounded(otg_usb, (30, 41, 59, 255), MUTED, width=1.2, radius=6)
    c.text(uart_usb.cx, uart_usb.cy, "USB UART", MUTED, font_tiny())
    c.text(otg_usb.cx, otg_usb.cy, "USB OTG", MUTED, font_tiny())

    module = Box(esp.x + 70, esp.y + 100, 240, 72)
    c.component(module, BACKEND, "ESP32-S3-WROOM-1", "9.6 kHz 开漏 Master")

    occupied = [
        (300, "GPIO4/5", "左口控制盒"),
        (324, "GPIO10–13", "ToF / OLED"),
        (348, "GPIO14–16", "MAX98357A"),
        (372, "GPIO1/2/8", "状态灯"),
        (396, "3V3", "禁止接桌子红线"),
    ]
    left_x = esp.x + 18
    for y, gpio, use in occupied:
        c.pin(left_x, y, NC, r=4)
        c.text(left_x + 12, y, f"{gpio}  {use}", NC, font_mono_small(), anchor="lm")

    right_x = esp.right - 16
    esp_pins = {
        "clk": (right_x, y_clk, "GPIO6", "右口 CLK", WIRE_CLK),
        "gnd": (right_x, y_gnd, "GND", "common ground", WIRE_GND),
        "dat": (right_x, y_dat, "GPIO7", "右口 DAT", WIRE_DAT),
    }
    for x, y, gpio, role, color in esp_pins.values():
        c.pin(x, y, color)
        c.text(x - 12, y - 11, gpio, WHITE, font_mono(), anchor="rm")
        c.text(x - 12, y + 11, role, MUTED, font_tiny(), anchor="rm")

    c.text(esp.cx, esp.bottom - 18, "勿将 RJ45 红线接到 3V3", SECURITY[1], font_tiny())

    pcb = Box(508, 118, 844, 492)
    c.rounded(pcb, (12, 18, 34, 255), BOARD[1], width=2, radius=12)
    c.text(pcb.x + 20, pcb.y + 20, "双口 RJ45 转接板", WHITE, font_name(), anchor="lm")
    c.text(pcb.x + 20, pcb.y + 40, "右口原厂面板 GPIO6/7  ·  左口控制盒共地与 3.3V 跳线", MUTED, font_tiny(),
           anchor="lm")

    left_jack = Box(800, 196, 148, 328)
    right_jack = Box(1088, 196, 148, 328)
    c.rj45_jack(left_jack, "左口", "控制盒", False)
    c.rj45_jack(right_jack, "右口", "原厂面板", True)
    c.text(left_jack.cx, left_jack.bottom + 14, "GPIO4/5 接左口", MUTED, font_tiny())
    c.text(right_jack.cx, right_jack.bottom + 14, "网线 → 原厂面板", WIRE_DAT, font_tiny())

    pad_x = 548
    left_pin_x = left_jack.x + 18
    right_pin_x = right_jack.x + 18
    gap_x = (left_jack.right + right_jack.x) / 2
    # Rise CLK/DAT to the right of the jack so the verticals never share the pin column.
    clk_rise_x = right_jack.right + 20
    dat_rise_x = right_jack.right + 42
    over_clk, under_dat = 172, 584

    pads = {
        "clk": (pad_x, y_clk, "右口 pin2  CLK", WIRE_CLK),
        "gnd": (pad_x, y_gnd, "pin3 绿  GND", WIRE_GND),
        "dat": (pad_x, y_dat, "右口 pin4  DAT", WIRE_DAT),
    }
    for x, y, name, color in pads.values():
        c.pin(x, y, color)
        c.text(x + 14, y - 12, name, WHITE, font_mono_small(), anchor="lm")

    c.pin(left_pin_x, y_3v3, WIRE_5V, r=4)
    c.pin(left_pin_x, y_clk, NC, r=4)
    c.pin(left_pin_x, y_gnd, WIRE_GND, r=4)
    c.pin(left_pin_x, y_dat, NC, r=4)
    c.pin(right_pin_x, y_3v3, WIRE_5V, r=4)
    c.pin(right_pin_x, y_clk, WIRE_CLK, r=4)
    c.pin(right_pin_x, y_gnd, WIRE_GND, r=4)
    c.pin(right_pin_x, y_dat, WIRE_DAT, r=4)
    c.text(left_pin_x + 12, y_3v3 - 12, "pin1 红", MUTED, font_tiny(), anchor="lm")
    c.text(left_pin_x + 12, y_clk - 12, "pin2 不跳", NC, font_tiny(), anchor="lm")
    c.text(left_pin_x + 12, y_dat - 12, "pin4 不跳", NC, font_tiny(), anchor="lm")

    c.wire([(esp_pins["clk"][0], y_clk), (pad_x, y_clk)], WIRE_CLK)
    c.wire([(esp_pins["gnd"][0], y_gnd), (pad_x, y_gnd)], WIRE_GND)
    c.wire([(esp_pins["dat"][0], y_dat), (pad_x, y_dat)], WIRE_DAT)

    # pin2 stays above GND; pin4 stays below. Neither vertical shares right_pin_x
    # with pin1/pin3, which is what made CLK look shorted onto the other pins.
    c.wire(
        [(pad_x, y_clk), (660, y_clk), (660, over_clk), (clk_rise_x, over_clk),
         (clk_rise_x, y_clk), (right_pin_x, y_clk)],
        WIRE_CLK,
    )
    c.wire(
        [(pad_x, y_dat), (704, y_dat), (704, under_dat), (dat_rise_x, under_dat),
         (dat_rise_x, y_dat), (right_pin_x, y_dat)],
        WIRE_DAT,
    )
    c.text(910, over_clk - 12, "CLK 从上方绕开左口", WIRE_CLK, font_tiny())
    c.text(910, under_dat + 12, "DAT 从下方绕开左口", WIRE_DAT, font_tiny())

    c.wire([(pad_x, y_gnd), (left_pin_x, y_gnd), (right_pin_x, y_gnd)], WIRE_GND)
    c.wire([(left_pin_x, y_3v3), (right_pin_x, y_3v3)], WIRE_5V)
    c.text(gap_x, y_3v3 - 14, "pin1 跳线", WIRE_5V, font_tiny())
    c.text(gap_x, y_gnd - 14, "pin3 跳线", WIRE_GND, font_tiny())

    forbid(gap_x, y_clk)
    forbid(gap_x, y_dat)
    c.text(gap_x, y_clk + 18, "禁止跳线", SECURITY[1], font_tiny())
    c.text(gap_x, y_dat + 18, "禁止跳线", SECURITY[1], font_tiny())

    # Endpoints on top of the incoming stubs so pin2/pin4 do not look merged.
    c.pin(right_pin_x, y_3v3, WIRE_5V, r=4)
    c.pin(right_pin_x, y_clk, WIRE_CLK, r=4)
    c.pin(right_pin_x, y_gnd, WIRE_GND, r=4)
    c.pin(right_pin_x, y_dat, WIRE_DAT, r=4)

    c.text(pcb.x + 20, pcb.bottom - 18, "右口不再补上拉；原厂面板 CLK/DAT 到 3.3V 约 1.99 kΩ", MUTED,
           font_tiny(), anchor="lm")

    notes = Box(48, 632, 1304, 132)
    c.rounded(notes, MASK, GRID, width=1, radius=8)
    c.text(notes.x + 18, notes.y + 22, "接线约束", AMBER, font_sub(), anchor="lm")
    lines = [
        "右口 = 原厂面板。GPIO6 CLK、GPIO7 DAT、GND 共地。约 9.6 kHz 开漏软件 Master。",
        "透传只跳 pin1 红 3.3V 和 pin3 绿 GND。左 pin2/4 不得接到右 pin2/4，否则信号绕过 ESP32。",
        "右口不要再焊 2 kΩ：原厂面板板载上拉已测约 1.99 kΩ。GPIO4/5 只接左口控制盒，不要接到右口。",
        "红线跳线只给原厂面板供电，禁止接到 ESP32 3V3。ESP32 用 USB-C 独立供电。",
    ]
    for i, line in enumerate(lines):
        c.text(notes.x + 18, notes.y + 48 + i * 20, line, MUTED, font_tiny(), anchor="lm")

    c.legend(
        788,
        [
            ((WIRE_5V, WIRE_5V), "pin1 3.3V 跳线"),
            ((WIRE_CLK, WIRE_CLK), "CLK GPIO6"),
            ((WIRE_DAT, WIRE_DAT), "DAT GPIO7"),
            ((WIRE_GND, WIRE_GND), "GND 跳线"),
            ((SECURITY[1], SECURITY[1]), "禁止 CLK/DAT 跳线"),
        ],
    )
    c.footer("Right jack pass-through  ·  GPIO6/7  ·  jumper 3.3V/GND only  ·  never tie CLK/DAT across")
    return c.save("dual-rj45-right-wiring.png")


def render_dual_rj45_passthrough_flow() -> Path:
    """Phase 2 data-flow: panel and multi-client commands merge in desk_core.

    Why this exists:
        The left/right wiring PNGs freeze pins. This drawing shows the active
        man-in-the-middle: original-panel keys enter GPIO6/7, multi-client
        commands enter desk_core, and the only outbound desk path is GPIO4/5
        through the left jack to the controller. CLK/DAT must not be jumpered.
    """
    c = Canvas(1400, 960)
    c.header(
        "Desk Gateway · Dual RJ45 Pass-through",
        "原厂面板与多端都进 desk_core；只有 GPIO4/5 左口向控制盒发指令。CLK/DAT 左右不短接。",
    )

    flow_panel = WIRE_DAT
    flow_client = FRONTEND[1]
    flow_out = BACKEND[1]

    def badge(x: float, y: float, n: str, color) -> None:
        c.circle(x, y, 11, color, WHITE, 1.5)
        c.text(x, y, n, WHITE, font_mono())

    clients_region = Box(48, 84, 1304, 108)
    c.region(clients_region, "多端入口  ·  只调 desk_core")
    client_boxes = _row(72, 112, 292, 64, 4, 24)
    for box, title, sub in zip(
        client_boxes,
        ["Web / REST", "iPhone / Android", "Apple Watch", "UART / 脚本"],
        ["局域网", "BLE · Wi-Fi 回退", "BLE Crown", "USB-C 串口"],
    ):
        c.component(box, FRONTEND, title, sub)

    fw = Box(48, 220, 1304, 230)
    c.region(fw, "ESP32-S3  主动中间人")
    core = Box(470, 248, 460, 64)
    c.component(core, BACKEND, "desk_core", "仲裁 · 童锁 · 断线 STOP", mono_title=True)
    slave = Box(90, 348, 300, 72)
    c.component(slave, BACKEND, "I²C Slave @0x24", "GPIO4 CLK · GPIO5 DAT", mono_title=True)
    proxy = Box(1010, 348, 300, 72)
    c.component(proxy, BACKEND, "Panel proxy", "GPIO6 CLK · GPIO7 DAT")

    c.arrow_down(core.cx, clients_region.bottom, core.y, flow_client)
    badge(core.cx + 22, (clients_region.bottom + core.y) / 2, "2", flow_client)
    c.text(core.cx + 42, (clients_region.bottom + core.y) / 2, "多端指令", flow_client, font_tiny(),
           anchor="lm")

    c.arrow_left(proxy.x, core.right, proxy.cy, flow_panel)
    badge((proxy.x + core.right) / 2, proxy.cy - 16, "1", flow_panel)
    c.text((proxy.x + core.right) / 2, proxy.cy + 16, "面板进 desk_core", flow_panel, font_tiny())
    c.arrow_left(core.x, slave.right, slave.cy, flow_out)
    badge((core.x + slave.right) / 2, slave.cy - 16, "3", flow_out)
    c.text((core.x + slave.right) / 2, slave.cy + 16, "唯一出站", flow_out, font_tiny())

    jacks = Box(48, 472, 1304, 148)
    c.region(jacks, "双口 RJ45 转接板  ·  左右插座彼此独立")
    left_jack = Box(80, 508, 340, 88)
    right_jack = Box(980, 508, 340, 88)
    c.component(left_jack, EXTERNAL, "左口", "RJ45 → 原厂控制盒")
    c.component(right_jack, EXTERNAL, "右口", "RJ45 → 原厂控制面板")
    gnd = Box(460, 536, 480, 36)
    c.bus(gnd, "GND 共地  ·  左 pin3 ↔ 右 pin3")
    c.text(700, 588, "CLK / DAT 禁止左右跳线", SECURITY[1], font_tiny())
    c.wire([(left_jack.right + 8, 524), (right_jack.x - 8, 524)], SECURITY[1], width=2)
    c.wire([(700 - 8, 516), (700 + 8, 532)], SECURITY[1], width=2.5)
    c.wire([(700 - 8, 532), (700 + 8, 516)], SECURITY[1], width=2.5)

    c.arrow_down(slave.cx, slave.bottom, left_jack.y, flow_out)
    c.text(slave.cx + 12, (slave.bottom + left_jack.y) / 2, "GPIO4 / GPIO5", flow_out,
           font_tiny(), anchor="lm")
    c.arrow_up(proxy.cx, right_jack.y, proxy.bottom, flow_panel)
    c.text(proxy.cx + 12, (proxy.bottom + right_jack.y) / 2, "GPIO6 / GPIO7", flow_panel,
           font_tiny(), anchor="lm")

    box = Box(80, 648, 340, 80)
    panel = Box(980, 648, 340, 80)
    c.component(box, EXTERNAL, "原厂控制盒", "I²C Master  ·  唯一受令端")
    c.component(panel, EXTERNAL, "原厂控制面板", "TM1650 按键 / 高度显示")
    c.arrow_down(left_jack.cx, left_jack.bottom, box.y, flow_out)
    c.text(left_jack.cx + 14, (left_jack.bottom + box.y) / 2, "RJ45 网线", MUTED, font_tiny(),
           anchor="lm")
    c.arrow_up(right_jack.cx, panel.y, right_jack.bottom, flow_panel)
    c.text(right_jack.cx + 14, (right_jack.bottom + panel.y) / 2, "RJ45 网线", MUTED, font_tiny(),
           anchor="lm")
    badge(left_jack.cx - 18, (left_jack.bottom + box.y) / 2, "3", flow_out)
    badge(right_jack.cx - 18, (right_jack.bottom + panel.y) / 2, "1", flow_panel)

    notes = Box(48, 752, 1304, 116)
    c.rounded(notes, MASK, GRID, width=1, radius=8)
    c.text(notes.x + 18, notes.y + 20, "数据流向", AMBER, font_sub(), anchor="lm")
    lines = [
        "① 原厂面板按键 → 右口 RJ45 → GPIO6/7 软件代理 → desk_core（未锁且 Panel 允许时，面板优先）。",
        "② Web / App / Watch / REST / UART → desk_core。所有入口都受急停、童锁、来源权限和 ToF 上升保护约束。",
        "③ desk_core 仲裁后只从 GPIO4/5 硬件 Slave 经左口 RJ45 发给控制盒。不要把左 pin2/4 跳到右 pin2/4。",
        "左右口共 GND；pin1 3.3V 只给面板供电，禁止接到 ESP32 3V3。右口不再补上拉。档位键 2 / 3 仍未验收。",
    ]
    for i, line in enumerate(lines):
        c.text(notes.x + 18, notes.y + 42 + i * 18, line, MUTED, font_tiny(), anchor="lm")

    c.legend(
        888,
        [
            ((flow_panel, flow_panel), "① 面板 → ESP32"),
            ((flow_client, flow_client), "② 多端 → ESP32"),
            ((flow_out, flow_out), "③ ESP32 → 控制盒"),
            ((WIRE_GND, WIRE_GND), "GND 共地"),
            ((SECURITY[1], SECURITY[1]), "CLK/DAT 不短接"),
        ],
    )
    c.footer("Active MITM  ·  panel GPIO6/7  ·  controller GPIO4/5  ·  shared GND  ·  never jumper CLK/DAT")
    return c.save("dual-rj45-passthrough-flow.png")


def render_dual_tof_wiring() -> Path:
    """Fritzing-style dual ToF only: shared I²C1 plus independent XSHUT.

    Why this exists:
        Both chips power up at 0x29. GPIO12/13 must stay independent so
        bring-up can hold one in reset while the other is addressed. Product
        height is the filtered TOF400C distance. This drawing does not include
        OLED; that screen has its own four-wire figure.
    """
    c = Canvas(1400, 900)
    c.header(
        "Desk Gateway · Dual ToF Wiring",
        "GPIO10/11 共用 I²C1。GPIO12 = TOF050C SHUT，GPIO13 = TOF400C SHUT。INT 不接。",
    )

    y_3v3, y_gnd, y_sda, y_scl = 236, 284, 332, 380
    y_int, y_shut050, y_shut400 = 428, 476, 528

    esp = Box(48, 108, 400, 500)
    c.rounded(esp, BOARD[0], BOARD[1], width=2, radius=10)
    c.text(esp.cx, esp.y + 22, "YD-ESP32-S3  N16R8", WHITE, font_name())
    c.text(esp.cx, esp.y + 42, "USB-C 独立供电", MUTED, font_tiny())

    uart_usb = Box(esp.x + 28, esp.y + 58, 140, 28)
    otg_usb = Box(esp.x + 212, esp.y + 58, 140, 28)
    c.rounded(uart_usb, (30, 41, 59, 255), MUTED, width=1.2, radius=6)
    c.rounded(otg_usb, (30, 41, 59, 255), MUTED, width=1.2, radius=6)
    c.text(uart_usb.cx, uart_usb.cy, "USB UART", MUTED, font_tiny())
    c.text(otg_usb.cx, otg_usb.cy, "USB OTG", MUTED, font_tiny())

    module = Box(esp.x + 70, esp.y + 100, 240, 72)
    c.component(module, BACKEND, "ESP32-S3-WROOM-1", "I²C1 400 kHz · 分时 SHUT")

    occupied = [
        (300, "GPIO4–7", "Desk / Panel"),
        (324, "GPIO14–16", "MAX98357A"),
        (348, "GPIO1/2/8", "状态灯"),
        (372, "GPIO10/11", "OLED 也走这条总线"),
        (396, "RJ45 3.3V", "禁止给 ToF 供电"),
    ]
    left_x = esp.x + 18
    for y, gpio, use in occupied:
        c.pin(left_x, y, NC, r=4)
        c.text(left_x + 12, y, f"{gpio}  {use}", NC, font_mono_small(), anchor="lm")

    right_x = esp.right - 16
    esp_pins = {
        "3v3": (right_x, y_3v3, "3V3", "开发板 3.3V", WIRE_3V3),
        "gnd": (right_x, y_gnd, "GND", "common ground", WIRE_GND),
        "sda": (right_x, y_sda, "GPIO11", "I²C1 SDA", WIRE_DAT),
        "scl": (right_x, y_scl, "GPIO10", "I²C1 SCL", WIRE_CLK),
        "shut050": (right_x, y_shut050, "GPIO12", "TOF050C SHUT", WIRE_SHUT050),
        "shut400": (right_x, y_shut400, "GPIO13", "TOF400C SHUT", WIRE_SHUT400),
    }
    for x, y, gpio, role, color in esp_pins.values():
        c.pin(x, y, color)
        c.text(x - 12, y - 11, gpio, WHITE, font_mono(), anchor="rm")
        c.text(x - 12, y + 11, role, MUTED, font_tiny(), anchor="rm")

    c.text(esp.cx, esp.bottom - 18, "勿用桌子 RJ45 红线供电", SECURITY[1], font_tiny())

    tof050 = Box(560, 140, 300, 355)
    tof400 = Box(960, 140, 300, 420)
    c.tof_module(tof050, "TOF050C", "VL6180X  ·  运行址 0x30", "水平朝右 · 右侧间距")
    c.tof_module(tof400, "TOF400C", "VL53L1X  ·  运行址 0x29", "垂直朝地 · 产品高度")

    p050 = tof050.x + 18
    p400 = tof400.x + 18

    def module_pins(px_left: float, shut_y: float, shut_color) -> None:
        c.pin(px_left, y_3v3, WIRE_3V3)
        c.pin(px_left, y_gnd, WIRE_GND)
        c.pin(px_left, y_sda, WIRE_DAT)
        c.pin(px_left, y_scl, WIRE_CLK)
        c.pin(px_left, y_int, NC, r=4)
        c.pin(px_left, shut_y, shut_color)
        c.text(px_left + 14, y_3v3, "VIN", WHITE, font_mono(), anchor="lm")
        c.text(px_left + 14, y_gnd, "GND", WHITE, font_mono(), anchor="lm")
        c.text(px_left + 14, y_sda, "SDA", WHITE, font_mono(), anchor="lm")
        c.text(px_left + 14, y_scl, "SCL", WHITE, font_mono(), anchor="lm")
        c.text(px_left + 14, y_int, "INT  不接", NC, font_mono_small(), anchor="lm")
        c.text(px_left + 14, shut_y, "SHUT", WHITE, font_mono(), anchor="lm")

    module_pins(p050, y_shut050, WIRE_SHUT050)
    module_pins(p400, y_shut400, WIRE_SHUT400)

    for key, y, color in (
        ("3v3", y_3v3, WIRE_3V3),
        ("gnd", y_gnd, WIRE_GND),
        ("sda", y_sda, WIRE_DAT),
        ("scl", y_scl, WIRE_CLK),
    ):
        c.wire([(esp_pins[key][0], y), (p050, y), (p400, y)], color)
        c.pin(p050, y, color, r=4)
        c.pin(p400, y, color, r=4)

    c.wire([(esp_pins["shut050"][0], y_shut050), (p050, y_shut050)], WIRE_SHUT050)
    c.wire([(esp_pins["shut400"][0], y_shut400), (p400, y_shut400)], WIRE_SHUT400)
    c.pin(p050, y_shut050, WIRE_SHUT050, r=4)
    c.pin(p400, y_shut400, WIRE_SHUT400, r=4)

    c.text((p050 + p400) / 2, y_scl - 14, "I²C1 并联", WIRE_CLK, font_tiny())
    c.text(p050 + 80, y_shut050 - 14, "只接 050", WIRE_SHUT050, font_tiny())
    c.text(p400 + 80, y_shut400 - 14, "只接 400", WIRE_SHUT400, font_tiny())

    notes = Box(48, 632, 1304, 132)
    c.rounded(notes, MASK, GRID, width=1, radius=8)
    c.text(notes.x + 18, notes.y + 22, "接线约束", AMBER, font_sub(), anchor="lm")
    lines = [
        "两颗默认地址都是 0x29。启动时 GPIO12/13 一起拉低，先唤醒 TOF050C 改到 0x30，再唤醒 TOF400C 留在 0x29。",
        "VIN 接开发板 3V3，与 ESP32 共地。禁止用桌子 RJ45 红线。INT 不接，固件轮询。",
        "产品高度是处理后的 TOF400C 距离，不是控制盒数码管。TOF050C 只在低于 800 mm 时做右侧间距保护。",
        "GPIO10/11 这条 I²C1 也给 OLED 用。换模块后断电检查 SCL/SDA 到 3V3 的并联上拉。",
    ]
    for i, line in enumerate(lines):
        c.text(notes.x + 18, notes.y + 48 + i * 20, line, MUTED, font_tiny(), anchor="lm")

    c.legend(
        788,
        [
            ((WIRE_3V3, WIRE_3V3), "ESP 3V3"),
            ((WIRE_CLK, WIRE_CLK), "SCL GPIO10"),
            ((WIRE_DAT, WIRE_DAT), "SDA GPIO11"),
            ((WIRE_SHUT050, WIRE_SHUT050), "GPIO12 TOF050C"),
            ((WIRE_SHUT400, WIRE_SHUT400), "GPIO13 TOF400C"),
        ],
    )
    c.footer("TOF400C = product height  ·  TOF050C = right gap  ·  INT floating  ·  never use desk RJ45 3.3V")
    return c.save("dual-tof-wiring.png")


def render_oled_wiring() -> Path:
    """Fritzing-style OLED-only: four wires on GPIO10/11.

    Why this exists:
        This figure is the screen pinout, not the ToF pinout. OLED shares
        I²C1 electrically, but drawing the sensors here mixed two jobs.
        VCC is ESP 3V3 only; 5V would pull SDA/SCL above the ESP32-S3 rating.
    """
    c = Canvas(1400, 900)
    c.header(
        "Desk Gateway · OLED Wiring",
        "0.91\" SSD1306：GND、VCC→3V3、SCL→GPIO10、SDA→GPIO11。只显示，不控桌。",
    )

    y_gnd, y_vcc, y_scl, y_sda = 268, 332, 396, 460

    esp = Box(48, 108, 400, 500)
    c.rounded(esp, BOARD[0], BOARD[1], width=2, radius=10)
    c.text(esp.cx, esp.y + 22, "YD-ESP32-S3  N16R8", WHITE, font_name())
    c.text(esp.cx, esp.y + 42, "USB-C 独立供电", MUTED, font_tiny())

    uart_usb = Box(esp.x + 28, esp.y + 58, 140, 28)
    otg_usb = Box(esp.x + 212, esp.y + 58, 140, 28)
    c.rounded(uart_usb, (30, 41, 59, 255), MUTED, width=1.2, radius=6)
    c.rounded(otg_usb, (30, 41, 59, 255), MUTED, width=1.2, radius=6)
    c.text(uart_usb.cx, uart_usb.cy, "USB UART", MUTED, font_tiny())
    c.text(otg_usb.cx, otg_usb.cy, "USB OTG", MUTED, font_tiny())

    module = Box(esp.x + 70, esp.y + 100, 240, 72)
    c.component(module, BACKEND, "ESP32-S3-WROOM-1", "I²C1 400 kHz · 只读显示")

    occupied = [
        (300, "GPIO4–7", "Desk / Panel"),
        (324, "GPIO12/13", "ToF SHUT"),
        (348, "GPIO14–16", "MAX98357A"),
        (372, "GPIO1/2/8", "状态灯"),
        (396, "5V", "禁止给 OLED 供电"),
    ]
    left_x = esp.x + 18
    for y, gpio, use in occupied:
        c.pin(left_x, y, NC, r=4)
        c.text(left_x + 12, y, f"{gpio}  {use}", NC, font_mono_small(), anchor="lm")

    right_x = esp.right - 16
    esp_pins = {
        "gnd": (right_x, y_gnd, "GND", "common ground", WIRE_GND),
        "vcc": (right_x, y_vcc, "3V3", "开发板 3.3V", WIRE_3V3),
        "scl": (right_x, y_scl, "GPIO10", "I²C1 SCL", WIRE_CLK),
        "sda": (right_x, y_sda, "GPIO11", "I²C1 SDA", WIRE_DAT),
    }
    for x, y, gpio, role, color in esp_pins.values():
        c.pin(x, y, color)
        c.text(x - 12, y - 11, gpio, WHITE, font_mono(), anchor="rm")
        c.text(x - 12, y + 11, role, MUTED, font_tiny(), anchor="rm")

    c.text(esp.cx, esp.bottom - 18, "禁止把 OLED VCC 接到 5V", SECURITY[1], font_tiny())

    oled = Box(640, 148, 520, 420)
    c.oled_module(oled)
    oled_pin = oled.x + 18
    for y, color, name in (
        (y_gnd, WIRE_GND, "GND"),
        (y_vcc, WIRE_3V3, "VCC"),
        (y_scl, WIRE_CLK, "SCL"),
        (y_sda, WIRE_DAT, "SDA"),
    ):
        c.pin(oled_pin, y, color)
        c.text(oled_pin + 14, y, name, WHITE, font_mono(), anchor="lm")

    for key, y, color in (
        ("gnd", y_gnd, WIRE_GND),
        ("vcc", y_vcc, WIRE_3V3),
        ("scl", y_scl, WIRE_CLK),
        ("sda", y_sda, WIRE_DAT),
    ):
        c.wire([(esp_pins[key][0], y), (oled_pin, y)], color)
        c.pin(oled_pin, y, color, r=4)

    c.text(oled.cx, oled.bottom - 22, "固件已驱动  ·  真机显示未验收", MUTED, font_tiny())

    notes = Box(48, 632, 1304, 132)
    c.rounded(notes, MASK, GRID, width=1, radius=8)
    c.text(notes.x + 18, notes.y + 22, "接线约束", AMBER, font_sub(), anchor="lm")
    lines = [
        "OLED 只有四根线：GND、VCC→开发板 3V3、SCL→GPIO10、SDA→GPIO11。",
        "VCC 禁止接 5V，禁止用桌子 RJ45 红线，否则 SDA/SCL 会被上拉到 5V。",
        "地址自动探测 0x3C 或 0x3D。缺失时只打一条日志，不影响控桌。",
        "只显示高度、侧距和状态，不参与控桌。真机显示和 30 分钟共存尚未验收。",
    ]
    for i, line in enumerate(lines):
        c.text(notes.x + 18, notes.y + 48 + i * 20, line, MUTED, font_tiny(), anchor="lm")

    c.legend(
        788,
        [
            ((WIRE_3V3, WIRE_3V3), "ESP 3V3"),
            ((WIRE_CLK, WIRE_CLK), "SCL GPIO10"),
            ((WIRE_DAT, WIRE_DAT), "SDA GPIO11"),
            ((WIRE_GND, WIRE_GND), "GND"),
            ((NC, NC), "已占用 / 禁止 5V"),
        ],
    )
    c.footer("SSD1306 four-wire  ·  firmware drives  ·  hardware display unaccepted  ·  never power from 5V")
    return c.save("oled-wiring.png")


def render_full_wiring() -> Path:
    """All frozen GPIO flying leads on one sheet.

    Why this exists:
        Per-peripheral PNGs stay the pin-level source of truth. This overview
        shows every occupied GPIO at once so bring-up does not reuse a desk-bus
        or I2S pin. GPIO17 SD and GPIO48 WS2812 stay unwired on purpose.

    Constraint:
        Never run a vertical on a jack pin column (that looked like a short
        across pin2/3/4). Desk 3.3V never feeds ESP 3V3. I²C1 is shared;
        XSHUT lines are not.
    """
    c = Canvas(1960, 1400)
    c.header(
        "Desk Gateway · Full Wiring",
        "YD-ESP32-S3 整机飞线。分图仍以各专项 PNG 为准。USB-C 独立供电；桌子红线不得进 ESP32 3V3。",
    )

    def forbid(x: float, y: float) -> None:
        s = 8
        c.wire([(x - s, y - s), (x + s, y + s)], SECURITY[1], width=2.5)
        c.wire([(x - s, y + s), (x + s, y - s)], SECURITY[1], width=2.5)

    esp = Box(36, 84, 348, 1000)
    c.rounded(esp, BOARD[0], BOARD[1], width=2, radius=10)
    c.text(esp.cx, esp.y + 20, "YD-ESP32-S3  N16R8", WHITE, font_name())
    c.text(esp.cx, esp.y + 40, "USB-C 独立供电", MUTED, font_tiny())

    uart_usb = Box(esp.x + 20, esp.y + 54, 140, 26)
    otg_usb = Box(esp.x + 188, esp.y + 54, 140, 26)
    c.rounded(uart_usb, (30, 41, 59, 255), MUTED, width=1.2, radius=6)
    c.rounded(otg_usb, (30, 41, 59, 255), MUTED, width=1.2, radius=6)
    c.text(uart_usb.cx, uart_usb.cy, "USB UART", MUTED, font_tiny())
    c.text(otg_usb.cx, otg_usb.cy, "USB OTG", MUTED, font_tiny())

    module = Box(esp.x + 54, esp.y + 90, 240, 56)
    c.component(module, BACKEND, "ESP32-S3-WROOM-1", "全部冻结 GPIO")

    reserved = [
        (248, "GPIO17", "Amp SD 预留"),
        (270, "GPIO48", "板载 WS2812 空闲"),
        (292, "GPIO35–37", "PSRAM 禁脚"),
    ]
    left_x = esp.x + 16
    for y, gpio, use in reserved:
        c.pin(left_x, y, NC, r=4)
        c.text(left_x + 12, y, f"{gpio}  {use}", NC, font_mono_small(), anchor="lm")

    # Pin Y values are grouped by bus so each cluster can sit on the same rows.
    y = {
        "led_r": 250,
        "led_y": 294,
        "led_b": 338,
        "gnd_led": 382,
        "pclk": 418,   # GPIO6 flies over the jacks; not a jack pin row
        "dclk": 470,   # GPIO4 = left pin2
        "gnd_desk": 518,
        "ddat": 566,   # GPIO5 = left pin4
        "pdat": 628,   # GPIO7 flies under the jacks
        "v3": 678,
        "gnd_i2c": 714,
        "scl": 750,
        "sda": 786,
        "shut050": 822,
        "shut400": 858,
        "lrc": 910,
        "bclk": 946,
        "din": 982,
        "v5": 1018,
        "gnd_amp": 1054,
    }
    right_x = esp.right - 16
    esp_pins = {
        "led_r": (right_x, y["led_r"], "GPIO1", "红灯", LED_RED),
        "led_y": (right_x, y["led_y"], "GPIO2", "黄灯", LED_YELLOW),
        "led_b": (right_x, y["led_b"], "GPIO8", "蓝灯", LED_BLUE),
        "gnd_led": (right_x, y["gnd_led"], "GND", "灯共地", WIRE_GND),
        "pclk": (right_x, y["pclk"], "GPIO6", "右口 CLK", WIRE_CLK),
        "dclk": (right_x, y["dclk"], "GPIO4", "左口 CLK", WIRE_CLK),
        "gnd_desk": (right_x, y["gnd_desk"], "GND", "桌控共地", WIRE_GND),
        "ddat": (right_x, y["ddat"], "GPIO5", "左口 DAT", WIRE_DAT),
        "pdat": (right_x, y["pdat"], "GPIO7", "右口 DAT", WIRE_DAT),
        "v3": (right_x, y["v3"], "3V3", "外设 3.3V", WIRE_3V3),
        "gnd_i2c": (right_x, y["gnd_i2c"], "GND", "I²C 共地", WIRE_GND),
        "scl": (right_x, y["scl"], "GPIO10", "I²C1 SCL", WIRE_CLK),
        "sda": (right_x, y["sda"], "GPIO11", "I²C1 SDA", WIRE_DAT),
        "shut050": (right_x, y["shut050"], "GPIO12", "TOF050C SHUT", WIRE_SHUT050),
        "shut400": (right_x, y["shut400"], "GPIO13", "TOF400C SHUT", WIRE_SHUT400),
        "lrc": (right_x, y["lrc"], "GPIO15", "I2S LRC", WIRE_LRC),
        "bclk": (right_x, y["bclk"], "GPIO14", "I2S BCLK", WIRE_BCLK),
        "din": (right_x, y["din"], "GPIO16", "I2S DIN", WIRE_DIN),
        "v5": (right_x, y["v5"], "5V", "USB VBUS", WIRE_5V),
        "gnd_amp": (right_x, y["gnd_amp"], "GND", "功放共地", WIRE_GND),
    }
    for x, py, gpio, role, color in esp_pins.values():
        c.pin(x, py, color)
        c.text(x - 10, py - 10, gpio, WHITE, font_mono_small(), anchor="rm")
        c.text(x - 10, py + 10, role, MUTED, font_tiny(), anchor="rm")

    c.text(esp.cx, esp.bottom - 16, "禁止桌子红线 → 3V3", SECURITY[1], font_tiny())

    alley = 418

    def pin_caption(x: float, py: float, name: str, color) -> None:
        """Pin dot with the name *above* the conductor so the wire does not strike it."""
        c.pin(x, py, color)
        c.text(x + 12, py - 12, name, WHITE, font_mono_small(), anchor="lm")

    def jog(x0: float, y0: float, x1: float, y1: float, color, lane: float = alley, width: float = 3.5) -> None:
        """Orthogonal: out from ESP, vertical in the alley, into the destination pin."""
        c.wire([(x0, y0), (lane, y0), (lane, y1), (x1, y1)], color, width)

    def gap_bus(esp_y: float, dest_y: float, color, taps: list, lane: float) -> None:
        """Shared net that only runs in the alley and the gaps *between* modules.

        Why: a straight bus at dest_y went through OLED/ToF titles and VIN/SCL
        captions. Each tap is a short stub to the left-edge pin; the label sits
        inside the module, off the bus. lane is staggered per net so the
        verticals in the alley do not paint on top of each other.
        """
        c.wire([(right_x, esp_y), (lane, esp_y), (lane, dest_y)], color)
        prev = lane
        for pin_x, box, name in taps:
            c.wire([(prev, dest_y), (pin_x, dest_y)], color)
            pin_caption(pin_x, dest_y, name, color)
            c.pin(pin_x, dest_y, color, r=4)
            prev = box.right + 12

    # --- Status LEDs: short runs, ohm text above the body ---
    c.region(Box(404, 228, 480, 158), "状态灯  ·  共阴")
    led_cx = 780
    gnd_rail = 860
    for key, color, name, ohms in (
        ("led_r", LED_RED, "红", "220Ω"),
        ("led_y", LED_YELLOW, "黄", "220Ω"),
        ("led_b", LED_BLUE, "蓝", "82Ω"),
    ):
        py = y[key]
        rbox = c.resistor(520, py, ohms, label_above=True)
        anode, cathode = c.status_led(led_cx, py, color, name)
        c.wire([(right_x, py), (rbox.x, py)], color)
        c.wire([(rbox.right, py), (anode, py)], color)
        c.wire([(cathode, py), (gnd_rail, py)], WIRE_GND, width=2.5)
        c.pin(gnd_rail, py, WIRE_GND, r=4)
    c.wire(
        [(gnd_rail, y["led_r"]), (gnd_rail, y["gnd_led"]), (alley, y["gnd_led"]), (right_x, y["gnd_led"])],
        WIRE_GND,
        width=2.5,
    )
    c.pin(gnd_rail, y["gnd_led"], WIRE_GND, r=4)

    # --- Dual RJ45: left = controller, right = original panel ---
    pcb = Box(404, 404, 1520, 248)
    c.region(pcb, "双口 RJ45  ·  左右独立  ·  CLK/DAT 禁止跳线")
    left_jack = Box(1288, 432, 128, 184)
    right_jack = Box(1548, 432, 128, 184)
    c.rj45_jack(left_jack, "左口", "控制盒", True)
    c.rj45_jack(right_jack, "右口", "原厂面板", True)
    c.text(left_jack.cx, left_jack.bottom + 18, "网线 → 控制盒", WIRE_DAT, font_tiny())
    c.text(right_jack.cx, right_jack.bottom + 18, "网线 → 原厂面板", WIRE_DAT, font_tiny())

    # Jack pin rows. Verticals never sit on left_pin_x / right_pin_x.
    # pin1 sits between the GPIO6 overflight and GPIO4. Do not reuse GPIO6's Y
    # as pin1: that made the overflight look soldered onto the 3.3V row.
    jack_3v3, jack_clk, jack_gnd, jack_dat = 442, y["dclk"], y["gnd_desk"], y["ddat"]
    left_pin = left_jack.x + 16
    right_pin = right_jack.x + 16
    gap_x = (left_jack.right + right_jack.x) / 2
    clk_rise = right_jack.right + 18
    dat_rise = right_jack.right + 40
    pull_x = 1120
    # Fly GPIO6 *above* the region title (title sits at pcb.y+14 = 418).
    over_clk = 396
    under_dat = 642

    for px_j, clk_c, dat_c in (
        (left_pin, WIRE_CLK, WIRE_DAT),
        (right_pin, WIRE_CLK, WIRE_DAT),
    ):
        c.pin(px_j, jack_3v3, WIRE_5V, r=4)
        c.pin(px_j, jack_clk, clk_c, r=4)
        c.pin(px_j, jack_gnd, WIRE_GND, r=4)
        c.pin(px_j, jack_dat, dat_c, r=4)

    pin_caption(left_pin, jack_3v3, "pin1 红", WIRE_5V)
    pin_caption(left_pin, jack_clk, "pin2 CLK", WIRE_CLK)
    pin_caption(left_pin, jack_gnd, "pin3 GND", WIRE_GND)
    pin_caption(left_pin, jack_dat, "pin4 DAT", WIRE_DAT)

    # Left jack: GPIO4/5 plus GND. Desk 3.3V does not enter the ESP.
    # Jog so the long run is not on the pin-caption row of the 2 kΩ bodies.
    jog(right_x, y["dclk"], left_pin, jack_clk, WIRE_CLK)
    jog(right_x, y["ddat"], left_pin, jack_dat, WIRE_DAT)
    c.wire(
        [(right_x, y["gnd_desk"]), (alley, y["gnd_desk"]), (alley, jack_gnd),
         (left_pin, jack_gnd), (right_pin, jack_gnd)],
        WIRE_GND,
    )
    c.wire([(left_pin, jack_3v3), (right_pin, jack_3v3)], WIRE_5V)
    c.text(gap_x, jack_3v3 - 16, "pin1 跳线", WIRE_5V, font_tiny())
    c.text(gap_x, jack_gnd - 16, "pin3 跳线", WIRE_GND, font_tiny())

    # 2 kΩ pull-ups on the left jack only; parallel to CLK/DAT, not series.
    r_clk = c.resistor(pull_x, jack_clk - 40, "2 kΩ", label_above=True)
    r_dat = c.resistor(pull_x, jack_dat + 40, "2 kΩ", label_above=True)
    c.wire([(pull_x, jack_3v3), (left_pin, jack_3v3)], WIRE_5V, width=2.5)
    c.wire([(r_clk.right, r_clk.cy), (pull_x + 70, r_clk.cy), (pull_x + 70, jack_3v3)], WIRE_5V, width=2)
    c.wire([(pull_x, r_clk.bottom), (pull_x, jack_clk)], WIRE_5V, width=2)
    c.wire([(r_dat.right, r_dat.cy), (pull_x + 70, r_dat.cy)], WIRE_5V, width=2)
    c.wire([(pull_x, jack_dat), (pull_x, r_dat.y)], WIRE_5V, width=2)
    c.pin(pull_x, jack_clk, WIRE_CLK, r=4)
    c.pin(pull_x, jack_dat, WIRE_DAT, r=4)
    c.pin(pull_x + 70, jack_3v3, WIRE_5V, r=4)
    c.text(pull_x - 8, jack_3v3 - 16, "桌子 3.3V 上拉", WIRE_5V, font_tiny(), anchor="rm")

    # Right jack CLK/DAT go around the left jack; no vertical on the pin column.
    c.wire(
        [(right_x, y["pclk"]), (alley, y["pclk"]), (alley, over_clk), (clk_rise, over_clk),
         (clk_rise, jack_clk), (right_pin, jack_clk)],
        WIRE_CLK,
    )
    c.wire(
        [(right_x, y["pdat"]), (alley, y["pdat"]), (alley, under_dat), (dat_rise, under_dat),
         (dat_rise, jack_dat), (right_pin, jack_dat)],
        WIRE_DAT,
    )
    c.text(clk_rise - 8, over_clk - 12, "GPIO6 绕开左口", WIRE_CLK, font_tiny(), anchor="rm")
    c.text(dat_rise + 8, under_dat + 12, "GPIO7 绕开左口", WIRE_DAT, font_tiny(), anchor="lm")

    forbid(gap_x, jack_clk)
    forbid(gap_x, jack_dat)
    c.text(gap_x, jack_clk + 16, "禁止跳线", SECURITY[1], font_tiny())
    c.text(gap_x, jack_dat + 16, "禁止跳线", SECURITY[1], font_tiny())
    c.pin(right_pin, jack_clk, WIRE_CLK, r=4)
    c.pin(right_pin, jack_dat, WIRE_DAT, r=4)

    # --- I²C1: OLED + dual ToF share SCL/SDA; SHUT stays 1:1 ---
    # Pin rows sit below the module titles. SHUT runs under OLED / TOF050C.
    i2c_region = Box(404, 656, 1520, 252)
    c.region(i2c_region, "I²C1  ·  GPIO10/11 并联  ·  SHUT 独立")
    i2c_v3, i2c_gnd, i2c_scl, i2c_sda = 748, 776, 804, 832
    i2c_shut050, i2c_shut400 = 860, 888
    oled = Box(500, 670, 260, 176)
    tof050 = Box(880, 670, 280, 210)
    tof400 = Box(1300, 670, 280, 230)
    c.oled_module(oled, compact=True)
    c.tof_module(tof050, "TOF050C", "VL6180X  ·  0x30", "朝右 · 侧距")
    c.tof_module(tof400, "TOF400C", "VL53L1X  ·  0x29", "朝地 · 高度")
    p_oled, p050, p400 = oled.x + 16, tof050.x + 16, tof400.x + 16

    gap_bus(
        y["v3"], i2c_v3, WIRE_3V3,
        [(p_oled, oled, "VIN/VCC"), (p050, tof050, "VIN"), (p400, tof400, "VIN")],
        406,
    )
    gap_bus(
        y["gnd_i2c"], i2c_gnd, WIRE_GND,
        [(p_oled, oled, "GND"), (p050, tof050, "GND"), (p400, tof400, "GND")],
        414,
    )
    gap_bus(
        y["scl"], i2c_scl, WIRE_CLK,
        [(p_oled, oled, "SCL"), (p050, tof050, "SCL"), (p400, tof400, "SCL")],
        422,
    )
    gap_bus(
        y["sda"], i2c_sda, WIRE_DAT,
        [(p_oled, oled, "SDA"), (p050, tof050, "SDA"), (p400, tof400, "SDA")],
        430,
    )
    gap_bus(y["shut050"], i2c_shut050, WIRE_SHUT050, [(p050, tof050, "SHUT")], 438)
    gap_bus(y["shut400"], i2c_shut400, WIRE_SHUT400, [(p400, tof400, "SHUT")], 446)
    c.text(oled.right + 40, i2c_scl - 14, "I²C1 并联 · INT 不接", WIRE_CLK, font_tiny())

    # --- MAX98357A: I2S TX only; GAIN/SD floating; no mic ---
    amp_region = Box(404, 904, 1520, 180)
    c.region(amp_region, "MAX98357A  ·  播放 only  ·  GAIN/SD 悬空")
    amp = Box(500, 908, 300, 164)
    c.rounded(amp, AMP[0], AMP[1], width=2, radius=10)
    # Names live on the region label. Inside the box, only pin captions sit
    # above the stubs so I2S wires do not strike MAX98357A / I2S Mono.
    c.text(amp.cx, amp.bottom - 16, "GAIN / SD 悬空", NC, font_tiny())
    amp_left = amp.x + 16
    amp_names = (("lrc", "LRC"), ("bclk", "BCLK"), ("din", "DIN"), ("v5", "Vin"), ("gnd_amp", "GND"))
    for i, (key, name) in enumerate(amp_names):
        color = esp_pins[key][4]
        jog(right_x, y[key], amp_left, y[key], color, lane=406 + i * 8)
        pin_caption(amp_left, y[key], name, color)
        c.pin(amp_left, y[key], color, r=4)

    spk_cx, spk_cy = 980, 1008
    for r, col in ((48, (30, 41, 59, 255)), (34, (15, 23, 42, 255)), (12, (51, 65, 85, 255))):
        c.circle(spk_cx, spk_cy, r, col, MUTED, 1.5)
    c.text(spk_cx, spk_cy + 64, "4 Ω / 3 W", WHITE, font_tiny())
    c.text(spk_cx, spk_cy + 80, "差分 · 不接 GND", MUTED, font_tiny())
    c.wire(
        [(amp.right, y["bclk"]), (amp.right + 36, y["bclk"]),
         (amp.right + 36, spk_cy - 12), (spk_cx - 48, spk_cy - 12)],
        WIRE_SPK,
        width=3,
    )
    c.wire(
        [(amp.right, y["din"]), (amp.right + 56, y["din"]),
         (amp.right + 56, spk_cy + 12), (spk_cx - 48, spk_cy + 12)],
        WIRE_5V,
        width=3,
    )

    notes = Box(36, 1092, 1888, 168)
    c.rounded(notes, MASK, GRID, width=1, radius=8)
    c.text(notes.x + 18, notes.y + 20, "接线约束", AMBER, font_sub(), anchor="lm")
    lines = [
        "ESP32 只走 USB-C。桌子 RJ45 红线 3.3V 只给左口 2 kΩ 上拉和右口面板供电，禁止接到 ESP32 3V3。GPIO35/36/37 是 PSRAM，禁止外接。",
        "左口 GPIO4/5 = 控制盒，硬件 I²C Slave @0x24，两只 2 kΩ 上拉（不是串联）。右口 GPIO6/7 = 原厂面板，只跳 pin1/pin3。左右 CLK/DAT 不得短接。",
        "GPIO10/11 是 OLED 与双 ToF 的同一条 I²C1。GPIO12 只接 TOF050C SHUT，GPIO13 只接 TOF400C SHUT。INT 不接。OLED VCC 禁止 5V。",
        "状态灯共阴：GPIO1 红 220Ω、GPIO2 黄 220Ω、GPIO8 蓝 82Ω。MAX98357A 走 GPIO14/15/16；GAIN/SD 悬空，GPIO17 预留。GPIO48 板载 RGB 空闲。",
        "针脚细节以分图为准：dual-rj45-left/right、dual-tof、oled、max98357a、status-leds。功放、OLED、状态灯真机均未验收。",
    ]
    for i, line in enumerate(lines):
        c.text(notes.x + 18, notes.y + 44 + i * 22, line, MUTED, font_tiny(), anchor="lm")

    c.legend(
        1280,
        [
            ((WIRE_5V, WIRE_5V), "桌子 3.3V / USB 5V"),
            ((WIRE_3V3, WIRE_3V3), "ESP 3V3"),
            ((WIRE_CLK, WIRE_CLK), "CLK / SCL"),
            ((WIRE_DAT, WIRE_DAT), "DAT / SDA"),
            ((WIRE_GND, WIRE_GND), "GND"),
            ((WIRE_SHUT050, WIRE_SHUT050), "SHUT"),
            ((SECURITY[1], SECURITY[1]), "禁止跳线"),
        ],
    )
    c.footer(
        "Full GPIO map  ·  per-peripheral PNGs remain canonical  ·  GPIO17/48 unwired  ·  never tie desk 3.3V to ESP 3V3"
    )
    return c.save("full-wiring.png")


def render_i2c_hw_limit() -> Path:
    """Why hardware I²C cannot ACK key + digit on the same CLK/DAT pair.

    Why this exists:
        Readers mix up 'five slave addresses' with 'five I²C buses'. The desk
        still has one CLK/DAT pair. TM1650 answers five 7-bit addresses on
        that pair. Each ESP32-S3 hardware slave binds exactly one address, and
        the two controllers cannot share one pin pair to cover all five.
    """
    c = Canvas(1200, 980)
    c.header(
        "Desk Gateway · Hardware I²C Limit",
        "一对 CLK/DAT，五个 Slave 地址。ESP32-S3 每组硬件 Slave 只能绑一个精确地址。",
    )

    def forbid(x: float, y: float) -> None:
        s = 8
        c.wire([(x - s, y - s), (x + s, y + s)], SECURITY[1], width=2.5)
        c.wire([(x - s, y + s), (x + s, y - s)], SECURITY[1], width=2.5)

    master = Box(36, 84, 1128, 88)
    c.region(master, "控制盒  ·  唯一 I²C Master")
    c.component(
        Box(300, 108, 600, 48),
        EXTERNAL,
        "Desk controller",
        "轮询 0x24，并写 0x34–0x37 刷新数码管",
    )

    bus = Box(80, 196, 1040, 36)
    c.bus(bus, "一对 CLK / DAT   ·   GPIO4 SCL  ·  GPIO5 SDA   ·   不是五组线")
    c.arrow_down(600, master.bottom, bus.y)

    addrs = _row(80, 256, 196, 64, 5, 16)
    labels = [
        ("0x24", "键通道  DR"),
        ("0x34", "DIG1  百位"),
        ("0x35", "DIG2  十位"),
        ("0x36", "DIG3  个位"),
        ("0x37", "DIG4  镜像"),
    ]
    for box, (addr, role) in zip(addrs, labels):
        palette = BACKEND if addr == "0x24" else DATABASE
        c.component(box, palette, addr, role, mono_title=True)
        c.arrow_down(box.cx, bus.bottom, box.y, BUS[1])

    tm = Box(36, 348, 560, 280)
    c.region(tm, "原厂 TM1650  ·  一块芯片 ACK 全部五个地址")
    c.component(Box(72, 384, 488, 64), FRONTEND, "TM1650", "显示驱动 + 键扫描  ·  同一对 CLK/DAT")
    chips = _row(72, 468, 88, 56, 5, 8)
    for box, (addr, _) in zip(chips, labels):
        pal = BACKEND if addr == "0x24" else DATABASE
        c.component(box, pal, addr, "ACK", mono_title=True)
    c.text(tm.cx, 548, "物理上仍是一根总线。五个地址是从机译码，不是五套接线。", MUTED, font_tiny())
    c.arrow_down(316, addrs[0].bottom, tm.y)

    esp = Box(612, 348, 552, 280)
    c.region(esp, "ESP32-S3  ·  两组硬件 I²C，每组 Slave 只绑 1 个地址")
    i2c0 = Box(644, 384, 488, 72)
    c.component(i2c0, BACKEND, "I2C0  Slave  @0x24", "GPIO4 / GPIO5  ·  只能占键通道", mono_title=True)
    i2c1 = Box(644, 476, 488, 72)
    c.component(i2c1, EXTERNAL, "I2C1  另一组控制器", "必须换脚。不能并到桌上这一对 CLK/DAT")
    c.security(Box(644, 564, 220, 44), "0x34–0x37")
    c.text(874, 586, "硬件 ACK 不了  ·  同线剩余四个地址", SECURITY[1], font_tiny(), anchor="lm")
    forbid(754, 586)
    c.arrow_down(884, addrs[0].bottom, esp.y)

    notes = Box(36, 656, 1128, 180)
    c.rounded(notes, MASK, GRID, width=1, radius=8)
    c.text(notes.x + 18, notes.y + 22, "读图要点", AMBER, font_sub(), anchor="lm")
    lines = [
        "控制盒是 Master。原厂面板 TM1650 在同一对 CLK/DAT 上应答 0x24 和 0x34–0x37。",
        "ESP32-S3 确有 I2C0 和 I2C1 两组硬件控制器，但一个硬件 Slave 只能配置一个精确从地址。",
        "就算把 I2C1 也配成 Slave，它也要占用另一组 SCL/SDA 脚，盖不住桌上这一对线上剩下的四个 digit 地址。",
        "所以硬件路径最多稳定占住 0x24 回键码。要同时 ACK 五个地址，只能改软件位级模拟，后面证明这条路把升降弄断了。",
    ]
    for i, line in enumerate(lines):
        c.text(notes.x + 18, notes.y + 48 + i * 28, line, MUTED, font_tiny(), anchor="lm")

    c.legend(
        860,
        [
            (EXTERNAL, "控制盒 Master"),
            (FRONTEND, "原厂 TM1650"),
            (BACKEND, "硬件可 ACK 的 0x24"),
            (DATABASE, "digit 0x34–0x37"),
            (SECURITY, "硬件盖不住"),
        ],
    )
    c.footer("Five slave addresses on one CLK/DAT pair  ·  each HW I²C slave binds one address  ·  I2C1 cannot join this pair")
    return c.save("i2c-hw-slave-limit.png")


def render_xiaozhi_mcp_flow() -> Path:
    """Voice path: JC3636W518C → XiaoZhi cloud MCP → Mac bridge → Desk Gateway.

    Why this exists:
        The sspai draft used an ASCII ladder for the voice chain. Touch UI on
        the round display talks REST directly and is not on this path. Keep
        the six hops as one vertical spine so readers do not mix the two.
    """
    c = Canvas(900, 820)
    c.header(
        "小智语音控桌  ·  MCP 链路",
        "触摸页面直连局域网 REST，不在这条链上。语音才走云端 MCP。",
    )

    steps = [
        (FRONTEND, "JC3636W518C", "小智硬件  ·  语音入口", True),
        (((120, 53, 15, 102), (251, 191, 36, 255)), "小智官方云智能体", "xiaozhi.me", False),
        (BUS, "MCP Endpoint", "tools/list  ·  tools/call", True),
        (BACKEND, "desk-mcp 桥接", "Mac  ·  launchd 常驻", True),
        (SECURITY, "Desk Gateway REST", "X-Desk-Key  ·  仅局域网", False),
        (EXTERNAL, "desk_core → 控制盒", "mxtark  ·  I²C Slave @0x24", True),
    ]

    x, w, h = 150, 600, 72
    y = 96
    gap = 36
    boxes: list[Box] = []
    for i, (pal, title, sub, mono) in enumerate(steps):
        box = Box(x, y, w, h)
        boxes.append(box)
        c.component(box, pal, title, sub, mono_title=mono)
        c.circle(x - 36, box.cy, 14, pal[1], WHITE, 1.5)
        c.text(x - 36, box.cy, str(i + 1), WHITE, font_mono())
        y = box.bottom + gap

    for a, b in zip(boxes, boxes[1:]):
        c.arrow_down(a.cx, a.bottom, b.y)

    c.footer("Voice only  ·  five fixed MCP tools  ·  Desk Gateway stays on LAN  ·  no public port mapping")
    return c.save("xiaozhi-mcp-flow.png")


def main() -> None:
    software = render_software()
    hardware = render_hardware()
    i2c_limit = render_i2c_hw_limit()
    full = render_full_wiring()
    audio = render_audio_wiring()
    leds = render_status_led_wiring()
    left_rj45 = render_dual_rj45_left_wiring()
    right_rj45 = render_dual_rj45_right_wiring()
    passthrough = render_dual_rj45_passthrough_flow()
    tof = render_dual_tof_wiring()
    oled = render_oled_wiring()
    xiaozhi = render_xiaozhi_mcp_flow()
    print(f"wrote {software}")
    print(f"wrote {hardware}")
    print(f"wrote {i2c_limit}")
    print(f"wrote {full}")
    print(f"wrote {audio}")
    print(f"wrote {leds}")
    print(f"wrote {left_rj45}")
    print(f"wrote {right_rj45}")
    print(f"wrote {passthrough}")
    print(f"wrote {tof}")
    print(f"wrote {oled}")
    print(f"wrote {xiaozhi}")


if __name__ == "__main__":
    main()
