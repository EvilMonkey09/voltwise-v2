"""Pydantic models matching docs/telemetry_spec.json."""
from __future__ import annotations

from typing import Literal

from pydantic import BaseModel, Field


class PhaseMetrics(BaseModel):
    label: Literal["L1", "L2", "L3"]
    voltage: float = Field(ge=0)
    current: float = Field(ge=0)
    power: float
    energy: float = Field(ge=0)
    frequency: float = Field(ge=0)
    power_factor: float = Field(ge=0, le=1)


class SystemStatus(BaseModel):
    uptime_s: float = Field(ge=0)
    ip: str
    network_type: Literal["wifi", "ethernet"]
    simulation: bool = False
    device_name: str | None = None


class TelemetryPayload(BaseModel):
    device_id: str = Field(min_length=1, max_length=128)
    timestamp: float
    phases: list[PhaseMetrics] = Field(min_length=1, max_length=3)
    neutral_current_a: float | None = Field(default=None, ge=0)
    system: SystemStatus

    def phases_by_label(self) -> dict[str, PhaseMetrics]:
        return {p.label: p for p in self.phases}

    def total_power(self) -> float:
        return sum(p.power for p in self.phases)

    def to_api_sensors(self) -> dict[str, dict]:
        """Legacy-compatible sensors dict keyed by phase label."""
        out: dict[str, dict] = {}
        for phase in self.phases:
            out[phase.label] = {
                "voltage": phase.voltage,
                "current": phase.current,
                "power": phase.power,
                "energy": phase.energy,
                "frequency": phase.frequency,
                "pf": phase.power_factor,
            }
        return out
