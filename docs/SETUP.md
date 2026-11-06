# VoltWise einrichten

Schritt-für-Schritt vom PC bis zur Messung. Edge–Central: Central läuft auf dem Computer, Messknoten sind ESP32/WT32 mit PZEM-004T.

## 1. Central auf dem PC (empfohlen: Desktop-App)

Kein Docker nötig für den Normalbetrieb.

1. Öffne die [GitHub Releases](https://github.com/EvilMonkey09/voltwise-v2/releases) des Repos.
2. Lade die App für dein System:
   - **Windows:** `VoltWise-Central-Windows-x86_64.exe`
   - **macOS:** `VoltWise-Central-macOS.dmg`
3. Starte die App (macOS: aus dem DMG nach Programme ziehen).
4. Browser öffnet sich mit dem Dashboard: **http://127.0.0.1:25555**  
   Im System-Tray: *Open Dashboard* / *Quit*.

Telemetrie vom Edge kommt per **UDP/mDNS** im LAN — kein MQTT-Broker in der App nötig.

**Hinweise:** Unsignierte Builds können von SmartScreen (Windows) oder Gatekeeper (macOS) blockiert werden — einmalig erlauben. Daten liegen unter `%APPDATA%\VoltWise` bzw. `~/Library/Application Support/VoltWise`.

### Alternative: Docker

```bash
git clone https://github.com/EvilMonkey09/voltwise-v2.git
cd voltwise-v2
docker compose up --build
```

Dashboard: http://localhost:25555 — inkl. Mosquitto.

### Alternative: Python (Entwicklung)

```bash
cd server
pip install -r requirements.txt
python run_desktop.py
```

## 2. Hardware

| Teil | Menge | Hinweis |
|------|------:|---------|
| ESP32-DevKit oder WT32-ETH01 | 1 | UART: RX **GPIO 16**, TX **GPIO 17**, 9600 Baud |
| PZEM-004T v3.0 (+ CT) | 3 oder 4 | je nach Messprofil |
| 5-V-Netzteil | 1 | Board + Module |
| USB-Kabel / USB-UART | 1 | zum Flashen |

**Messprofile**

- **3 Sensoren (L1, L2, L3 + N gerechnet):** Adressen 1–3, Nullleiter berechnet  
- **4 Sensoren (L1, L2, L3, N):** Adressen 1–3 Phasen, **4 = N** (CT um Neutral, Spannung z. B. L1–N)  

PE nicht messen.

## 3. ESP flashen

1. In Central: **Flasher** (`http://127.0.0.1:25555/flasher`) — Chrome oder Edge.
2. Board-Profil wählen (WT32-ETH01 / ESP32-WROOM / Simulation).
3. Optional Anzeigename setzen.
4. USB verbinden → Flashen.

## 4. Netzwerk (vor dem Geräte-Setup)

- **LAN / Ethernet (WT32):** Kabel stecken → Uplink fertig.  
- **WLAN:** Gerät öffnet Hotspot `VoltWise-Setup-XXXX` → mit Handy verbinden → Captive-Portal nur für **WLAN** → danach Geräte-IP im Heimnetz notieren.

## 5. Setup-Wizard (einmalig)

Im Browser die **IP des Edge** öffnen (z. B. `http://192.168.x.x/`). Solange Setup nicht abgeschlossen ist, erscheint der Wizard:

1. **Anzeigename**
2. **Messprofil** wählen
3. **PZEM-Adressen** geführt setzen (jeweils nur **ein** Modul am UART) → am Ende alle parallel → Bus prüfen
4. **Fertig** → lokales Dashboard

Ohne erfolgreiche Adressen für das Profil lässt sich der Wizard nicht abschließen.

## 6. In Central prüfen

Das Gerät sollte im Dashboard erscheinen (UDP). Live-Werte für L1/L2/L3 und Neutralstrom (gerechnet oder gemessen).

## 7. Werkseinstellungen / Wizard erneut

Auf dem Edge: **Einstellungen → Auf Werkseinstellungen zurücksetzen**.  
Löscht WLAN, Name, Profil und Setup-Status (Geräte-ID bleibt). Nach Neustart: ggf. WLAN erneut, dann Wizard.

## Troubleshooting

| Problem | Idee |
|---------|------|
| Keine Desktop-App / Flasher ohne Firmware | Release-Assets prüfen; lokal `./tools/build_firmware.sh` |
| Alle PZEM antworten auf Adresse 1 | Einzeln adressieren (Wizard) |
| Kein UART / Messwerte | RX/TX gekreuzt? 5 V TTL vs. 3,3 V? Gemeinsames GND? |
| Gerät nicht in Central | Gleiches LAN; Firewall UDP **48485**; Edge-IP im Browser testen |
| macOS blockiert App | Systemeinstellungen → Datenschutz → trotzdem öffnen |

## Weiterführend

- Telemetrie-Schema: [telemetry_spec.json](telemetry_spec.json)
- Firmware bauen: `./tools/build_firmware.sh`
