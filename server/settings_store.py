"""Server settings persisted in JSON."""
from __future__ import annotations

import json
import os
from pathlib import Path

import config

DEFAULTS = {
    "mqtt_broker_host": config.MQTT_BROKER_HOST,
    "mqtt_broker_port": config.MQTT_BROKER_PORT,
    "imbalance_abs_threshold_a": config.IMBALANCE_ABS_THRESHOLD_A,
    "imbalance_pct_threshold": config.IMBALANCE_PCT_THRESHOLD,
    "imbalance_min_avg_a": config.IMBALANCE_MIN_AVG_A,
    "imbalance_mode": config.IMBALANCE_MODE,
    "offline_threshold_s": config.OFFLINE_THRESHOLD_S,
    "flasher_manifest_base": os.environ.get(
        "VOLTWISE_FLASHER_MANIFEST_BASE", "/api/flasher/manifest"
    ),
}


def _settings_path(data_dir: str) -> Path:
    return Path(data_dir) / "server_settings.json"


def load_settings(data_dir: str) -> dict:
    path = _settings_path(data_dir)
    if not path.is_file():
        return dict(DEFAULTS)
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
        out = dict(DEFAULTS)
        out.update({k: v for k, v in data.items() if k in DEFAULTS})
        return out
    except (json.JSONDecodeError, OSError):
        return dict(DEFAULTS)


def save_settings(data_dir: str, updates: dict) -> dict:
    current = load_settings(data_dir)
    for key in DEFAULTS:
        if key in updates:
            current[key] = updates[key]
    path = _settings_path(data_dir)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(current, indent=2), encoding="utf-8")
    return current


def apply_runtime_settings(settings: dict) -> None:
    config.IMBALANCE_ABS_THRESHOLD_A = float(settings["imbalance_abs_threshold_a"])
    config.IMBALANCE_PCT_THRESHOLD = float(settings["imbalance_pct_threshold"])
    config.IMBALANCE_MIN_AVG_A = float(settings["imbalance_min_avg_a"])
    config.IMBALANCE_MODE = str(settings["imbalance_mode"])
    config.OFFLINE_THRESHOLD_S = float(settings["offline_threshold_s"])
