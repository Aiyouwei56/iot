from __future__ import annotations

import logging
import threading
import time
import uuid

import paho.mqtt.client as mqtt
from supabase import Client, create_client

from .analysis import ImageMetrics
from .config import Settings
from .risk import RiskAssessment

LOGGER = logging.getLogger(__name__)


class MqttService:
    MIN_CAPTURE_INTERVAL_SECONDS = 15

    def __init__(self, settings: Settings, enabled: bool = True) -> None:
        self.settings = settings
        self.enabled = enabled
        self.connected = False
        self._lock = threading.Lock()
        self._temperature: tuple[float, float] | None = None
        self._humidity: tuple[float, float] | None = None
        self._interval_override: int | None = None
        self._capture_requested = threading.Event()
        self._connected_event = threading.Event()
        self.client: mqtt.Client | None = None

        if enabled:
            client_id = f"AgriSense-Module4-{uuid.uuid4().hex[:10]}"
            self.client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2, client_id=client_id)
            self.client.on_connect = self._on_connect
            self.client.on_disconnect = self._on_disconnect
            self.client.on_message = self._on_message

    def topic(self, suffix: str) -> str:
        return f"{self.settings.mqtt_base}/{suffix}"

    def start(self) -> None:
        if not self.enabled or self.client is None:
            return
        self.client.connect_async(self.settings.mqtt_host, self.settings.mqtt_port, 60)
        self.client.loop_start()
        if not self._connected_event.wait(timeout=8):
            LOGGER.warning("MQTT connection was not ready within 8 seconds")

    def stop(self) -> None:
        if not self.enabled or self.client is None:
            return
        self.client.disconnect()
        self.client.loop_stop()

    def _on_connect(self, client, userdata, flags, reason_code, properties) -> None:
        self.connected = not reason_code.is_failure
        if not self.connected:
            LOGGER.error("MQTT connection failed: %s", reason_code)
            return
        self._connected_event.set()
        client.subscribe(self.topic("module2/temperature"))
        client.subscribe(self.topic("module2/humidity"))
        client.subscribe(self.topic("control/module4/capture"))
        client.subscribe(self.topic("config/module4/capture_interval_seconds"))
        LOGGER.info("MQTT connected; climate and capture-control topics subscribed")
        self._publish(
            "module4/capture_interval_seconds",
            str(self.current_interval(self.settings.capture_interval_seconds)),
        )

    def _on_disconnect(self, client, userdata, disconnect_flags, reason_code, properties) -> None:
        self.connected = False
        if reason_code.is_failure:
            LOGGER.warning("Unexpected MQTT disconnect: %s", reason_code)

    def _on_message(self, client, userdata, message) -> None:
        try:
            payload = message.payload.decode("utf-8").strip()
        except UnicodeDecodeError:
            LOGGER.warning("Ignored undecodable payload on %s", message.topic)
            return

        if message.topic == self.topic("control/module4/capture"):
            self._capture_requested.set()
            return

        if message.topic == self.topic("config/module4/capture_interval_seconds"):
            try:
                seconds = max(self.MIN_CAPTURE_INTERVAL_SECONDS, int(float(payload)))
            except ValueError:
                LOGGER.warning("Ignored invalid capture interval payload: %s", payload)
                return
            with self._lock:
                self._interval_override = seconds
            LOGGER.info("Capture interval updated via MQTT to %ss", seconds)
            self._publish("module4/capture_interval_seconds", str(seconds))
            return

        try:
            value = float(payload)
        except ValueError:
            LOGGER.warning("Ignored invalid climate payload on %s", message.topic)
            return

        now = time.time()
        with self._lock:
            if message.topic == self.topic("module2/temperature") and -40 <= value <= 85:
                self._temperature = (value, now)
            elif message.topic == self.topic("module2/humidity") and 0 <= value <= 100:
                self._humidity = (value, now)

    def climate(self) -> tuple[float | None, float | None]:
        cutoff = time.time() - self.settings.climate_max_age_seconds
        with self._lock:
            temperature = self._temperature[0] if self._temperature and self._temperature[1] >= cutoff else None
            humidity = self._humidity[0] if self._humidity and self._humidity[1] >= cutoff else None
        return temperature, humidity

    def current_interval(self, default_seconds: int) -> int:
        with self._lock:
            return self._interval_override if self._interval_override is not None else default_seconds

    def consume_capture_request(self) -> bool:
        if self._capture_requested.is_set():
            self._capture_requested.clear()
            return True
        return False

    def publish_invalid_image(self, message: str) -> None:
        self._publish("module4/status", "AMBER")
        self._publish("module4/alert", f"Camera/image validation warning: {message}")

    def publish_analysis(self, metrics: ImageMetrics, assessment: RiskAssessment) -> None:
        self._publish("module4/yellowing", f"{metrics.yellowing_percent:.2f}")
        self._publish("module4/risk", f"{assessment.score:.1f}")
        self._publish("module4/status", assessment.level)
        self._publish("module4/alert", assessment.alert)

    def _publish(self, suffix: str, payload: str) -> None:
        if not self.enabled or self.client is None:
            LOGGER.info("MQTT dry-run %s=%s", suffix, payload)
            return
        info = self.client.publish(self.topic(suffix), payload, qos=0, retain=True)
        if info.rc != mqtt.MQTT_ERR_SUCCESS:
            LOGGER.warning("MQTT publish queued/failed for %s (rc=%s)", suffix, info.rc)


class SupabaseService:
    def __init__(self, settings: Settings, enabled: bool = True) -> None:
        self.enabled = enabled and bool(settings.supabase_url and settings.supabase_key)
        self.client: Client | None = None
        if self.enabled:
            self.client = create_client(settings.supabase_url, settings.supabase_key)

    def store(
        self,
        jpeg_bytes: bytes,
        image_path: str,
        captured_at: str,
        metrics: ImageMetrics,
        assessment: RiskAssessment,
        temperature_c: float | None,
        humidity_percent: float | None,
    ) -> None:
        if not self.enabled or self.client is None:
            LOGGER.info("Supabase disabled; retained local growth-diary image only")
            return

        self.client.storage.from_("crop-images").upload(
            path=image_path,
            file=jpeg_bytes,
            file_options={"content-type": "image/jpeg", "upsert": "false"},
        )
        record = {
            "image_path": image_path,
            "green_percent": metrics.green_percent,
            "yellowing_percent": metrics.yellowing_percent,
            "brightness_score": metrics.brightness_score,
            "blur_score": metrics.blur_score,
            "temperature_c": temperature_c,
            "humidity_percent": humidity_percent,
            "risk_score": assessment.score,
            "risk_level": assessment.level,
            "camera_status": "HEALTHY",
            "analysis_status": "VALID",
            "alert_message": assessment.alert,
            "metadata": {
                "plant_coverage_percent": metrics.plant_coverage_percent,
                "yellowing_trend": assessment.yellowing_trend,
                "method": "prototype_hsv_colour_warning",
                "diagnostic_claim": False,
            },
            "captured_at": captured_at,
        }
        self.client.table("crop_health_records").insert(record).execute()
        LOGGER.info("Supabase growth-diary upload completed: %s", image_path)
