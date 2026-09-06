import time
import unittest
from types import SimpleNamespace
from unittest.mock import MagicMock, call, patch

import paho.mqtt.client as mqtt

from agrisense.analysis import ImageMetrics
from agrisense.config import Settings
from agrisense.risk import RiskAssessment
from agrisense.services import MqttService, SupabaseService


class MqttServiceTests(unittest.TestCase):
    def setUp(self):
        self.settings = Settings(climate_max_age_seconds=30)

    @patch("agrisense.services.mqtt.Client")
    def test_climate_messages_and_analysis_publications(self, client_class):
        client = client_class.return_value
        client.publish.return_value = SimpleNamespace(rc=mqtt.MQTT_ERR_SUCCESS)
        service = MqttService(self.settings)

        client_class.assert_called_once()
        self.assertEqual(client_class.call_args.args[0], mqtt.CallbackAPIVersion.VERSION2)

        service._on_message(
            client,
            None,
            SimpleNamespace(
                topic=service.topic("module2/temperature"), payload=b"29.5"
            ),
        )
        service._on_message(
            client,
            None,
            SimpleNamespace(topic=service.topic("module2/humidity"), payload=b"81"),
        )
        self.assertEqual(service.climate(), (29.5, 81.0))

        metrics = ImageMetrics(640, 480, 100, 80, 40, 75, 25)
        assessment = RiskAssessment(55, "AMBER", "Inspect the crop", 4)
        service.publish_analysis(metrics, assessment)

        self.assertEqual(
            client.publish.call_args_list,
            [
                call(service.topic("module4/yellowing"), "25.00", qos=0, retain=True),
                call(service.topic("module4/risk"), "55.0", qos=0, retain=True),
                call(service.topic("module4/status"), "AMBER", qos=0, retain=True),
                call(service.topic("module4/alert"), "Inspect the crop", qos=0, retain=True),
            ],
        )

    def test_stale_climate_and_invalid_values_are_not_used(self):
        service = MqttService(self.settings, enabled=False)
        service._temperature = (27.0, time.time() - 31)
        service._humidity = (55.0, time.time())

        service._on_message(
            None,
            None,
            SimpleNamespace(
                topic=service.topic("module2/humidity"), payload=b"125"
            ),
        )
        self.assertEqual(service.climate(), (None, 55.0))


class SupabaseServiceTests(unittest.TestCase):
    @patch("agrisense.services.create_client")
    def test_image_and_growth_diary_record_are_written(self, create_client):
        client = create_client.return_value
        bucket = client.storage.from_.return_value
        table = client.table.return_value
        settings = Settings(
            supabase_url="https://example.supabase.co", supabase_key="test-key"
        )
        service = SupabaseService(settings)
        metrics = ImageMetrics(640, 480, 110, 90, 42, 82, 18)
        assessment = RiskAssessment(46, "AMBER", "Inspect the crop", 2.5)

        service.store(
            b"jpeg-data",
            "module4/2026/09/02/capture.jpg",
            "2026-09-02T04:00:00+00:00",
            metrics,
            assessment,
            30.0,
            80.0,
        )

        client.storage.from_.assert_called_once_with("crop-images")
        bucket.upload.assert_called_once_with(
            path="module4/2026/09/02/capture.jpg",
            file=b"jpeg-data",
            file_options={"content-type": "image/jpeg", "upsert": "true"},
        )
        client.table.assert_called_once_with("crop_health_records")
        record = table.upsert.call_args.args[0]
        self.assertEqual(table.upsert.call_args.kwargs["on_conflict"], "image_path")
        self.assertEqual(record["risk_level"], "AMBER")
        self.assertEqual(record["yellowing_percent"], 18)
        self.assertEqual(record["humidity_percent"], 80.0)
        self.assertFalse(record["metadata"]["diagnostic_claim"])
        table.upsert.return_value.execute.assert_called_once_with()


if __name__ == "__main__":
    unittest.main()
