"""MQTT telemetry ingestion service."""
from __future__ import annotations

import json
import logging
import threading

import paho.mqtt.client as mqtt
from pydantic import ValidationError

import config
from database_handler import DatabaseHandler
from models import TelemetryPayload
from state import registry

logger = logging.getLogger(__name__)

_TOPIC_SUFFIX = "/+"


class MqttIngestService:
    def __init__(self, db: DatabaseHandler) -> None:
        self.db = db
        self._client: mqtt.Client | None = None
        self._thread: threading.Thread | None = None
        self._recording_event_id: int | None = None
        self._recording_lock = threading.Lock()

    def set_recording_event_id(self, event_id: int | None) -> None:
        with self._recording_lock:
            self._recording_event_id = event_id

    def get_recording_event_id(self) -> int | None:
        with self._recording_lock:
            return self._recording_event_id

    def _topic_for_device(self, device_id: str) -> str:
        return f"{config.MQTT_TOPIC_PREFIX}/{device_id}"

    def _extract_device_id(self, topic: str) -> str | None:
        prefix = config.MQTT_TOPIC_PREFIX + "/"
        if not topic.startswith(prefix):
            return None
        return topic[len(prefix) :]

    def _on_connect(self, client: mqtt.Client, userdata, flags, reason_code, properties=None) -> None:
        topic = f"{config.MQTT_TOPIC_PREFIX}{_TOPIC_SUFFIX}"
        client.subscribe(topic, qos=0)
        logger.info("MQTT connected, subscribed to %s", topic)

    def _on_message(self, client: mqtt.Client, userdata, msg) -> None:
        device_id_from_topic = self._extract_device_id(msg.topic)
        if not device_id_from_topic:
            logger.warning("Ignoring message on unexpected topic: %s", msg.topic)
            return

        try:
            raw = json.loads(msg.payload.decode("utf-8"))
        except (UnicodeDecodeError, json.JSONDecodeError) as exc:
            logger.warning("Invalid JSON on %s: %s", msg.topic, exc)
            return

        try:
            payload = TelemetryPayload.model_validate(raw)
        except ValidationError as exc:
            logger.warning("Validation failed for %s: %s", msg.topic, exc)
            return

        if payload.device_id != device_id_from_topic:
            logger.warning(
                "device_id mismatch: topic=%s payload=%s",
                device_id_from_topic,
                payload.device_id,
            )
            return

        labels = self.db.get_device_labels()
        label = labels.get(payload.device_id)
        self.db.upsert_device(
            payload.device_id,
            ip=payload.system.ip,
            network_type=payload.system.network_type,
            last_seen=payload.timestamp,
            status="online",
        )
        state = registry.update_device(payload.device_id, payload, device_label=label)

        if payload.system.device_name:
            self.db.set_remote_name(payload.device_id, payload.system.device_name)

        event_id = self.get_recording_event_id()
        if event_id is not None and self.db.should_record_device(event_id, payload.device_id):
            self.db.log_telemetry(
                payload.device_id,
                payload,
                event_id=event_id,
                imbalance=state.imbalance,
            )

    def start(self) -> None:
        if self._thread and self._thread.is_alive():
            return

        client = mqtt.Client(
            mqtt.CallbackAPIVersion.VERSION2,
            client_id=config.MQTT_CLIENT_ID,
        )
        client.on_connect = self._on_connect
        client.on_message = self._on_message
        self._client = client

        def run() -> None:
            while True:
                try:
                    logger.info(
                        "Connecting to MQTT broker %s:%s",
                        config.MQTT_BROKER_HOST,
                        config.MQTT_BROKER_PORT,
                    )
                    client.connect(config.MQTT_BROKER_HOST, config.MQTT_BROKER_PORT, 60)
                    client.loop_forever()
                except Exception as exc:
                    logger.error("MQTT connection error: %s — retrying in 5s", exc)
                    import time

                    time.sleep(5)

        self._thread = threading.Thread(target=run, daemon=True, name="mqtt-ingest")
        self._thread.start()

    def stop(self) -> None:
        if self._client:
            self._client.disconnect()
