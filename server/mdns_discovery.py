"""Discover edge nodes via mDNS and poll their /api/data — zero IP configuration."""
from __future__ import annotations

import json
import logging
import socket
import threading
import time
import urllib.error
import urllib.request

from pydantic import ValidationError

import config
from database_handler import DatabaseHandler
from models import TelemetryPayload
from telemetry_processor import ingest_telemetry

logger = logging.getLogger(__name__)

try:
    from zeroconf import ServiceBrowser, ServiceListener, Zeroconf
except ImportError:
    Zeroconf = None  # type: ignore
    ServiceBrowser = None  # type: ignore
    ServiceListener = object  # type: ignore


class _EdgeMdnsListener(ServiceListener):
    def __init__(self, devices: dict[str, str], lock: threading.Lock) -> None:
        self._devices = devices
        self._lock = lock

    def add_service(self, zc, service_type: str, name: str) -> None:
        self._update(zc, service_type, name)

    def update_service(self, zc, service_type: str, name: str) -> None:
        self._update(zc, service_type, name)

    def remove_service(self, zc, service_type: str, name: str) -> None:
        with self._lock:
            self._devices.pop(name, None)

    def _update(self, zc, service_type: str, name: str) -> None:
        info = zc.get_service_info(service_type, name)
        if not info or not info.addresses:
            return
        ip = socket.inet_ntoa(info.addresses[0])
        with self._lock:
            self._devices[name] = ip
        logger.info("mDNS edge found: %s at %s", name, ip)


class MdnsDiscoveryService:
    def __init__(self, db: DatabaseHandler, recording_event_id_getter) -> None:
        self.db = db
        self._recording_event_id_getter = recording_event_id_getter
        self._devices: dict[str, str] = {}
        self._lock = threading.Lock()
        self._thread: threading.Thread | None = None
        self._zc: Zeroconf | None = None
        self._browser: ServiceBrowser | None = None
        self._listener: _EdgeMdnsListener | None = None

    def _poll_loop(self) -> None:
        while True:
            with self._lock:
                targets = list(self._devices.items())
            for name, ip in targets:
                url = f"http://{ip}/api/data"
                try:
                    with urllib.request.urlopen(url, timeout=2) as resp:
                        raw = json.loads(resp.read().decode("utf-8"))
                    payload = TelemetryPayload.model_validate(raw)
                    ingest_telemetry(
                        self.db,
                        payload,
                        self._recording_event_id_getter,
                        source="mdns",
                    )
                except (urllib.error.URLError, ValidationError, json.JSONDecodeError, OSError) as exc:
                    logger.debug("mDNS poll %s failed: %s", name, exc)
            time.sleep(config.MDNS_POLL_INTERVAL_S)

    def start(self) -> None:
        if not config.MDNS_DISCOVERY_ENABLED:
            return
        if Zeroconf is None:
            logger.warning("zeroconf not installed — mDNS discovery disabled")
            return
        if self._thread and self._thread.is_alive():
            return

        self._zc = Zeroconf()
        self._listener = _EdgeMdnsListener(self._devices, self._lock)
        self._browser = ServiceBrowser(
            self._zc,
            config.MDNS_EDGE_SERVICE,
            listener=self._listener,
        )
        self._thread = threading.Thread(target=self._poll_loop, daemon=True, name="mdns-poll")
        self._thread.start()
        logger.info("mDNS discovery browsing %s", config.MDNS_EDGE_SERVICE)
