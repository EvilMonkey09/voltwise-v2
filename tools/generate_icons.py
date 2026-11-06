#!/usr/bin/env python3
"""Generate server/icon.ico and server/icon.icns (+ logo.png) for desktop builds."""
from __future__ import annotations

import os
from pathlib import Path

from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parent.parent
SERVER = ROOT / "server"


def make_logo(size: int = 512) -> Image.Image:
    img = Image.new("RGBA", (size, size), (8, 8, 12, 255))
    d = ImageDraw.Draw(img)
    margin = size // 10
    # Accent rounded square
    d.rounded_rectangle(
        [margin, margin, size - margin, size - margin],
        radius=size // 8,
        fill=(15, 15, 20, 255),
        outline=(34, 211, 238, 255),
        width=max(2, size // 64),
    )
    # Simple "bolt" / V mark
    cx, cy = size // 2, size // 2
    bolt = [
        (cx - size // 8, cy - size // 5),
        (cx + size // 16, cy - size // 5),
        (cx - size // 32, cy - size // 48),
        (cx + size // 8, cy + size // 5),
        (cx - size // 16, cy + size // 5),
        (cx + size // 32, cy + size // 48),
    ]
    d.polygon(bolt, fill=(34, 211, 238, 255))
    return img


def main() -> None:
    SERVER.mkdir(parents=True, exist_ok=True)
    logo = make_logo(512)
    logo_path = SERVER / "logo.png"
    logo.save(logo_path, format="PNG")

    ico = logo.resize((256, 256), Image.Resampling.LANCZOS)
    ico.save(SERVER / "icon.ico", format="ICO", sizes=[(256, 256), (128, 128), (64, 64), (32, 32), (16, 16)])

    # ICNS: Pillow may not support all platforms; write multi-size PNG set as fallback .icns attempt
    icns_path = SERVER / "icon.icns"
    try:
        ico.save(icns_path, format="ICNS")
    except Exception:
        # macOS CI can convert; keep a 512 PNG named for create-dmg / pyinstaller --icon
        logo.save(SERVER / "icon.png", format="PNG")
        print("ICNS not supported by Pillow here; wrote icon.png — CI may use icon.ico/png")

    print(f"Wrote {logo_path}")
    print(f"Wrote {SERVER / 'icon.ico'}")
    if icns_path.exists():
        print(f"Wrote {icns_path}")


if __name__ == "__main__":
    os.chdir(ROOT)
    main()
