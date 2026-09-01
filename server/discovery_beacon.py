"""UDP beacon so edge nodes can find Central MQTT without per-network configuration."""
from __future__ import annotations

import json
import logging
import os
import socket
import threading
import time

import config

logger = logging.getLogger(__name__)

MAGIC = "voltwise"


def _public_mqtt_host() -> str:
    env = os.environ.get("VOLTWISE_PUBLIC_MQTT_HOST", "").strip()
    if env:
        return env
    try:
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as s:
            s.connect(("8.8.8.8", 80))
            ip = s.getsockname()[0]
        parts = [int(x) for x in ip.split(".")]
        if len(parts) == 4 and parts[0] == 172 and 17 <= parts[1] <= 31:
            return ""
        return ip
    except OSError:
        return ""


def discovery_beacon_loop() -> None:
    while True:
        host = _public_mqtt_host()
        if not host:
            time.sleep(10)
            continue

        payload = json.dumps(
            {
                "magic": MAGIC,
                "mqtt_host": host,
                "mqtt_port": config.MQTT_BROKER_PORT,
                "central_port": config.SERVER_PORT,
            }
        ).encode("utf-8")

        try:
            sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            sock.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
            sock.sendto(payload, ("255.255.255.255", config.DISCOVERY_PORT))
            sock.close()
        except OSError as exc:
            logger.debug("discovery beacon failed: %s", exc)

        time.sleep(config.DISCOVERY_BEACON_INTERVAL_S)


def start_discovery_beacon() -> None:
    if not config.DISCOVERY_BEACON_ENABLED:
        return
    thread = threading.Thread(
        target=discovery_beacon_loop,
        daemon=True,
        name="discovery-beacon",
    )
    thread.start()
    logger.info(
        "Central discovery beacon on UDP %s (MQTT at host LAN IP)",
        config.DISCOVERY_PORT,
    )
