# VoltWise v2

Edge–Central architecture for multi-PDU power monitoring (ESP32 / WT32-ETH01 + PZEM-004T).

Repository: [github.com/EvilMonkey09/voltwise-v2](https://github.com/EvilMonkey09/voltwise-v2)

**Full setup guide (German):** [docs/SETUP.md](docs/SETUP.md)

## Getting started

1. **Install Central on your PC** — download from [Releases](https://github.com/EvilMonkey09/voltwise-v2/releases):
   - Windows: `VoltWise-Central-Windows-x86_64.exe`
   - macOS: `VoltWise-Central-macOS.dmg`  
   Start the app → dashboard at http://127.0.0.1:25555 (system tray: Open Dashboard / Quit).
2. **Flash the ESP** via `/flasher` (Chrome/Edge + USB).
3. **Network:** Ethernet plug-and-play, or WiFi via captive portal `VoltWise-Setup-XXXX`.
4. **Edge setup wizard** (device IP): display name → sensor profile → PZEM addresses (required once).
5. Device appears on the Central dashboard (UDP/mDNS).

Sensor profiles:

- **3 sensors (L1, L2, L3 + N calculated)** — Modbus 1–3  
- **4 sensors (L1, L2, L3, N)** — Modbus 1–4 (neutral measured)

Factory reset on the edge UI re-runs the wizard (keeps `device_id`).

### Alternative: Docker

```bash
docker compose up --build
```

- Dashboard: http://localhost:25555  
- MQTT: localhost:1883  

Production image: `docker compose -f docker-compose.prod.yml up -d`

### Alternative: Python tray

```bash
cd server
pip install -r requirements.txt
python run_desktop.py
```

## Components

| Path | Description |
|------|-------------|
| [server/](server/) | VoltWise Central — UDP/MQTT ingest, dashboard, flasher |
| [edge-firmware/](edge-firmware/) | ESP32/WT32 — PZEM, captive WiFi, setup wizard, OTA |
| [flasher/](flasher/) | Standalone browser flasher |
| [docs/SETUP.md](docs/SETUP.md) | End-user setup (DE) |
| [tools/mqtt_simulator.py](tools/mqtt_simulator.py) | Dev MQTT simulator |

## Build firmware (PlatformIO)

```bash
./tools/build_firmware.sh
# or:
cd edge-firmware && pio run -e esp32dev
```

Profiles: `esp32dev`, `wt32-eth01`, `simulation`. Artifacts: `firmware-artifacts/bin/`.

Desktop icons: `python tools/generate_icons.py`

## Releases & updates

Tag `v*` → CI builds firmware, **Windows/macOS Central apps**, GitHub release assets, and pushes `ghcr.io/.../voltwise-v2`.

```bash
git tag v0.3.2
git push origin v0.3.2
```

Edge OTA polls the same repo for `firmware-<profile>.bin`.

## API

MQTT/UDP payload: [docs/telemetry_spec.json](docs/telemetry_spec.json)

REST highlights: `/api/devices`, `/api/devices/<id>/telemetry`, `/api/fleet/trend`, `/api/events`, `/api/flasher/manifest/<profile>`

## Environment

| Variable | Default | Purpose |
|----------|---------|---------|
| `VOLTWISE_GITHUB_REPO` | `EvilMonkey09/voltwise-v2` | Release / OTA source |
| `MQTT_BROKER_HOST` | `localhost` | Central MQTT (optional with UDP) |
| `VOLTWISE_PORT` | `25555` | Dashboard port |
