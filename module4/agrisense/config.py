from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path
import os


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
        load_env_file(repo_root / ".env")
        return cls(
            mqtt_host=os.getenv("MQTT_HOST", "broker.hivemq.com"),
            mqtt_port=int(os.getenv("MQTT_PORT", "1883")),
            mqtt_base=os.getenv("MQTT_BASE", "smartfarm/rsd2s3g3").rstrip("/"),
            camera_index=int(os.getenv("MODULE4_CAMERA_INDEX", "1")),
            capture_interval_seconds=max(5, int(os.getenv("MODULE4_CAPTURE_INTERVAL_SECONDS", "300"))),
            climate_max_age_seconds=max(30, int(os.getenv("MODULE4_CLIMATE_MAX_AGE_SECONDS", "180"))),
            supabase_url=os.getenv("SUPABASE_URL"),
            supabase_key=os.getenv("SUPABASE_PUBLISHABLE_KEY") or os.getenv("SUPABASE_ANON_KEY"),
        )
