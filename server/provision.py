"""Generate ESP32 NVS partition images for flash-time provisioning."""
from __future__ import annotations

import csv
import hashlib
import subprocess
import sys
import tempfile
from pathlib import Path

TOOLS_DIR = Path(__file__).resolve().parent.parent / "tools"
NVS_GEN = TOOLS_DIR / "nvs_partition_gen.py"
NVS_SIZE = 0x5000
NVS_OFFSET = 0x9000

_cache: dict[str, bytes] = {}


def build_provision_nvs(device_name: str, mqtt_host: str, mqtt_port: int) -> bytes:
    key = hashlib.sha256(
        f"{device_name}|{mqtt_host}|{mqtt_port}".encode()
    ).hexdigest()[:16]
    if key in _cache:
        return _cache[key]

    with tempfile.TemporaryDirectory() as td:
        csv_path = Path(td) / "nvs.csv"
        bin_path = Path(td) / "nvs.bin"
        with csv_path.open("w", newline="", encoding="utf-8") as f:
            w = csv.writer(f)
            w.writerow(["key", "type", "encoding", "value"])
            w.writerow(["voltwise", "namespace", "", ""])
            if device_name:
                w.writerow(["device_name", "data", "string", device_name[:64]])
            if mqtt_host:
                w.writerow(["mqtt_host", "data", "string", mqtt_host[:128]])
            w.writerow(["mqtt_port", "data", "u16", str(int(mqtt_port))])
        result = subprocess.run(
            [sys.executable, str(NVS_GEN), "generate", str(csv_path), str(bin_path), hex(NVS_SIZE)],
            check=True,
            capture_output=True,
            text=True,
        )
        if result.returncode != 0:
            raise RuntimeError(result.stderr or "NVS generation failed")
        data = bin_path.read_bytes()
        _cache[key] = data
        return data


def has_provision_params(name: str | None, mqtt_host: str | None) -> bool:
    return bool((name or "").strip() or (mqtt_host or "").strip())
