"""Thread-safe in-memory device registry."""
from __future__ import annotations

import threading
import time
from collections import deque
from dataclasses import dataclass, field
from typing import Literal

from imbalance import ImbalanceResult, compute_imbalance
from models import TelemetryPayload

import config


def _history_deque() -> deque:
    return deque(maxlen=config.TREND_HISTORY_MAX)


@dataclass
class DeviceState:
    device_id: str
    last_seen: float = 0.0
    status: Literal["online", "offline"] = "offline"
    latest: TelemetryPayload | None = None
    imbalance: ImbalanceResult | None = None
    device_label: str | None = None
    ip: str | None = None
    network_type: str | None = None
    history: deque = field(default_factory=_history_deque)
    _last_history_sample: float = 0.0

    def to_summary(self) -> dict:
        out: dict = {
            "device_id": self.device_id,
            "status": self.status,
            "last_seen": self.last_seen,
            "ip": self.ip,
            "network_type": self.network_type,
            "device_label": self.device_label,
        }
        if self.imbalance:
            out["imbalance"] = self.imbalance.to_dict()
        if self.latest:
            out["total_power"] = round(self.latest.total_power(), 1)
            out["simulation"] = self.latest.system.simulation
        return out


class DeviceRegistry:
    def __init__(self) -> None:
        self._lock = threading.Lock()
        self._devices: dict[str, DeviceState] = {}
        self._fleet_history: deque = deque(maxlen=config.TREND_HISTORY_MAX)
        self._last_fleet_sample: float = 0.0

    def update_device(
        self,
        device_id: str,
        payload: TelemetryPayload,
        device_label: str | None = None,
    ) -> DeviceState:
        now = time.time()
        imbalance = compute_imbalance(payload)
        with self._lock:
            state = self._devices.get(device_id)
            if state is None:
                state = DeviceState(device_id=device_id)
                self._devices[device_id] = state
            state.last_seen = now
            state.status = "online"
            state.latest = payload
            state.imbalance = imbalance
            state.ip = payload.system.ip
            state.network_type = payload.system.network_type
            if device_label is not None:
                state.device_label = device_label
            self._maybe_append_history(state, payload)
            self._maybe_sample_fleet(now)
            return state

    def _maybe_append_history(self, state: DeviceState, payload: TelemetryPayload) -> None:
        ts = payload.timestamp
        if state.history and (ts - state._last_history_sample) < config.TREND_SAMPLE_INTERVAL_S:
            return
        state.history.append(
            {
                "timestamp": ts,
                "sensors": payload.to_api_sensors(),
            }
        )
        state._last_history_sample = ts

    def _maybe_sample_fleet(self, now: float) -> None:
        if now - self._last_fleet_sample < config.TREND_SAMPLE_INTERVAL_S:
            return
        total = sum(
            s.latest.total_power()
            for s in self._devices.values()
            if s.status == "online" and s.latest is not None
        )
        self._fleet_history.append({"timestamp": now, "total_power": round(total, 1)})
        self._last_fleet_sample = now

    def set_label(self, device_id: str, label: str | None) -> bool:
        with self._lock:
            state = self._devices.get(device_id)
            if state is None:
                return False
            state.device_label = label
            return True

    def remove_device(self, device_id: str) -> bool:
        with self._lock:
            return self._devices.pop(device_id, None) is not None

    def get_device(self, device_id: str) -> DeviceState | None:
        with self._lock:
            return self._devices.get(device_id)

    def get_device_trend(self, device_id: str, limit: int = 60) -> list[dict]:
        with self._lock:
            state = self._devices.get(device_id)
            if state is None:
                return []
            return list(state.history)[-limit:]

    def get_fleet_trend(self, limit: int = 60) -> list[dict]:
        with self._lock:
            return list(self._fleet_history)[-limit:]

    def list_devices(self) -> list[DeviceState]:
        with self._lock:
            return list(self._devices.values())

    def refresh_statuses(self) -> None:
        now = time.time()
        with self._lock:
            for state in self._devices.values():
                if state.last_seen > 0 and (now - state.last_seen) > config.OFFLINE_THRESHOLD_S:
                    state.status = "offline"

    def sync_labels_from_db(self, labels: dict[str, str | None]) -> None:
        with self._lock:
            for device_id, label in labels.items():
                if device_id in self._devices:
                    self._devices[device_id].device_label = label


registry = DeviceRegistry()
