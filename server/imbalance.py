"""Phase current imbalance (Schieflast) detection."""
from __future__ import annotations

import math
from dataclasses import dataclass, field

from models import PhaseMetrics, TelemetryPayload

import config


@dataclass
class ImbalanceResult:
    warning: bool = False
    abs_diff_a: float = 0.0
    pct_diff: float = 0.0
    phases: dict[str, float] = field(default_factory=dict)

    def to_dict(self) -> dict:
        return {
            "warning": self.warning,
            "abs_diff_a": round(self.abs_diff_a, 3),
            "pct_diff": round(self.pct_diff, 1),
            "phases": {k: round(v, 3) for k, v in self.phases.items()},
        }


def calculate_neutral(i1: float, i2: float, i3: float) -> float:
    """Neutral current for 3-phase system (120° phase shift assumption)."""
    val = (i1**2 + i2**2 + i3**2) - (i1 * i2 + i2 * i3 + i3 * i1)
    return round(math.sqrt(max(0.0, val)), 3)


def compute_imbalance(payload: TelemetryPayload) -> ImbalanceResult:
    currents = {p.label: p.current for p in payload.phases}
    if len(currents) < 2:
        return ImbalanceResult(phases=currents)

    values = list(currents.values())
    i_max = max(values)
    i_min = min(values)
    i_avg = sum(values) / len(values)
    abs_diff = i_max - i_min
    pct_diff = (abs_diff / i_avg * 100.0) if i_avg > 0 else 0.0

    if i_avg < config.IMBALANCE_MIN_AVG_A:
        return ImbalanceResult(
            warning=False,
            abs_diff_a=abs_diff,
            pct_diff=pct_diff,
            phases=currents,
        )

    abs_exceeded = abs_diff > config.IMBALANCE_ABS_THRESHOLD_A
    pct_exceeded = pct_diff > config.IMBALANCE_PCT_THRESHOLD

    mode = config.IMBALANCE_MODE.lower()
    if mode == "both":
        warning = abs_exceeded and pct_exceeded
    elif mode == "either":
        warning = abs_exceeded or pct_exceeded
    else:
        warning = abs_exceeded and pct_exceeded

    return ImbalanceResult(
        warning=warning,
        abs_diff_a=abs_diff,
        pct_diff=pct_diff,
        phases=currents,
    )


def resolve_neutral_current(payload: TelemetryPayload) -> float | None:
    if payload.neutral_current_a is not None:
        return payload.neutral_current_a
    by_label = payload.phases_by_label()
    if all(label in by_label for label in ("L1", "L2", "L3")):
        return calculate_neutral(
            by_label["L1"].current,
            by_label["L2"].current,
            by_label["L3"].current,
        )
    return None
