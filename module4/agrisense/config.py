from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path
import logging
import os


LOGGER = logging.getLogger(__name__)


def env_int(name: str, default: int, minimum: int, maximum: int) -> int:
    """Return a bounded integer setting, recovering to a safe default."""
    raw = os.getenv(name)
    if raw is None:
        return default
    try:
        value = int(raw)
    except ValueError:
        LOGGER.warning("Invalid %s=%r; using %s", name, raw, default)
        return default
    if value < minimum or value > maximum:
        LOGGER.warning(
            "Out-of-range %s=%s (expected %s..%s); using %s",
            name,
            value,
            minimum,
            maximum,
            default,
        )
        return default
    return value


def env_text(name: str, default: str) -> str:
    value = os.getenv(name, default).strip()
    if value:
        return value
    LOGGER.warning("Empty %s; using %s", name, default)
    return default


def load_env_file(path: Path) -> None:
    """Load simple KEY=VALUE entries without overwriting the current process."""
    if not path.exists():
        return
    for raw_line in path.read_text(encoding="utf-8").splitlines():
        line = raw_line.strip()
        if not line or line.startswith("#") or "=" not in line:
            continue
        name, value = line.split("=", 1)
        os.environ.setdefault(name.strip(), value.strip().strip('"').strip("'"))


@dataclass(frozen=True)
class Settings:
    mqtt_host: str = "broker.hivemq.com"
    mqtt_port: int = 1883
    mqtt_base: str = "smartfarm/rsd2s3g3"
    camera_index: int = 1
    capture_interval_seconds: int = 300
    climate_max_age_seconds: int = 180
    min_width: int = 320
    min_height: int = 240
    min_brightness: float = 25.0
    max_brightness: float = 230.0
    min_blur_score: float = 45.0
    min_plant_coverage_percent: float = 1.0
    jpeg_quality: int = 85
    supabase_url: str | None = None
    supabase_key: str | None = None

    @classmethod
    def from_environment(cls, repo_root: Path) -> "Settings":
        # Prefer a project-local file, then load the shared workspace file used
        # by Node-RED. load_env_file uses setdefault, so local values win.
        load_env_file(repo_root / ".env")
        load_env_file(repo_root.parent / ".env")
        return cls(
            mqtt_host=env_text("MQTT_HOST", "broker.hivemq.com"),
            mqtt_port=env_int("MQTT_PORT", 1883, 1, 65535),
            mqtt_base=(
                env_text("MQTT_BASE", "smartfarm/rsd2s3g3").rstrip("/")
                or "smartfarm/rsd2s3g3"
            ),
            camera_index=env_int("MODULE4_CAMERA_INDEX", 1, 0, 20),
            capture_interval_seconds=env_int(
                "MODULE4_CAPTURE_INTERVAL_SECONDS", 300, 5, 86400
            ),
            climate_max_age_seconds=env_int(
                "MODULE4_CLIMATE_MAX_AGE_SECONDS", 180, 30, 86400
            ),
            supabase_url=os.getenv("SUPABASE_URL"),
            supabase_key=os.getenv("SUPABASE_PUBLISHABLE_KEY") or os.getenv("SUPABASE_ANON_KEY"),
        )
