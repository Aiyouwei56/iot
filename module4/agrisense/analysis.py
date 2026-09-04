from __future__ import annotations

from dataclasses import dataclass

import cv2
import numpy as np

from .config import Settings


@dataclass(frozen=True)
class ImageMetrics:
    width: int
    height: int
    brightness_score: float
    blur_score: float
    plant_coverage_percent: float
    green_percent: float
    yellowing_percent: float
    stress_percent: float


class InvalidImageError(ValueError):
    """Raised when an image cannot support a reliable prototype analysis."""


def _percentage(part: int, whole: int) -> float:
    return 0.0 if whole <= 0 else 100.0 * part / whole


def analyze_image(frame: np.ndarray, settings: Settings) -> ImageMetrics:
    if frame is None or not isinstance(frame, np.ndarray) or frame.size == 0:
        raise InvalidImageError("Image is empty or unreadable")
    if frame.ndim != 3 or frame.shape[2] != 3:
        raise InvalidImageError("Image must contain three BGR colour channels")

    height, width = frame.shape[:2]
    if width < settings.min_width or height < settings.min_height:
        raise InvalidImageError(
            f"Resolution {width}x{height} is below {settings.min_width}x{settings.min_height}"
        )

    gray = cv2.cvtColor(frame, cv2.COLOR_BGR2GRAY)
    brightness = float(np.mean(gray))
    blur_score = float(cv2.Laplacian(gray, cv2.CV_64F).var())
    if brightness < settings.min_brightness:
        raise InvalidImageError(f"Image is too dark (brightness={brightness:.1f})")
    if brightness > settings.max_brightness:
        raise InvalidImageError(f"Image is overexposed (brightness={brightness:.1f})")
    if blur_score < settings.min_blur_score:
        raise InvalidImageError(f"Image is too blurred (score={blur_score:.1f})")

    hsv = cv2.cvtColor(frame, cv2.COLOR_BGR2HSV)
    green = cv2.inRange(hsv, np.array([35, 40, 30]), np.array([90, 255, 255]))
    yellow = cv2.inRange(hsv, np.array([18, 45, 40]), np.array([34, 255, 255]))

    kernel = np.ones((5, 5), dtype=np.uint8)
    green = cv2.morphologyEx(green, cv2.MORPH_OPEN, kernel)
    green = cv2.morphologyEx(green, cv2.MORPH_CLOSE, kernel)
    yellow = cv2.morphologyEx(yellow, cv2.MORPH_OPEN, kernel)
    yellow = cv2.morphologyEx(yellow, cv2.MORPH_CLOSE, kernel)

    plant_mask = cv2.bitwise_or(green, yellow)
    frame_pixels = width * height
    green_pixels = int(cv2.countNonZero(green))
    yellow_pixels = int(cv2.countNonZero(yellow))
    plant_pixels = int(cv2.countNonZero(plant_mask))
    coverage = _percentage(plant_pixels, frame_pixels)
    if coverage < settings.min_plant_coverage_percent:
        raise InvalidImageError(f"Plant-coloured area is too small ({coverage:.2f}%)")

    green_percent = _percentage(green_pixels, plant_pixels)
    yellowing_percent = _percentage(yellow_pixels, plant_pixels)

    # This is a colour-stress proxy, not a scientific wilting measurement.
    stress_percent = yellowing_percent
    return ImageMetrics(
        width=width,
        height=height,
        brightness_score=round(brightness, 2),
        blur_score=round(blur_score, 2),
        plant_coverage_percent=round(coverage, 2),
        green_percent=round(green_percent, 2),
        yellowing_percent=round(yellowing_percent, 2),
        stress_percent=round(stress_percent, 2),
    )

