import tempfile
import unittest
from pathlib import Path

from agrisense.analysis import ImageMetrics
from agrisense.risk import TrendTracker, evaluate_risk


def metrics(yellowing: float) -> ImageMetrics:
    return ImageMetrics(640, 480, 100, 100, 40, 100 - yellowing, yellowing, yellowing)


class RiskTests(unittest.TestCase):
    def test_green_low_yellowing(self) -> None:
        result = evaluate_risk(metrics(4), humidity_percent=60, temperature_c=28, yellowing_trend=0)
        self.assertEqual(result.level, "GREEN")

    def test_red_combined_high_humidity(self) -> None:
        result = evaluate_risk(metrics(18), humidity_percent=90, temperature_c=30, yellowing_trend=1)
        self.assertEqual(result.level, "RED")

    def test_trend_uses_recent_baseline(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            tracker = TrendTracker(Path(directory) / "history.jsonl")
            tracker.append("2026-09-01T00:00:00Z", metrics(5))
            tracker.append("2026-09-01T00:05:00Z", metrics(7))
            self.assertAlmostEqual(tracker.delta(12), 6)


if __name__ == "__main__":
    unittest.main()

