from pathlib import Path
from tempfile import TemporaryDirectory
import unittest
from unittest.mock import MagicMock

from agrisense.analysis import ImageMetrics
from agrisense.risk import RiskAssessment
from agrisense.upload_queue import enqueue_upload, retry_pending_uploads


class UploadQueueTests(unittest.TestCase):
    def setUp(self):
        self.metrics = ImageMetrics(640, 480, 110, 90, 42, 82, 18)
        self.assessment = RiskAssessment(46, "AMBER", "Inspect the crop", 2.5)

    def test_pending_upload_survives_and_is_removed_after_success(self):
        with TemporaryDirectory() as directory:
            root = Path(directory)
            image_path = root / "capture.jpg"
            image_path.write_bytes(b"jpeg-data")
            queue_dir = root / "queue"
            enqueue_upload(
                queue_dir,
                image_path,
                "module4/2026/09/02/capture.jpg",
                "2026-09-02T04:00:00+00:00",
                self.metrics,
                self.assessment,
                30.0,
                80.0,
            )

            service = MagicMock()
            service.enabled = True
            self.assertEqual(retry_pending_uploads(queue_dir, service), 1)
            self.assertEqual(list(queue_dir.glob("*.json")), [])
            self.assertEqual(service.store.call_args.args[0], b"jpeg-data")
            self.assertEqual(
                service.store.call_args.args[1], "module4/2026/09/02/capture.jpg"
            )

    def test_transient_failure_keeps_job_for_later_retry(self):
        with TemporaryDirectory() as directory:
            root = Path(directory)
            image_path = root / "capture.jpg"
            image_path.write_bytes(b"jpeg-data")
            queue_dir = root / "queue"
            enqueue_upload(
                queue_dir,
                image_path,
                "module4/2026/09/02/capture.jpg",
                "2026-09-02T04:00:00+00:00",
                self.metrics,
                self.assessment,
                None,
                None,
            )

            service = MagicMock()
            service.enabled = True
            service.store.side_effect = RuntimeError("network unavailable")
            self.assertEqual(retry_pending_uploads(queue_dir, service), 0)
            self.assertEqual(len(list(queue_dir.glob("*.json"))), 1)


if __name__ == "__main__":
    unittest.main()
