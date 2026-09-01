"""Shared telemetry ingestion for MQTT, UDP, and mDNS discovery."""
from __future__ import annotations

import logging
from typing import Callable

from database_handler import DatabaseHandler
from models import TelemetryPayload
from state import registry

logger = logging.getLogger(__name__)


def ingest_telemetry(
    db: DatabaseHandler,
    payload: TelemetryPayload,
    recording_event_id_getter: Callable[[], int | None],
    *,
    source: str = "mqtt",
) -> None:
    labels = db.get_device_labels()
    label = labels.get(payload.device_id)
    db.upsert_device(
        payload.device_id,
        ip=payload.system.ip,
        network_type=payload.system.network_type,
        last_seen=payload.timestamp,
        status="online",
    )
    state = registry.update_device(payload.device_id, payload, device_label=label)

    if payload.system.device_name:
        db.set_remote_name(payload.device_id, payload.system.device_name)

    event_id = recording_event_id_getter()
    if event_id is not None and db.should_record_device(event_id, payload.device_id):
        db.log_telemetry(
            payload.device_id,
            payload,
            event_id=event_id,
            imbalance=state.imbalance,
        )

    logger.debug("Ingested %s via %s", payload.device_id, source)
