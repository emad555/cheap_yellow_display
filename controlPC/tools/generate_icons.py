"""
Regenerate icons.h from brand images.
  pip install -r tools/requirements.txt
  python tools/generate_icons.py
Optional: drop PNGs in assets/icons/ (e.g. icon_chrome.png) to override downloads.
"""

from __future__ import annotations

import io
from pathlib import Path

import requests
from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
ASSETS = ROOT / "assets" / "icons"
OUT = ROOT / "icons.h"
ICON_SIZE = 56
# Chroma-key color for transparent pixels (must match ICON_TRANSP in icons.h)
TRANSP_RGB565 = 0xF81F
ALPHA_CUTOFF = 128

# PNG / ICO URLs (full-color where possible)
ICON_URLS = {
    "icon_chrome": (
        "https://raw.githubusercontent.com/alrra/browser-logos/main/src/chrome/chrome_64x64.png"
    ),
    "icon_steam": "https://store.steampowered.com/favicon.ico",
    "icon_fusion360": "https://api.iconify.design/devicon/fusion.svg",
    "icon_eufymake": "https://www.eufymake.com/favicon.ico",
    "icon_cursor": "https://www.cursor.com/favicon.ico",
    "icon_arduino": "https://api.iconify.design/devicon/arduino.svg",
    # Full-color Office product icons (Icons8 color set)
    "icon_word": "https://img.icons8.com/color/96/microsoft-word-2019.png",
    "icon_excel": "https://img.icons8.com/color/96/microsoft-excel-2019.png",
    "icon_powerpoint": "https://img.icons8.com/color/96/microsoft-powerpoint-2019.png",
    "icon_outlook": "https://img.icons8.com/color/96/microsoft-outlook-2019.png",
    "icon_onenote": "https://img.icons8.com/color/96/microsoft-onenote-2019.png",
    "icon_teams": "https://img.icons8.com/color/96/microsoft-teams.png",
}

# SVG slugs on api.iconify.design (fallback)
SVG_FALLBACKS = {
    "icon_fusion360": "devicon/fusion",
    "icon_steam": "mdi/steam",
    "icon_cursor": "vscode-icons/file-type-cursorrules",
    "icon_arduino": "devicon/arduino",
    "icon_word": "simple-icons/microsoftword",
    "icon_excel": "simple-icons/microsoftexcel",
    "icon_powerpoint": "simple-icons/microsoftpowerpoint",
    "icon_outlook": "simple-icons/microsoftoutlook",
    "icon_onenote": "simple-icons/microsoftonenote",
    "icon_teams": "simple-icons/microsoftteams",
}

CROP_SQUARE = {"icon_fusion360": "left"}


def load_image(name: str, url: str) -> Image.Image:
    print(f"  {name}: {url[:72]}...")
    r = requests.get(url, timeout=30, headers={"User-Agent": "controlPC/1.0"})
    r.raise_for_status()
    img = Image.open(io.BytesIO(r.content))
    if getattr(img, "n_frames", 1) > 1:
        best = max(range(img.n_frames), key=lambda i: img.size)
        img.seek(best)
        img = img.copy()
    if img.mode != "RGBA":
        img = img.convert("RGBA")
    return img


def load_svg_url(url: str) -> Image.Image:
    from reportlab.graphics import renderPM
    from svglib.svglib import svg2rlg

    print(f"  svg: {url}")
    r = requests.get(url, timeout=30, headers={"User-Agent": "controlPC/1.0"})
    r.raise_for_status()
    drawing = svg2rlg(io.BytesIO(r.content))
    if drawing is None:
        raise RuntimeError(f"Could not parse SVG: {url}")
    target = 256.0
    scale = target / max(drawing.width, drawing.height)
    drawing.width *= scale
    drawing.height *= scale
    drawing.scale(scale, scale)
    png = renderPM.drawToString(drawing, fmt="PNG")
    img = Image.open(io.BytesIO(png))
    if img.mode != "RGBA":
        img = img.convert("RGBA")
    return img


def load_svg_icon(slug: str) -> Image.Image:
    return load_svg_url(f"https://api.iconify.design/{slug}.svg")


def prepare_icon(name: str, img: Image.Image) -> Image.Image:
    if name in CROP_SQUARE and img.width != img.height:
        side = min(img.width, img.height)
        if CROP_SQUARE[name] == "left":
            img = img.crop((0, 0, side, side))
        else:
            left = (img.width - side) // 2
            top = (img.height - side) // 2
            img = img.crop((left, top, left + side, top + side))
    return img


def to_rgb565(img: Image.Image, name: str) -> list[int]:
    img = prepare_icon(name, img)
    img = img.resize((ICON_SIZE, ICON_SIZE), Image.Resampling.LANCZOS)
    pixels: list[int] = []
    for y in range(ICON_SIZE):
        for x in range(ICON_SIZE):
            r, g, b, a = img.getpixel((x, y))
            if a < ALPHA_CUTOFF:
                pixels.append(TRANSP_RGB565)
            else:
                c = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)
                if c == TRANSP_RGB565:
                    c = 0xF800  # avoid accidental transparency
                pixels.append(c)
    return pixels


def emit_c_array(name: str, pixels: list[int]) -> str:
    lines = [
        f"// {ICON_SIZE}x{ICON_SIZE} RGB565",
        f"const uint16_t {name}[{ICON_SIZE * ICON_SIZE}] PROGMEM = {{",
    ]
    for i in range(0, len(pixels), 8):
        chunk = pixels[i : i + 8]
        hexes = ", ".join(f"0x{v:04X}" for v in chunk)
        lines.append(f"  {hexes},")
    lines.append("};")
    return "\n".join(lines)


def load_icon(name: str) -> Image.Image:
    local = ASSETS / f"{name}.png"
    if local.is_file():
        print(f"  {name}: local {local.name}")
        img = Image.open(local)
        return img.convert("RGBA")

    url = ICON_URLS[name]
    try:
        if url.endswith(".svg") or "iconify.design" in url and url.endswith(".svg"):
            if "iconify.design/" in url:
                slug = url.split("iconify.design/")[1].replace(".svg", "")
                return load_svg_icon(slug)
            return load_svg_url(url)
        return load_image(name, url)
    except Exception as err:
        print(f"  warn: {name} failed ({err})")
        if name in SVG_FALLBACKS:
            return load_svg_icon(SVG_FALLBACKS[name])
        raise


def main() -> None:
    ASSETS.mkdir(parents=True, exist_ok=True)
    parts = [
        "#pragma once",
        "#include <pgmspace.h>",
        f"#define ICON_W {ICON_SIZE}",
        f"#define ICON_H {ICON_SIZE}",
        f"#define ICON_TRANSP 0x{TRANSP_RGB565:04X}",
        "",
    ]

    for name in ICON_URLS:
        img = load_icon(name)
        parts.append(emit_c_array(name, to_rgb565(img, name)))
        parts.append("")

    OUT.write_text("\n".join(parts), encoding="utf-8")
    print(f"\nWrote {OUT} ({OUT.stat().st_size // 1024} KB)")


if __name__ == "__main__":
    main()
