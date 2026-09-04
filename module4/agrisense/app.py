from __future__ import annotations

import argparse
from datetime import datetime, timezone
import logging
from pathlib import Path
import signal
import sys
import time

import cv2

from .analysis import InvalidImageError, analyze_image
from .config import Settings
from .risk import TrendTracker, evaluate_risk
from .services import MqttService, SupabaseService

LOGGER = logging.getLogger(__name__)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="AgriSense Module 4 prototype warning application")
    parser.add_argument("--image", type=Path, help="Analyse an image file instead of a webcam")
    parser.add_argument("--once", action="store_true", help="Capture/analyse once, then exit")
    parser.add_argument("--no-mqtt", action="store_true", help="Do not connect or publish to MQTT")
    parser.add_argument("--no-upload", action="store_true", help="Keep images locally; do not use Supabase")
    parser.add_argument("--camera", type=int, help="Override USB webcam index")
    return parser.parse_args()


def capture_frame(camera: cv2.VideoCapture | None, image_path: Path | None):
    if image_path:
        return cv2.imread(str(image_path))
    if camera is None or not camera.isOpened():
        return None
    for _ in range(3):
        ok, frame = camera.read()
        if ok and frame is not None:
            return frame
        time.sleep(0.2)
    return None


def warm_up_camera(camera: cv2.VideoCapture | None, seconds: float = 2.0) -> None:
    """Discard USB-camera startup frames that are commonly black or unstable."""
    if camera is None or not camera.isOpened():
        return
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        camera.read()
        time.sleep(0.08)


def save_latest_capture(frame, latest_path: Path, settings: Settings) -> bool:
    """Always preserve the latest webcam frame, including rejected images."""
    if frame is None:
        return False
    latest_path.parent.mkdir(parents=True, exist_ok=True)
    ok = cv2.imwrite(
        str(latest_path),
        frame,
        [cv2.IMWRITE_JPEG_QUALITY, settings.jpeg_quality],
    )
    return bool(ok)


def encode_and_save(frame, capture_dir: Path, settings: Settings, captured_at: datetime) -> tuple[bytes, str]:
    capture_dir.mkdir(parents=True, exist_ok=True)
    timestamp = captured_at.strftime("%Y%m%dT%H%M%S%fZ")
    filename = f"{timestamp}.jpg"
    local_path = capture_dir / filename
    ok, encoded = cv2.imencode(".jpg", frame, [cv2.IMWRITE_JPEG_QUALITY, settings.jpeg_quality])
    if not ok:
        raise RuntimeError("OpenCV could not encode the captured image")
    jpeg_bytes = encoded.tobytes()
    local_path.write_bytes(jpeg_bytes)
    storage_path = f"module4/{captured_at:%Y/%m/%d}/{filename}"
    return jpeg_bytes, storage_path


def main() -> int:
    args = parse_args()
    logging.basicConfig(level=logging.INFO, format="%(asctime)s %(levelname)s %(message)s")
    module_dir = Path(__file__).resolve().parents[1]
    repo_root = module_dir.parent
    settings = Settings.from_environment(repo_root)
    if args.camera is not None:
        settings = Settings(**{**settings.__dict__, "camera_index": args.camera})

    mqtt_service = MqttService(settings, enabled=not args.no_mqtt)
    supabase_service = SupabaseService(settings, enabled=not args.no_upload)
    tracker = TrendTracker(module_dir / "runtime" / "analysis-history.jsonl")
    capture_dir = module_dir / "captures"
    latest_capture_path = module_dir / "runtime" / "latest_capture.jpg"
    camera = None if args.image else cv2.VideoCapture(settings.camera_index, cv2.CAP_DSHOW)
    if camera is not None:
        # The external UVC camera returns black frames when forced to 1280x720;
        # 640x480 is its verified stable capture mode on this Windows laptop.
        camera.set(cv2.CAP_PROP_FRAME_WIDTH, 640)
        camera.set(cv2.CAP_PROP_FRAME_HEIGHT, 480)
        warm_up_camera(camera)

    LOGGER.info(
        "Module 4 starting: source=%s interval=%ss latest_capture=%s",
        args.image if args.image else f"USB camera {settings.camera_index}",
        settings.capture_interval_seconds,
        latest_capture_path,
    )
    if camera is not None and not camera.isOpened():
        LOGGER.error("USB camera %s could not be opened", settings.camera_index)

    stop_requested = False

    def request_stop(signum, frame) -> None:
        nonlocal stop_requested
        stop_requested = True

    signal.signal(signal.SIGINT, request_stop)
    signal.signal(signal.SIGTERM, request_stop)
    mqtt_service.start()

    try:
        while not stop_requested:
            captured_at = datetime.now(timezone.utc)
            frame = capture_frame(camera, args.image)
            if save_latest_capture(frame, latest_capture_path, settings):
                height, width = frame.shape[:2]
                LOGGER.info(
                    "Captured %sx%s frame; latest image saved to %s",
                    width,
                    height,
                    latest_capture_path,
                )
            else:
                LOGGER.error("No camera frame was captured or saved")
            try:
                metrics = analyze_image(frame, settings)
            except InvalidImageError as error:
                LOGGER.warning("Image rejected: %s", error)
                mqtt_service.publish_invalid_image(str(error))
                if args.once or args.image:
                    return 2
            else:
                temperature_c, humidity_percent = mqtt_service.climate()
                trend = tracker.delta(metrics.yellowing_percent)
                assessment = evaluate_risk(metrics, humidity_percent, temperature_c, trend)
                jpeg_bytes, storage_path = encode_and_save(frame, capture_dir, settings, captured_at)
                tracker.append(captured_at.isoformat(), metrics)
                mqtt_service.publish_analysis(metrics, assessment)
                LOGGER.info(
                    "Analysis status=%s risk=%.1f yellowing=%.2f%% trend=%+.2f%% humidity=%s",
                    assessment.level,
                    assessment.score,
                    metrics.yellowing_percent,
                    assessment.yellowing_trend,
                    "stale" if humidity_percent is None else f"{humidity_percent:.1f}%",
                )
                try:
                    supabase_service.store(
                        jpeg_bytes,
                        storage_path,
                        captured_at.isoformat(),
                        metrics,
                        assessment,
                        temperature_c,
                        humidity_percent,
                    )
                except Exception:
                    LOGGER.exception("Supabase sync failed; local image and analysis history were preserved")

                if args.once or args.image:
                    return 0

            deadline = time.monotonic() + mqtt_service.current_interval(settings.capture_interval_seconds)
            while not stop_requested and time.monotonic() < deadline:
                if mqtt_service.consume_capture_request():
                    LOGGER.info("Manual capture requested via MQTT")
                    break
                time.sleep(0.25)
    finally:
        mqtt_service.stop()
        if camera is not None:
            camera.release()
    return 0


if __name__ == "__main__":
    sys.exit(main())
