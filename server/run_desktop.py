#!/usr/bin/env python3
"""VoltWise Central desktop launcher with system tray (dev + PyInstaller)."""
from __future__ import annotations

import os
import sys
import threading
import webbrowser


def _server_dir() -> str:
    if getattr(sys, "frozen", False):
        return getattr(sys, "_MEIPASS", os.path.dirname(sys.executable))
    return os.path.dirname(os.path.abspath(__file__))


SERVER_DIR = _server_dir()
sys.path.insert(0, SERVER_DIR)
try:
    os.chdir(SERVER_DIR)
except OSError:
    pass

from app import app, bootstrap, config  # noqa: E402


def run_flask() -> None:
    bootstrap()
    app.run(host="0.0.0.0", port=config.SERVER_PORT, debug=False, use_reloader=False)


def _tray_image():
    from PIL import Image, ImageDraw

    logo = os.path.join(SERVER_DIR, "logo.png")
    if os.path.isfile(logo):
        return Image.open(logo).convert("RGBA").resize((64, 64))
    img = Image.new("RGB", (64, 64), color=(8, 8, 12))
    d = ImageDraw.Draw(img)
    d.rounded_rectangle([6, 6, 58, 58], radius=10, outline=(34, 211, 238), width=3)
    d.polygon([(28, 16), (40, 16), (30, 32), (42, 48), (26, 48), (34, 32)], fill=(34, 211, 238))
    return img


def run_tray() -> None:
    try:
        from pystray import Icon, Menu, MenuItem
    except ImportError:
        run_flask()
        return

    def open_dash(_icon, _item):
        webbrowser.open(f"http://127.0.0.1:{config.SERVER_PORT}")

    def quit_app(icon, _item):
        icon.stop()
        os._exit(0)

    menu = Menu(
        MenuItem("Open Dashboard", open_dash, default=True),
        MenuItem("Quit", quit_app),
    )
    icon = Icon("VoltWise", _tray_image(), "VoltWise Central", menu)
    threading.Thread(target=run_flask, daemon=True, name="flask").start()
    webbrowser.open(f"http://127.0.0.1:{config.SERVER_PORT}")
    icon.run()


if __name__ == "__main__":
    if os.environ.get("VOLTWISE_TRAY", "1") == "1":
        run_tray()
    else:
        run_flask()
