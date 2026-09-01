"""Server configuration (env-overridable)."""
from __future__ import annotations

import os

MQTT_BROKER_HOST = os.environ.get("MQTT_BROKER_HOST", "localhost")
MQTT_BROKER_PORT = int(os.environ.get("MQTT_BROKER_PORT", "1883"))
MQTT_TOPIC_PREFIX = os.environ.get("MQTT_TOPIC_PREFIX", "voltwise/telemetry")
MQTT_CLIENT_ID = os.environ.get("MQTT_CLIENT_ID", "voltwise-server")

HEARTBEAT_INTERVAL_S = float(os.environ.get("HEARTBEAT_INTERVAL_S", "1.0"))
OFFLINE_THRESHOLD_S = float(os.environ.get("OFFLINE_THRESHOLD_S", "5.0"))
TREND_HISTORY_MAX = int(os.environ.get("TREND_HISTORY_MAX", "120"))
TREND_SAMPLE_INTERVAL_S = float(os.environ.get("TREND_SAMPLE_INTERVAL_S", "2.0"))

IMBALANCE_ABS_THRESHOLD_A = float(os.environ.get("IMBALANCE_ABS_THRESHOLD_A", "2.0"))
IMBALANCE_PCT_THRESHOLD = float(os.environ.get("IMBALANCE_PCT_THRESHOLD", "15.0"))
IMBALANCE_MIN_AVG_A = float(os.environ.get("IMBALANCE_MIN_AVG_A", "0.5"))
IMBALANCE_MODE = os.environ.get("IMBALANCE_MODE", "both")

SERVER_PORT = int(os.environ.get("VOLTWISE_PORT", "25555"))

DISCOVERY_PORT = int(os.environ.get("VOLTWISE_DISCOVERY_PORT", "48484"))
DISCOVERY_BEACON_INTERVAL_S = float(os.environ.get("VOLTWISE_DISCOVERY_BEACON_INTERVAL_S", "10"))
DISCOVERY_BEACON_ENABLED = os.environ.get("VOLTWISE_DISCOVERY_BEACON", "0") == "1"

TELEMETRY_UDP_PORT = int(os.environ.get("VOLTWISE_TELEMETRY_UDP_PORT", "48485"))
MDNS_DISCOVERY_ENABLED = os.environ.get("VOLTWISE_MDNS_DISCOVERY", "1") == "1"
MDNS_EDGE_SERVICE = os.environ.get("VOLTWISE_MDNS_EDGE_SERVICE", "_voltwise._tcp.local.")
MDNS_POLL_INTERVAL_S = float(os.environ.get("VOLTWISE_MDNS_POLL_INTERVAL_S", "1.0"))
