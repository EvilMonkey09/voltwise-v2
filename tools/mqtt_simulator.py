#!/usr/bin/env python3
"""Publish synthetic VoltWise telemetry for local development."""
from __future__ import annotations

import json
import math
import random
import sys
import time
import uuid

import paho.mqtt.client as mqtt

BROKER_HOST = "localhost"
BROKER_PORT = 1883
TOPIC_PREFIX = "voltwise/telemetry"
INTERVAL_S = 0.5

DEVICE_IDS = ["sim-001", "sim-002", "sim-003"]
SIM_ENERGY: dict[str, dict[str, float]] = {}


def simulate_phase(device_idx: int, phase_idx: int, dt: float) -> dict:
    t = time.time()
    base_i = 2.0 + device_idx * 0.5 + phase_idx * 0.3
    imbalance = 0.0 if phase_idx == 0 else (phase_idx * 0.8 if device_idx == 1 else 0.0)
    current = base_i + imbalance + 0.15 * math.sin(t * 0.4 + phase_idx + device_idx)
    current = max(0.0, current)
    voltage = 230.0 + 2.0 * math.sin(t * 0.2 + phase_idx)
    power = voltage * current * 0.95
    label = f"L{phase_idx + 1}"
    key = f"{DEVICE_IDS[device_idx]}:{label}"
    SIM_ENERGY.setdefault(key, 0.0)
    SIM_ENERGY[key] += power * (dt / 3600.0)
    return {
        "label": label,
        "voltage": round(voltage, 1),
        "current": round(current, 3),
        "power": round(power, 1),
        "energy": round(SIM_ENERGY[key], 0),
        "frequency": 50.0,
        "power_factor": 0.95,
    }


def build_payload(device_id: str, device_idx: int, dt: float) -> dict:
    phases = [simulate_phase(device_idx, i, dt) for i in range(3)]
    i1, i2, i3 = phases[0]["current"], phases[1]["current"], phases[2]["current"]
    val = (i1**2 + i2**2 + i3**2) - (i1 * i2 + i2 * i3 + i3 * i1)
    neutral = round(math.sqrt(max(0.0, val)), 3)
    return {
        "device_id": device_id,
        "timestamp": time.time(),
        "phases": phases,
        "neutral_current_a": neutral,
        "system": {
            "uptime_s": time.time() % 86400,
            "ip": f"192.168.1.{10 + device_idx}",
            "network_type": "ethernet" if device_idx % 2 == 0 else "wifi",
            "simulation": True,
        },
    }


def main() -> None:
    host = sys.argv[1] if len(sys.argv) > 1 else BROKER_HOST
    port = int(sys.argv[2]) if len(sys.argv) > 2 else BROKER_PORT

    client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2, client_id=f"voltwise-sim-{uuid.uuid4().hex[:8]}")
    client.connect(host, port, 60)
    client.loop_start()

    print(f"Publishing to {TOPIC_PREFIX}/<device_id> on {host}:{port} every {INTERVAL_S}s")
    last_tick = time.time()

    try:
        while True:
            now = time.time()
            dt = max(0.0, min(5.0, now - last_tick))
            last_tick = now
            for idx, device_id in enumerate(DEVICE_IDS):
                payload = build_payload(device_id, idx, dt)
                topic = f"{TOPIC_PREFIX}/{device_id}"
                client.publish(topic, json.dumps(payload), qos=0)
            time.sleep(INTERVAL_S)
    except KeyboardInterrupt:
        print("\nStopped.")
    finally:
        client.loop_stop()
        client.disconnect()


if __name__ == "__main__":
    main()
