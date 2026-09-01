"""Receive edge telemetry via LAN UDP broadcast — no broker IP required."""
from __future__ import annotations

import json
import logging
import socket
import threading

from pydantic import ValidationError

import config
from database_handler import DatabaseHandler
from models import TelemetryPayload
from telemetry_processor import ingest_telemetry

logger = logging.getLogger(__name__)

MAGIC = "voltwise"


class UdpIngestService:
    def __init__(self, db: DatabaseHandler, recording_event_id_getter) -> None:
        self.db = db
        self._recording_event_id_getter = recording_event_id_getter
        self._thread: threading.Thread | None = None

    def _handle(self, data: bytes) -> None:
        try:
            raw = json.loads(data.decode("utf-8"))
        except (UnicodeDecodeError, json.JSONDecodeError) as exc:
            logger.debug("UDP invalid JSON: %s", exc)
            return

        if raw.get("magic") != MAGIC:
            return

        try:
            payload = TelemetryPayload.model_validate(raw)
        except ValidationError as exc:
            logger.warning("UDP validation failed: %s", exc)
            return

        ingest_telemetry(
            self.db,
            payload,
            self._recording_event_id_getter,
            source="udp",
        )

    def start(self) -> None:
        if self._thread and self._thread.is_alive():
            return

        def run() -> None:
            sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
            try:
                sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEPORT, 1)
            except (AttributeError, OSError):
                pass
            sock.bind(("", config.TELEMETRY_UDP_PORT))
            logger.info("UDP telemetry listener on port %s", config.TELEMETRY_UDP_PORT)
            while True:
                try:
                    data, _addr = sock.recvfrom(4096)
                    self._handle(data)
                except OSError as exc:
                    logger.error("UDP ingest error: %s", exc)

        self._thread = threading.Thread(target=run, daemon=True, name="udp-ingest")
        self._thread.start()
