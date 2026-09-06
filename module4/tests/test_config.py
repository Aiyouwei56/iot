import os
from pathlib import Path
from tempfile import TemporaryDirectory
import unittest
from unittest.mock import patch

from agrisense.config import Settings


class SettingsTests(unittest.TestCase):
    def test_invalid_environment_values_recover_to_safe_defaults(self):
        invalid = {
            "MQTT_HOST": "   ",
            "MQTT_PORT": "not-a-port",
            "MQTT_BASE": "   ",
            "MODULE4_CAMERA_INDEX": "-1",
            "MODULE4_CAPTURE_INTERVAL_SECONDS": "0",
            "MODULE4_CLIMATE_MAX_AGE_SECONDS": "NaN",
        }
        with TemporaryDirectory() as directory, patch.dict(os.environ, invalid, clear=True):
            settings = Settings.from_environment(Path(directory))

        self.assertEqual(settings.mqtt_host, "broker.hivemq.com")
        self.assertEqual(settings.mqtt_port, 1883)
        self.assertEqual(settings.mqtt_base, "smartfarm/rsd2s3g3")
        self.assertEqual(settings.camera_index, 1)
        self.assertEqual(settings.capture_interval_seconds, 300)
        self.assertEqual(settings.climate_max_age_seconds, 180)

    def test_valid_environment_values_are_loaded(self):
        values = {
            "MQTT_HOST": "mqtt.example.test",
            "MQTT_PORT": "8883",
            "MQTT_BASE": "farm/team/",
            "MODULE4_CAMERA_INDEX": "2",
            "MODULE4_CAPTURE_INTERVAL_SECONDS": "60",
            "MODULE4_CLIMATE_MAX_AGE_SECONDS": "120",
        }
        with TemporaryDirectory() as directory, patch.dict(os.environ, values, clear=True):
            settings = Settings.from_environment(Path(directory))

        self.assertEqual(settings.mqtt_host, "mqtt.example.test")
        self.assertEqual(settings.mqtt_port, 8883)
        self.assertEqual(settings.mqtt_base, "farm/team")
        self.assertEqual(settings.camera_index, 2)
        self.assertEqual(settings.capture_interval_seconds, 60)
        self.assertEqual(settings.climate_max_age_seconds, 120)

    def test_shared_parent_environment_file_is_loaded(self):
        with TemporaryDirectory() as directory, patch.dict(os.environ, {}, clear=True):
            workspace = Path(directory)
            repo = workspace / "iot"
            repo.mkdir()
            (workspace / ".env").write_text(
                "SUPABASE_URL=https://project.supabase.co\n"
                "SUPABASE_PUBLISHABLE_KEY=test-key\n",
                encoding="utf-8",
            )

            settings = Settings.from_environment(repo)

        self.assertEqual(settings.supabase_url, "https://project.supabase.co")
        self.assertEqual(settings.supabase_key, "test-key")


if __name__ == "__main__":
    unittest.main()
