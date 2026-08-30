"""Multi-device SQLite persistence for VoltWise server."""
from __future__ import annotations

import sqlite3
import time
from pathlib import Path

from imbalance import ImbalanceResult, compute_imbalance, resolve_neutral_current
from models import TelemetryPayload


class DatabaseHandler:
    def __init__(self, db_path: str | Path) -> None:
        self.db_path = str(db_path)
        self.init_db()

    def get_connection(self) -> sqlite3.Connection:
        conn = sqlite3.connect(self.db_path)
        conn.row_factory = sqlite3.Row
        return conn

    def init_db(self) -> None:
        conn = self.get_connection()
        c = conn.cursor()
        c.execute(
            """
            CREATE TABLE IF NOT EXISTS devices (
                device_id TEXT PRIMARY KEY,
                ip TEXT,
                network_type TEXT,
                last_seen REAL,
                status TEXT,
                device_label TEXT,
                remote_name TEXT
            )
            """
        )
        c.execute(
            """
            CREATE TABLE IF NOT EXISTS events (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                name TEXT NOT NULL,
                start_time REAL NOT NULL,
                end_time REAL
            )
            """
        )
        c.execute(
            """
            CREATE TABLE IF NOT EXISTS telemetry_logs (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                device_id TEXT NOT NULL,
                timestamp REAL NOT NULL,
                event_id INTEGER,
                l1_v REAL, l1_i REAL, l1_p REAL, l1_e REAL, l1_hz REAL, l1_pf REAL,
                l2_v REAL, l2_i REAL, l2_p REAL, l2_e REAL, l2_hz REAL, l2_pf REAL,
                l3_v REAL, l3_i REAL, l3_p REAL, l3_e REAL, l3_hz REAL, l3_pf REAL,
                neutral_i REAL,
                imbalance_abs REAL,
                imbalance_pct REAL,
                FOREIGN KEY(device_id) REFERENCES devices(device_id),
                FOREIGN KEY(event_id) REFERENCES events(id)
            )
            """
        )
        c.execute(
            """
            CREATE TABLE IF NOT EXISTS event_devices (
                event_id INTEGER NOT NULL,
                device_id TEXT NOT NULL,
                PRIMARY KEY (event_id, device_id),
                FOREIGN KEY(event_id) REFERENCES events(id) ON DELETE CASCADE,
                FOREIGN KEY(device_id) REFERENCES devices(device_id)
            )
            """
        )
        c.execute(
            "CREATE INDEX IF NOT EXISTS idx_telemetry_device_ts ON telemetry_logs(device_id, timestamp)"
        )
        conn.commit()
        conn.close()

    def upsert_device(
        self,
        device_id: str,
        ip: str | None = None,
        network_type: str | None = None,
        last_seen: float | None = None,
        status: str = "online",
    ) -> None:
        conn = self.get_connection()
        c = conn.cursor()
        now = last_seen if last_seen is not None else time.time()
        c.execute(
            """
            INSERT INTO devices (device_id, ip, network_type, last_seen, status)
            VALUES (?, ?, ?, ?, ?)
            ON CONFLICT(device_id) DO UPDATE SET
                ip = COALESCE(excluded.ip, devices.ip),
                network_type = COALESCE(excluded.network_type, devices.network_type),
                last_seen = excluded.last_seen,
                status = excluded.status
            """,
            (device_id, ip, network_type, now, status),
        )
        conn.commit()
        conn.close()

    def update_device_status(self, device_id: str, status: str, last_seen: float | None = None) -> None:
        conn = self.get_connection()
        c = conn.cursor()
        if last_seen is not None:
            c.execute(
                "UPDATE devices SET status = ?, last_seen = ? WHERE device_id = ?",
                (status, last_seen, device_id),
            )
        else:
            c.execute(
                "UPDATE devices SET status = ? WHERE device_id = ?",
                (status, device_id),
            )
        conn.commit()
        conn.close()

    def set_device_label(self, device_id: str, label: str | None) -> bool:
        conn = self.get_connection()
        c = conn.cursor()
        c.execute("UPDATE devices SET device_label = ? WHERE device_id = ?", (label, device_id))
        updated = c.rowcount > 0
        conn.commit()
        conn.close()
        return updated

    def delete_device(self, device_id: str) -> None:
        conn = self.get_connection()
        c = conn.cursor()
        c.execute("DELETE FROM telemetry_logs WHERE device_id = ?", (device_id,))
        c.execute("DELETE FROM devices WHERE device_id = ?", (device_id,))
        conn.commit()
        conn.close()

    def list_devices(self) -> list[dict]:
        conn = self.get_connection()
        c = conn.cursor()
        c.execute("SELECT * FROM devices ORDER BY device_id")
        rows = [dict(row) for row in c.fetchall()]
        conn.close()
        return rows

    def get_device_labels(self) -> dict[str, str | None]:
        conn = self.get_connection()
        c = conn.cursor()
        c.execute("SELECT device_id, device_label FROM devices")
        labels = {row["device_id"]: row["device_label"] for row in c.fetchall()}
        conn.close()
        return labels

    def set_remote_name(self, device_id: str, name: str | None) -> None:
        if not name:
            return
        conn = self.get_connection()
        c = conn.cursor()
        c.execute(
            """
            UPDATE devices SET remote_name = ?
            WHERE device_id = ? AND (remote_name IS NULL OR remote_name = '')
            """,
            (name.strip(), device_id),
        )
        if c.rowcount == 0:
            c.execute("SELECT 1 FROM devices WHERE device_id = ?", (device_id,))
            if not c.fetchone():
                c.execute(
                    "INSERT INTO devices (device_id, remote_name, status) VALUES (?, ?, 'offline')",
                    (device_id, name.strip()),
                )
        conn.commit()
        conn.close()

    def create_event(self, name: str, device_ids: list[str] | None = None) -> int:
        conn = self.get_connection()
        c = conn.cursor()
        start_time = time.time()
        c.execute(
            "INSERT INTO events (name, start_time) VALUES (?, ?)",
            (name, start_time),
        )
        event_id = int(c.lastrowid)
        for device_id in device_ids or []:
            c.execute(
                "INSERT OR IGNORE INTO event_devices (event_id, device_id) VALUES (?, ?)",
                (event_id, device_id),
            )
        conn.commit()
        conn.close()
        return event_id

    def set_event_devices(self, event_id: int, device_ids: list[str]) -> None:
        conn = self.get_connection()
        c = conn.cursor()
        c.execute("DELETE FROM event_devices WHERE event_id = ?", (event_id,))
        for device_id in device_ids:
            c.execute(
                "INSERT OR IGNORE INTO event_devices (event_id, device_id) VALUES (?, ?)",
                (event_id, device_id),
            )
        conn.commit()
        conn.close()

    def get_event_device_ids(self, event_id: int) -> list[str]:
        conn = self.get_connection()
        c = conn.cursor()
        c.execute(
            "SELECT device_id FROM event_devices WHERE event_id = ? ORDER BY device_id",
            (event_id,),
        )
        ids = [row["device_id"] for row in c.fetchall()]
        conn.close()
        return ids

    def should_record_device(self, event_id: int, device_id: str) -> bool:
        device_ids = self.get_event_device_ids(event_id)
        if not device_ids:
            return True
        return device_id in device_ids

    def stop_event(self, event_id: int) -> None:
        conn = self.get_connection()
        c = conn.cursor()
        c.execute(
            "UPDATE events SET end_time = ? WHERE id = ?",
            (time.time(), event_id),
        )
        conn.commit()
        conn.close()

    def get_active_event_id(self) -> int | None:
        conn = self.get_connection()
        c = conn.cursor()
        c.execute("SELECT id FROM events WHERE end_time IS NULL ORDER BY start_time DESC LIMIT 1")
        row = c.fetchone()
        conn.close()
        return int(row["id"]) if row else None

    def log_telemetry(
        self,
        device_id: str,
        payload: TelemetryPayload,
        event_id: int | None = None,
        imbalance: ImbalanceResult | None = None,
    ) -> None:
        imbalance = imbalance or compute_imbalance(payload)
        neutral_i = resolve_neutral_current(payload)
        by_label = payload.phases_by_label()

        def phase_vals(label: str) -> tuple:
            p = by_label.get(label)
            if p is None:
                return (None,) * 6
            return (
                p.voltage,
                p.current,
                p.power,
                p.energy,
                p.frequency,
                p.power_factor,
            )

        l1 = phase_vals("L1")
        l2 = phase_vals("L2")
        l3 = phase_vals("L3")

        conn = self.get_connection()
        c = conn.cursor()
        c.execute(
            """
            INSERT INTO telemetry_logs (
                device_id, timestamp, event_id,
                l1_v, l1_i, l1_p, l1_e, l1_hz, l1_pf,
                l2_v, l2_i, l2_p, l2_e, l2_hz, l2_pf,
                l3_v, l3_i, l3_p, l3_e, l3_hz, l3_pf,
                neutral_i, imbalance_abs, imbalance_pct
            ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
            """,
            (
                device_id,
                payload.timestamp,
                event_id,
                *l1,
                *l2,
                *l3,
                neutral_i,
                imbalance.abs_diff_a,
                imbalance.pct_diff,
            ),
        )
        conn.commit()
        conn.close()

    def get_history(self, device_id: str, limit: int = 500) -> list[dict]:
        conn = self.get_connection()
        c = conn.cursor()
        c.execute(
            """
            SELECT * FROM telemetry_logs
            WHERE device_id = ?
            ORDER BY timestamp DESC
            LIMIT ?
            """,
            (device_id, limit),
        )
        rows = [dict(row) for row in c.fetchall()]
        conn.close()
        rows.reverse()
        return rows

    def get_events(self) -> list[dict]:
        conn = self.get_connection()
        c = conn.cursor()
        c.execute("SELECT * FROM events ORDER BY start_time DESC")
        rows = []
        for row in c.fetchall():
            ev = dict(row)
            end = ev["end_time"] if ev["end_time"] else time.time()
            ev["duration"] = round(end - ev["start_time"], 1)
            c.execute("SELECT COUNT(*) AS cnt FROM telemetry_logs WHERE event_id = ?", (ev["id"],))
            ev["log_count"] = c.fetchone()["cnt"]
            c.execute(
                "SELECT device_id FROM event_devices WHERE event_id = ? ORDER BY device_id",
                (ev["id"],),
            )
            ev["device_ids"] = [r["device_id"] for r in c.fetchall()]
            ev["is_finished"] = ev["end_time"] is not None
            rows.append(ev)
        conn.close()
        return rows

    def get_event(self, event_id: int) -> dict | None:
        conn = self.get_connection()
        c = conn.cursor()
        c.execute("SELECT * FROM events WHERE id = ?", (event_id,))
        row = c.fetchone()
        if not row:
            conn.close()
            return None
        ev = dict(row)
        end = ev["end_time"] if ev["end_time"] else time.time()
        ev["duration"] = round(end - ev["start_time"], 1)
        c.execute("SELECT COUNT(*) AS cnt FROM telemetry_logs WHERE event_id = ?", (event_id,))
        ev["log_count"] = c.fetchone()["cnt"]
        c.execute(
            "SELECT device_id FROM event_devices WHERE event_id = ? ORDER BY device_id",
            (event_id,),
        )
        ev["device_ids"] = [r["device_id"] for r in c.fetchall()]
        ev["is_finished"] = ev["end_time"] is not None
        ev["logs"] = self._fetch_event_logs(c, event_id)
        conn.close()
        return ev

    def _fetch_event_logs(self, cursor, event_id: int) -> list[dict]:
        cursor.execute(
            "SELECT * FROM telemetry_logs WHERE event_id = ? ORDER BY timestamp ASC",
            (event_id,),
        )
        return [dict(row) for row in cursor.fetchall()]

    def update_event(self, event_id: int, name: str) -> None:
        conn = self.get_connection()
        c = conn.cursor()
        c.execute("UPDATE events SET name = ? WHERE id = ?", (name, event_id))
        conn.commit()
        conn.close()

    def delete_event(self, event_id: int) -> None:
        conn = self.get_connection()
        c = conn.cursor()
        c.execute("DELETE FROM telemetry_logs WHERE event_id = ?", (event_id,))
        c.execute("DELETE FROM event_devices WHERE event_id = ?", (event_id,))
        c.execute("DELETE FROM events WHERE id = ?", (event_id,))
        conn.commit()
        conn.close()

    def export_event_csv(self, event_id: int) -> str:
        logs = self.get_event(event_id)
        if not logs:
            return ""
        lines = [
            "Timestamp,Device,L1_V,L1_A,L1_W,L2_V,L2_A,L2_W,L3_V,L3_A,L3_W,Neutral_I_A"
        ]
        for row in logs.get("logs", []):
            lines.append(
                ",".join(
                    str(x)
                    for x in [
                        row["timestamp"],
                        row["device_id"],
                        row.get("l1_v"),
                        row.get("l1_i"),
                        row.get("l1_p"),
                        row.get("l2_v"),
                        row.get("l2_i"),
                        row.get("l2_p"),
                        row.get("l3_v"),
                        row.get("l3_i"),
                        row.get("l3_p"),
                        row.get("neutral_i"),
                    ]
                )
            )
        return "\n".join(lines)

    def export_device_csv(self, device_id: str, limit: int = 5000) -> str:
        rows = self.get_history(device_id, limit=limit)
        lines = [
            "Timestamp,Device,L1_V,L1_A,L1_W,L2_V,L2_A,L2_W,L3_V,L3_A,L3_W,Neutral_I_A"
        ]
        for row in rows:
            lines.append(
                ",".join(
                    str(x) if x is not None else ""
                    for x in [
                        row.get("timestamp"),
                        row.get("device_id", device_id),
                        row.get("l1_v"),
                        row.get("l1_i"),
                        row.get("l1_p"),
                        row.get("l2_v"),
                        row.get("l2_i"),
                        row.get("l2_p"),
                        row.get("l3_v"),
                        row.get("l3_i"),
                        row.get("l3_p"),
                        row.get("neutral_i"),
                    ]
                )
            )
        return "\n".join(lines)
