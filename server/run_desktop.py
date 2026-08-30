#!/usr/bin/env python3
"""VoltWise Central desktop launcher with optional system tray."""
from __future__ import annotations

import os
import sys
import threading
import webbrowser

# Ensure server package is importable
SERVER_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), "server")
sys.path.insert(0, SERVER_DIR)
os.chdir(SERVER_DIR)

from app import app, bootstrap, config  # noqa: E402


def run_flask():
    bootstrap()
    app.run(host="0.0.0.0", port=config.SERVER_PORT, debug=False, use_reloader=False)


def run_tray():
    try:
        from pystray import Icon, Menu, MenuItem
        from PIL import Image, ImageDraw

        def open_dash(_icon, _item):
            webbrowser.open(f"http://127.0.0.1:{config.SERVER_PORT}")

        def quit_app(icon, _item):
            icon.stop()
            os._exit(0)

        img = Image.new("RGB", (64, 64), color=(217, 119, 87))
        d = ImageDraw.Draw(img)
        d.rectangle((12, 12, 52, 52), fill=(255, 255, 255))
        menu = Menu(
            MenuItem("Open Dashboard", open_dash, default=True),
            MenuItem("Quit", quit_app),
        )
        icon = Icon("VoltWise", img, "VoltWise Central", menu)
        threading.Thread(target=run_flask, daemon=True).start()
        webbrowser.open(f"http://127.0.0.1:{config.SERVER_PORT}")
        icon.run()
    except ImportError:
        run_flask()


if __name__ == "__main__":
    if os.environ.get("VOLTWISE_TRAY", "1") == "1":
        run_tray()
    else:
        run_flask()
