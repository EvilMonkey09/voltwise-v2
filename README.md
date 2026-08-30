# VoltWise v2

Edge–Central architecture for multi-PDU power monitoring (ESP32 / WT32-ETH01 + PZEM-004T).

Repository: [github.com/EvilMonkey09/voltwise-v2](https://github.com/EvilMonkey09/voltwise-v2)

## Components

| Path | Description |
|------|-------------|
| [server/](server/) | VoltWise Central — MQTT ingest, dashboard, events, web flasher |
| [edge-firmware/](edge-firmware/) | ESP32/WT32 firmware — PZEM, MQTT, captive portal, local UI, OTA |
| [flasher/](flasher/) | Standalone browser web flasher (esp-web-tools) |
| [shared/ui/](shared/ui/) | Shared design system |
| [tools/mqtt_simulator.py](tools/mqtt_simulator.py) | Dev MQTT simulator |

## Quick start (Central)

```bash
docker compose up --build
```

- Dashboard: http://localhost:25555
- Flasher: http://localhost:25555/flasher
- MQTT: localhost:1883

## Flash ESP32 (browser)

1. Open `/flasher` in Chrome or Edge
2. Select board profile (WT32-ETH01 / ESP32-WROOM / Simulation)
3. Optionally set display name and MQTT broker (written to NVS during flash)
4. Connect USB → Flash

### WiFi onboarding (captive portal)

If the edge node has no Ethernet/WiFi uplink:

| Situation | Behaviour |
|-----------|-----------|
| Fresh flash, no WiFi saved | Hotspot after ~18 s |
| Saved WiFi unreachable | Hotspot after ~25 s |
| Manual | **WLAN einrichten** on edge UI (`http://<device-ip>/`) |

Steps:

1. Join open AP `VoltWise-Setup-XXXX`
2. Captive portal opens (or browse `http://192.168.4.1`)
3. Scan/select WiFi, set MQTT broker if needed
4. AP closes after successful connection
5. Telemetry appears on Central via MQTT

## Build firmware (PlatformIO)

```bash
./tools/build_firmware.sh
# or:
cd edge-firmware && pio run -e esp32dev
```

Profiles: `esp32dev` (WiFi), `wt32-eth01` (Ethernet + WiFi), `simulation` (no hardware).

Artifacts land in `firmware-artifacts/bin/` for the Central flasher.

## Releases & updates

- **Central**: checks GitHub releases on load; banner + Settings → *Check for updates*
- **Edge OTA**: devices poll `EvilMonkey09/voltwise-v2` releases every 6 h and install `firmware-<profile>.bin`
- **CI**: push tag `v*` → [`.github/workflows/release.yml`](.github/workflows/release.yml) builds firmware, creates GitHub release, pushes Docker image to `ghcr.io`

```bash
git tag v0.3.1
git push origin v0.3.1
```

Production deploy (published image):

```bash
docker compose -f docker-compose.prod.yml up -d
```

Docker image after release:

```bash
docker pull ghcr.io/evilmonkey09/voltwise-v2:latest
```

## Desktop tray (optional)

```bash
cd server
pip install -r requirements.txt
python run_desktop.py
```

## API

MQTT payload: [docs/telemetry_spec.json](docs/telemetry_spec.json)

Central REST highlights:

- `/api/devices`, `/api/devices/<id>/telemetry`, `/api/devices/<id>/trend`
- `/api/fleet/trend`
- `/api/events`, `/api/flasher/manifest/<profile>`
- `/api/app/update-status`

## Environment

| Variable | Default | Purpose |
|----------|---------|---------|
| `VOLTWISE_GITHUB_REPO` | `EvilMonkey09/voltwise-v2` | Release / OTA source |
| `MQTT_BROKER_HOST` | `localhost` | Central MQTT broker |
| `VOLTWISE_PORT` | `25555` | Dashboard port |
