import unittest

import cv2
import numpy as np

from agrisense.analysis import InvalidImageError, analyze_image
from agrisense.config import Settings


class AnalysisTests(unittest.TestCase):
    def setUp(self) -> None:
        self.settings = Settings(min_blur_score=10)

    def test_green_and_yellow_percentages(self) -> None:
        image = np.full((480, 640, 3), (20, 20, 20), dtype=np.uint8)
        cv2.rectangle(image, (80, 80), (319, 399), (0, 180, 0), -1)
        cv2.rectangle(image, (320, 80), (559, 399), (0, 220, 220), -1)
        cv2.line(image, (0, 0), (639, 479), (255, 255, 255), 3)
        metrics = analyze_image(image, self.settings)
        self.assertGreater(metrics.plant_coverage_percent, 40)
        self.assertAlmostEqual(metrics.green_percent, 50, delta=4)
        self.assertAlmostEqual(metrics.yellowing_percent, 50, delta=4)

    def test_rejects_empty_image(self) -> None:
        with self.assertRaises(InvalidImageError):
            analyze_image(None, self.settings)


if __name__ == "__main__":
    unittest.main()

