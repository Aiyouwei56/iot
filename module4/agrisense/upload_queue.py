from __future__ import annotations

from dataclasses import asdict
import json
import logging
from pathlib import Path
from typing import Protocol

from .analysis import ImageMetrics
from .risk import RiskAssessment


LOGGER = logging.getLogger(__name__)


class UploadService(Protocol):
    enabled: bool

    def store(
        self,
        jpeg_bytes: bytes,
        image_path: str,
        captured_at: str,
        metrics: ImageMetrics,
        assessment: RiskAssessment,
        temperature_c: float | None,
        humidity_percent: float | None,
    ) -> None: ...


def enqueue_upload(
    queue_dir: Path,
    local_image_path: Path,
    storage_path: str,
    captured_at: str,
    metrics: ImageMetrics,
    assessment: RiskAssessment,
    temperature_c: float | None,
    humidity_percent: float | None,
) -> Path:
    """Persist an upload job atomically so a process restart cannot lose it."""
    queue_dir.mkdir(parents=True, exist_ok=True)
    job = {
        "local_image_path": str(local_image_path.resolve()),
        "storage_path": storage_path,
        "captured_at": captured_at,
        "metrics": asdict(metrics),
        "assessment": asdict(assessment),
        "temperature_c": temperature_c,
        "humidity_percent": humidity_percent,
    }
    job_path = queue_dir / f"{local_image_path.stem}.json"
    temporary_path = job_path.with_suffix(".tmp")
    temporary_path.write_text(json.dumps(job, separators=(",", ":")), encoding="utf-8")
    temporary_path.replace(job_path)
    return job_path


def retry_pending_uploads(
    queue_dir: Path, service: UploadService, maximum: int = 10
) -> int:
    """Retry oldest jobs; keep transient failures and quarantine corrupt jobs."""
    if not service.enabled or not queue_dir.exists():
        return 0

    completed = 0
    for job_path in sorted(queue_dir.glob("*.json"))[:maximum]:
        try:
            job = json.loads(job_path.read_text(encoding="utf-8"))
            local_path = Path(job["local_image_path"])
            jpeg_bytes = local_path.read_bytes()
            metrics = ImageMetrics(**job["metrics"])
            assessment = RiskAssessment(**job["assessment"])
        except (OSError, ValueError, KeyError, TypeError, json.JSONDecodeError) as error:
            invalid_path = job_path.with_suffix(".invalid")
            job_path.replace(invalid_path)
            LOGGER.error("Quarantined invalid upload job %s: %s", job_path, error)
            continue

        try:
            service.store(
                jpeg_bytes,
                job["storage_path"],
                job["captured_at"],
                metrics,
                assessment,
                job.get("temperature_c"),
                job.get("humidity_percent"),
            )
        except Exception:
            LOGGER.exception("Pending Supabase upload still unavailable: %s", job_path)
            break

        job_path.unlink()
        completed += 1
        LOGGER.info("Recovered pending Supabase upload: %s", job["storage_path"])

    return completed
