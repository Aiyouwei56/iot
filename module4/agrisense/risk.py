from __future__ import annotations

from dataclasses import dataclass
from collections import deque
import json
from pathlib import Path

from .analysis import ImageMetrics


@dataclass(frozen=True)
class RiskAssessment:
    score: float
    level: str
    alert: str
    yellowing_trend: float


class TrendTracker:
    def __init__(self, history_path: Path, max_samples: int = 12) -> None:
        self.history_path = history_path
        self.values: deque[float] = deque(maxlen=max_samples)
        if history_path.exists():
            for line in history_path.read_text(encoding="utf-8").splitlines()[-max_samples:]:
                try:
                    value = float(json.loads(line)["yellowing_percent"])
                    self.values.append(value)
                except (ValueError, KeyError, TypeError, json.JSONDecodeError):
                    continue

    def delta(self, current: float) -> float:
        if not self.values:
            return 0.0
        baseline_count = min(3, len(self.values))
        baseline = sum(list(self.values)[-baseline_count:]) / baseline_count
        return round(current - baseline, 2)

    def append(self, timestamp: str, metrics: ImageMetrics) -> None:
        self.history_path.parent.mkdir(parents=True, exist_ok=True)
        row = {
            "captured_at": timestamp,
            "yellowing_percent": metrics.yellowing_percent,
            "green_percent": metrics.green_percent,
        }
        with self.history_path.open("a", encoding="utf-8") as stream:
            stream.write(json.dumps(row, separators=(",", ":")) + "\n")
        self.values.append(metrics.yellowing_percent)


def evaluate_risk(
    metrics: ImageMetrics,
    humidity_percent: float | None,
    temperature_c: float | None,
    yellowing_trend: float,
) -> RiskAssessment:
    yellow_component = min(60.0, metrics.yellowing_percent * 2.0)
    trend_component = min(20.0, max(0.0, yellowing_trend) * 4.0)
    humidity_component = 0.0
    if humidity_percent is not None:
        if humidity_percent >= 85:
            humidity_component = 20.0
        elif humidity_percent >= 75:
            humidity_component = 10.0
    heat_component = 10.0 if temperature_c is not None and temperature_c >= 35 else 0.0
    score = round(min(100.0, yellow_component + trend_component + humidity_component + heat_component), 1)

    combined_humidity_risk = (
        humidity_percent is not None
        and humidity_percent >= 85
        and metrics.yellowing_percent >= 15
    )
    if score >= 65 or metrics.yellowing_percent >= 30 or combined_humidity_risk:
        level = "RED"
        alert = "High prototype crop-health risk - inspect plants promptly"
    elif score >= 30 or metrics.yellowing_percent >= 10 or yellowing_trend >= 3:
        level = "AMBER"
        alert = "Prototype crop-health warning - inspect trend and plant condition"
    else:
        level = "GREEN"
        alert = "No elevated prototype crop-health warning"

    return RiskAssessment(score=score, level=level, alert=alert, yellowing_trend=yellowing_trend)

