from flask import Flask, render_template, jsonify, request, g, redirect, url_for, Response
from urllib.parse import unquote, urlencode

import csv
import io
import logging
import os
import platform
import socket
import sys
import threading
import time
from pathlib import Path

import config
import i18n
import voltwise_release_info
from database_handler import DatabaseHandler
from imbalance import resolve_neutral_current
from mqtt_ingest import MqttIngestService
from provision import NVS_OFFSET, build_provision_nvs, has_provision_params
from settings_store import apply_runtime_settings, load_settings, save_settings
from state import registry


def get_data_dir() -> str:
    env = os.environ.get("VOLTWISE_DATA_DIR", "").strip()
    if env:
        try:
            os.makedirs(env, exist_ok=True)
            return env
        except OSError:
            pass
    app_name = "VoltWise"
    system = platform.system()
    if system == "Windows":
        base_path = os.environ.get("APPDATA", os.path.expanduser("~\\AppData\\Roaming"))
    elif system == "Darwin":
        base_path = os.path.expanduser("~/Library/Application Support")
    else:
        base_path = os.path.expanduser("~/.local/share")
    data_dir = os.path.join(base_path, app_name)
    try:
        os.makedirs(data_dir, exist_ok=True)
    except OSError:
        data_dir = "/tmp"
    return data_dir


DATA_DIR = get_data_dir()
DB_PATH = os.path.join(DATA_DIR, "dashboard.db")
LOG_PATH = os.path.join(DATA_DIR, "debug.log")
FIRMWARE_DIR = Path(__file__).resolve().parent.parent / "firmware-artifacts"

try:
    logging.basicConfig(
        filename=LOG_PATH,
        level=logging.DEBUG,
        format="%(asctime)s - %(levelname)s - %(message)s",
    )
except Exception:
    logging.basicConfig(level=logging.DEBUG)

logging.info("Starting VoltWise Server. Data Directory: %s", DATA_DIR)

db = DatabaseHandler(DB_PATH)
mqtt_service = MqttIngestService(db)
server_settings = load_settings(DATA_DIR)
apply_runtime_settings(server_settings)

app = Flask(__name__)

FLASHER_PROFILES = {
    "wt32-eth01": {"name": "VoltWise Edge WT32-ETH01", "chipFamily": "ESP32"},
    "esp32dev": {"name": "VoltWise Edge ESP32-WROOM", "chipFamily": "ESP32"},
    "simulation": {"name": "VoltWise Edge Simulation", "chipFamily": "ESP32"},
}


@app.before_request
def _set_locale():
    g.locale = i18n.resolve_locale(request)


@app.context_processor
def _i18n_context():
    loc = getattr(g, "locale", "en")

    def _t(key: str) -> str:
        return i18n.translate(loc, key)

    return dict(t=_t, lang=loc, vw_central=i18n.central_js_strings(loc), active_nav="")


@app.route("/set-language/<code>")
def set_language(code):
    code = (code or "").lower()
    if code not in i18n.LOCALES:
        code = "en"
    dest = request.referrer or url_for("index")
    resp = redirect(dest)
    resp.set_cookie(i18n.COOKIE_NAME, code, max_age=365 * 24 * 3600, samesite="Lax", path="/")
    return resp


def resource_path(relative_path: str) -> str:
    try:
        base_path = sys._MEIPASS
    except Exception:
        base_path = os.path.abspath(".")
    return os.path.join(base_path, relative_path)


def app_version_string() -> str:
    p = os.path.join(os.path.dirname(os.path.abspath(__file__)), "VERSION")
    if os.path.isfile(p):
        with open(p, encoding="utf-8") as f:
            return f.read().strip()
    return os.environ.get("VOLTWISE_VERSION", "0.3.0")


def firmware_binary_version(profile: str) -> str | None:
    path = FIRMWARE_DIR / "bin" / profile / "version.txt"
    if path.is_file():
        return path.read_text(encoding="utf-8").strip()
    return None


def discover_lan_ip() -> str:
    env = os.environ.get("VOLTWISE_PUBLIC_MQTT_HOST", "").strip()
    if env:
        return env
    try:
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as s:
            s.connect(("8.8.8.8", 80))
            ip = s.getsockname()[0]
        if is_docker_bridge_ip(ip):
            return ""
        return ip
    except OSError:
        return ""


def is_docker_bridge_ip(host: str) -> bool:
    """Docker bridge networks often use 172.17–172.31.x.x — unreachable from LAN devices."""
    try:
        parts = [int(x) for x in host.split(".")]
        if len(parts) != 4:
            return False
        return parts[0] == 172 and 17 <= parts[1] <= 31
    except ValueError:
        return False


def is_internal_mqtt_host(host: str) -> bool:
    h = (host or "").strip().lower()
    if h in {"", "localhost", "127.0.0.1", "mosquitto", "host.docker.internal", "0.0.0.0"}:
        return True
    return is_docker_bridge_ip(h)


def flasher_mqtt_host(settings: dict) -> tuple[str, bool]:
    configured = str(settings.get("mqtt_broker_host", "")).strip()
    suggested = discover_lan_ip()
    if is_internal_mqtt_host(configured):
        return suggested, True
    return configured, False


if getattr(sys, "frozen", False):
    app.template_folder = resource_path("templates")
    app.static_folder = resource_path("static")


def heartbeat_loop() -> None:
    while True:
        try:
            registry.refresh_statuses()
            for state in registry.list_devices():
                db.update_device_status(
                    state.device_id,
                    state.status,
                    last_seen=state.last_seen if state.last_seen > 0 else None,
                )
        except Exception as exc:
            logging.exception("heartbeat_loop: %s", exc)
        time.sleep(config.HEARTBEAT_INTERVAL_S)


def start_heartbeat_monitor() -> None:
    threading.Thread(target=heartbeat_loop, daemon=True, name="heartbeat").start()


def _merge_device_list() -> list[dict]:
    db_devices = {d["device_id"]: d for d in db.list_devices()}
    seen: set[str] = set()
    result: list[dict] = []
    for state in registry.list_devices():
        seen.add(state.device_id)
        row = db_devices.get(state.device_id, {})
        summary = state.to_summary()
        summary["device_label"] = state.device_label or row.get("device_label")
        summary["remote_name"] = row.get("remote_name")
        result.append(summary)
    for device_id, row in db_devices.items():
        if device_id not in seen:
            result.append(
                {
                    "device_id": device_id,
                    "status": row.get("status", "offline"),
                    "last_seen": row.get("last_seen"),
                    "ip": row.get("ip"),
                    "network_type": row.get("network_type"),
                    "device_label": row.get("device_label"),
                    "remote_name": row.get("remote_name"),
                }
            )
    result.sort(key=lambda d: d.get("device_id", ""))
    return result


def _render(name: str, active: str, **ctx):
    return render_template(name, active_nav=active, **ctx)


@app.route("/")
def index():
    return _render("dashboard.html", "dashboard")


@app.route("/devices/<path:device_id>")
def device_detail_page(device_id):
    return _render("device_detail.html", "dashboard", device_id=unquote(device_id))


@app.route("/events")
def events_page():
    return _render("events.html", "events")


@app.route("/events/<int:event_id>")
def event_detail_page(event_id):
    return _render("event_detail.html", "events", event_id=event_id)


@app.route("/flasher")
def flasher_page():
    settings = load_settings(DATA_DIR)
    mqtt_host, mqtt_internal = flasher_mqtt_host(settings)
    app_ver = app_version_string()
    stale_profiles = [
        p for p in FLASHER_PROFILES
        if (bv := firmware_binary_version(p)) and bv != app_ver
    ]
    return _render(
        "flasher.html",
        "flasher",
        settings=settings,
        flasher_mqtt_host=mqtt_host,
        mqtt_internal=mqtt_internal,
        suggested_lan_ip=discover_lan_ip(),
        stale_firmware_profiles=stale_profiles,
        app_version=app_ver,
    )


@app.route("/api/flasher/recommended-mqtt")
def flasher_recommended_mqtt():
    settings = load_settings(DATA_DIR)
    host, internal = flasher_mqtt_host(settings)
    return jsonify(
        {
            "host": host,
            "port": int(settings.get("mqtt_broker_port", 1883)),
            "internal_configured": internal,
            "configured_host": settings.get("mqtt_broker_host"),
        }
    )


@app.route("/settings")
def settings_page():
    return _render(
        "settings.html",
        "settings",
        settings=load_settings(DATA_DIR),
        app_version=app_version_string(),
    )


@app.route("/api/settings", methods=["GET", "PUT"])
def api_settings():
    global server_settings
    if request.method == "GET":
        return jsonify(load_settings(DATA_DIR))
    data = request.get_json(silent=True) or {}
    server_settings = save_settings(DATA_DIR, data)
    apply_runtime_settings(server_settings)
    return jsonify({"ok": True, "settings": server_settings})


@app.route("/api/app/update-status")
def api_app_update_status():
    cache = Path(DATA_DIR) / "update_check_cache.json"
    info = voltwise_release_info.check_cached_or_fetch(app_version_string(), cache)
    return jsonify(info)


@app.route("/api/devices", methods=["GET"])
@app.route("/api/nodes", methods=["GET"])
def api_devices_list():
    devices = _merge_device_list()
    if request.path.endswith("/nodes"):
        return jsonify([{**d, "node_id": d["device_id"], "node_label": d.get("device_label")} for d in devices])
    return jsonify(devices)


@app.route("/api/devices/<path:device_id>/telemetry", methods=["GET"])
def api_device_telemetry(device_id):
    device_id = unquote(device_id)
    state = registry.get_device(device_id)
    if state is None or state.latest is None:
        return jsonify({"error": "not_found"}), 404
    payload = state.latest
    return jsonify(
        {
            "device_id": device_id,
            "timestamp": payload.timestamp,
            "sensors": payload.to_api_sensors(),
            "neutral_current": resolve_neutral_current(payload),
            "simulation": payload.system.simulation,
            "total_power": round(payload.total_power(), 1),
            "imbalance": state.imbalance.to_dict() if state.imbalance else None,
            "system": payload.system.model_dump(),
        }
    )


@app.route("/api/devices/<path:device_id>/history", methods=["GET"])
def api_device_history(device_id):
    limit = max(1, min(request.args.get("limit", 500, type=int), 5000))
    return jsonify(db.get_history(unquote(device_id), limit=limit))


@app.route("/api/devices/<path:device_id>/trend", methods=["GET"])
def api_device_trend(device_id):
    limit = max(1, min(request.args.get("limit", 60, type=int), config.TREND_HISTORY_MAX))
    return jsonify(registry.get_device_trend(unquote(device_id), limit=limit))


@app.route("/api/fleet/trend", methods=["GET"])
def api_fleet_trend():
    limit = max(1, min(request.args.get("limit", 60, type=int), config.TREND_HISTORY_MAX))
    return jsonify(registry.get_fleet_trend(limit=limit))


@app.route("/api/devices/<path:device_id>/export", methods=["GET"])
def api_device_export(device_id):
    device_id = unquote(device_id)
    limit = max(1, min(request.args.get("limit", 5000, type=int), 50000))
    csv_text = db.export_device_csv(device_id, limit=limit)
    return Response(
        csv_text,
        mimetype="text/csv; charset=utf-8",
        headers={"Content-Disposition": f'attachment; filename="{device_id}_telemetry.csv"'},
    )


@app.route("/api/devices/<path:device_id>/label", methods=["PUT"])
def set_device_label(device_id):
    device_id = unquote(device_id)
    label = (request.get_json(silent=True) or {}).get("label", "").strip() or None
    if not db.set_device_label(device_id, label):
        db.upsert_device(device_id, status="offline")
        db.set_device_label(device_id, label)
    registry.set_label(device_id, label)
    return jsonify({"success": True})


@app.route("/api/devices/<path:device_id>", methods=["DELETE"])
def delete_device(device_id):
    device_id = unquote(device_id)
    registry.remove_device(device_id)
    db.delete_device(device_id)
    return jsonify({"ok": True})


@app.route("/api/events", methods=["GET", "POST"])
def api_events_list():
    if request.method == "POST":
        data = request.get_json(silent=True) or {}
        name = (data.get("name") or "").strip() or "Veranstaltung"
        device_ids = data.get("device_ids") or []
        event_id = db.create_event(name, device_ids)
        return jsonify({"id": event_id, "name": name, "device_ids": device_ids}), 201
    events = db.get_events()
    recording_id = mqtt_service.get_recording_event_id()
    for ev in events:
        ev["recording"] = ev["id"] == recording_id
    return jsonify(events)


@app.route("/api/events/<int:event_id>", methods=["GET", "PUT", "DELETE"])
def api_event_detail(event_id):
    if request.method == "DELETE":
        if mqtt_service.get_recording_event_id() == event_id:
            mqtt_service.set_recording_event_id(None)
        db.delete_event(event_id)
        return jsonify({"ok": True})
    if request.method == "PUT":
        name = (request.get_json(silent=True) or {}).get("name", "").strip()
        if name:
            db.update_event(event_id, name)
        return jsonify({"ok": True})
    ev = db.get_event(event_id)
    if not ev:
        return jsonify({"error": "not_found"}), 404
    ev["recording"] = mqtt_service.get_recording_event_id() == event_id
    return jsonify(ev)


def _telemetry_for_device(device_id: str) -> dict | None:
    state = registry.get_device(device_id)
    if state is None or state.latest is None:
        return None
    payload = state.latest
    return {
        "device_id": device_id,
        "status": state.status,
        "timestamp": payload.timestamp,
        "sensors": payload.to_api_sensors(),
        "neutral_current": resolve_neutral_current(payload),
        "total_power": round(payload.total_power(), 1),
        "imbalance": state.imbalance.to_dict() if state.imbalance else None,
        "system": payload.system.model_dump(),
        "simulation": payload.system.simulation,
    }


@app.route("/api/events/<int:event_id>/devices", methods=["PUT"])
def api_event_devices(event_id):
    ev = db.get_event(event_id)
    if not ev:
        return jsonify({"error": "not_found"}), 404
    data = request.get_json(silent=True) or {}
    device_ids = data.get("device_ids") or []
    db.set_event_devices(event_id, device_ids)
    return jsonify({"ok": True, "device_ids": device_ids})


@app.route("/api/events/<int:event_id>/live", methods=["GET"])
def api_event_live(event_id):
    ev = db.get_event(event_id)
    if not ev:
        return jsonify({"error": "not_found"}), 404
    device_ids = ev.get("device_ids") or []
    if not device_ids:
        device_ids = [d.device_id for d in registry.list_devices()]
    devices = []
    for did in device_ids:
        tel = _telemetry_for_device(did)
        row = db.list_devices()
        meta = next((d for d in row if d["device_id"] == did), {})
        devices.append({
            "device_id": did,
            "device_label": meta.get("device_label"),
            "remote_name": meta.get("remote_name"),
            "telemetry": tel,
        })
    return jsonify({
        "event_id": event_id,
        "recording": mqtt_service.get_recording_event_id() == event_id,
        "devices": devices,
    })


@app.route("/api/events/<int:event_id>/recording/start", methods=["POST"])
def start_event_recording(event_id):
    ev = db.get_event(event_id)
    if not ev:
        return jsonify({"error": "not_found"}), 404
    current = mqtt_service.get_recording_event_id()
    if current and current != event_id:
        db.stop_event(current)
    mqtt_service.set_recording_event_id(event_id)
    return jsonify({"ok": True, "event_id": event_id})


@app.route("/api/events/<int:event_id>/recording/stop", methods=["POST"])
def stop_event_recording(event_id):
    if mqtt_service.get_recording_event_id() == event_id:
        db.stop_event(event_id)
        mqtt_service.set_recording_event_id(None)
    return jsonify({"ok": True})


@app.route("/api/events/<int:event_id>/logs", methods=["GET"])
def api_event_logs(event_id):
    ev = db.get_event(event_id)
    if not ev:
        return jsonify({"error": "not_found"}), 404
    return jsonify(ev.get("logs", []))


@app.route("/api/events/<int:event_id>/export", methods=["GET"])
def api_event_export(event_id):
    ev = db.get_event(event_id)
    if not ev:
        return jsonify({"error": "not_found"}), 404
    csv_text = db.export_event_csv(event_id)
    return Response(
        csv_text,
        mimetype="text/csv",
        headers={"Content-Disposition": f'attachment; filename="event_{event_id}.csv"'},
    )


@app.route("/api/recording/start_all", methods=["POST"])
def start_recording_all():
    data = request.get_json(silent=True) or {}
    online = [d for d in registry.list_devices() if d.status == "online"]
    device_ids = [d.device_id for d in online]
    event_id = db.create_event(data.get("name", "Central Recording"), device_ids)
    mqtt_service.set_recording_event_id(event_id)
    return jsonify({"event_id": event_id, "devices": device_ids})


@app.route("/api/recording/stop_all", methods=["POST"])
def stop_recording_all():
    event_id = mqtt_service.get_recording_event_id()
    if event_id is not None:
        db.stop_event(event_id)
    mqtt_service.set_recording_event_id(None)
    return jsonify({"success": True})


@app.route("/api/recording/status", methods=["GET"])
def recording_status():
    event_id = mqtt_service.get_recording_event_id()
    return jsonify({"recording": event_id is not None, "event_id": event_id})


@app.route("/flasher-static/<path:filename>")
def flasher_static(filename):
    base = Path(__file__).resolve().parent.parent / "flasher"
    path = base / filename
    if not path.is_file():
        return jsonify({"error": "not_found"}), 404
    return Response(path.read_bytes())


@app.route("/api/flasher/manifests", methods=["GET"])
def flasher_manifests():
    version = app_version_string()
    return jsonify(
        [
            {"profile": k, "name": v["name"], "version": version}
            for k, v in FLASHER_PROFILES.items()
        ]
    )


@app.route("/api/flasher/manifest/<profile>", methods=["GET"])
def flasher_manifest(profile):
    if profile not in FLASHER_PROFILES:
        return jsonify({"error": "unknown_profile"}), 404
    meta = FLASHER_PROFILES[profile]
    version = app_version_string()
    bin_dir = FIRMWARE_DIR / "bin" / profile
    parts = []
    offsets = [
        ("bootloader.bin", 0x1000),
        ("partitions.bin", 0x8000),
        ("firmware.bin", 0x10000),
        ("littlefs.bin", 0x290000),
    ]
    for fname, offset in offsets:
        path = bin_dir / fname
        if path.is_file():
            parts.append({"path": f"/api/flasher/firmware/{profile}/{fname}", "offset": offset})
    if not parts:
        parts.append({"path": f"/api/flasher/firmware/{profile}/firmware.bin", "offset": 0x10000})

    name = (request.args.get("name") or "").strip()
    mqtt_host = (request.args.get("mqtt_host") or "").strip()
    mqtt_port = request.args.get("mqtt_port", 1883, type=int)
    if has_provision_params(name, mqtt_host):
        qs = urlencode({"name": name, "mqtt_host": mqtt_host, "mqtt_port": mqtt_port})
        parts.insert(2, {
            "path": f"/api/flasher/provision-nvs?{qs}",
            "offset": NVS_OFFSET,
        })

    manifest = {
        "name": meta["name"],
        "version": version,
        "firmware_built_version": firmware_binary_version(profile),
        "new_install_prompt_erase": True,
        "builds": [{"chipFamily": meta["chipFamily"], "parts": parts}],
    }
    return jsonify(manifest)


@app.route("/api/flasher/provision-nvs", methods=["GET"])
def flasher_provision_nvs():
    name = (request.args.get("name") or "").strip()
    mqtt_host = (request.args.get("mqtt_host") or "").strip()
    mqtt_port = request.args.get("mqtt_port", 1883, type=int)
    try:
        data = build_provision_nvs(name, mqtt_host, mqtt_port)
    except Exception as exc:
        logging.exception("provision-nvs: %s", exc)
        return jsonify({"error": "provision_failed"}), 500
    return Response(data, mimetype="application/octet-stream")


@app.route("/api/flasher/firmware/<profile>/<filename>", methods=["GET"])
def flasher_firmware_file(profile, filename):
    if profile not in FLASHER_PROFILES:
        return jsonify({"error": "unknown"}), 404
    path = FIRMWARE_DIR / "bin" / profile / filename
    if not path.is_file():
        return jsonify({"error": "not_built", "hint": "Run firmware CI build first"}), 404
    return Response(path.read_bytes(), mimetype="application/octet-stream")


def bootstrap() -> None:
    labels = db.get_device_labels()
    registry.sync_labels_from_db(labels)
    active_id = db.get_active_event_id()
    if active_id is not None:
        mqtt_service.set_recording_event_id(active_id)
        logging.info("Restored active recording for event %s", active_id)
    mqtt_service.start()
    start_heartbeat_monitor()


if __name__ == "__main__":
    bootstrap()
    app.run(host="0.0.0.0", port=config.SERVER_PORT, debug=False, use_reloader=False)
