# AgriSense Smart Agriculture Monitor

BMIT2123 Internet of Things assignment prototype using an ESP32 as the simple
programmable module and a laptop as the operating-system embedded platform.

## Current architecture

```text
ESP32 sensors/actuators -> HiveMQ MQTT -> GitHub dashboard
                               |-> Node-RED weather decisions and Supabase history
Laptop USB webcam -> Python/OpenCV Module 4 -> MQTT warnings + Supabase growth diary
```

The active MQTT prefix is `smartfarm/rsd2s3g3`. See
[`docs/mqtt-contract.md`](docs/mqtt-contract.md) before changing a topic.

## Repository layout

- `firmware/smart_agriculture/`: canonical ESP32 firmware for Modules 1-3.
- `index.html`: live MQTT and Supabase dashboard.
- `simulator.html`: laptop sensor/actuator simulator.
- `node-red/`: weather-aware irrigation and Supabase bridge.
- `database/supabase_setup.sql`: idempotent database setup/migration.
- `module4/`: Laptop + USB webcam + Python/OpenCV crop-health warning app.
- `scripts/validate-scope.mjs`: JavaScript, flow, topic, and scope checks.

## Control safety

```text
LOW-WATER SAFETY LOCK > MANUAL > AUTO
```

The pump cannot be switched on while the tank is below the configured low-water
threshold. Manual commands are accepted only after the relevant actuator is put
in MANUAL mode. Returning to AUTO resumes rules.

During supervised bench testing only, the firmware flag
`ENABLE_MANUAL_PUMP_TEST_BYPASS` may temporarily permit the pump to run in
MANUAL mode while the tank sensor reports low water. The bypass is capped at
10 seconds and must be set back to `false` before the final demonstration.

## Build the ESP32 firmware

Required Arduino libraries:

- WiFiManager
- PubSubClient
- DHT sensor library

The Seeed Grove 16×2 LCD (White on Blue) JHD1802 driver is included in the
sketch and uses I2C address `0x3E`; it does not require a separate LCD package.

```powershell
arduino-cli compile --fqbn esp32:esp32:esp32 firmware\smart_agriculture
```

Before the physical demo, update soil dry/wet and water empty/full calibration
constants near the top of the sketch. Also confirm relay active level, RGB LED
common-anode/common-cathode type, and LCD address (`0x27` or `0x3F`).

## Start Node-RED

Configure `.env` in the parent workspace with Supabase values and the real farm
coordinates:

```text
WEATHER_LATITUDE=...
WEATHER_LONGITUDE=...
```

Then run:

```powershell
.\node-red\start-node-red.ps1
```

Stop any older Node-RED instance on port 1880 first. The old globally started
flow may still subscribe to the legacy `smartfarm/#` topics.

## Validate the repository

```powershell
node .\scripts\validate-scope.mjs
```

## Run Module 4 safely

Module 4 is warnings-only. It never diagnoses disease or applies automatic
treatment. Setup, webcam, dry-run and test commands are documented in
[`module4/README.md`](module4/README.md).
