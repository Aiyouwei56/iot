# Smart Farm Node-RED Bridge

This flow connects to `broker.hivemq.com:1883`, caches the latest value from
each `smartfarm/rsd2s3g3/#` MQTT topic, and writes a combined snapshot to Supabase every
15 seconds.

It also:

- updates the four rows in `module_status` every 30 seconds;
- records a dashboard actuator command after matching MQTT feedback arrives;
- stores `smartfarm/rsd2s3g3/module4/alert` messages in the `alerts` table;
- retrieves the next six hours of rain probability from Open-Meteo every 15 minutes;
- publishes a weather-aware AUTO pump decision while preserving ESP32 safety priority;
- applies retained soil, rain and humidity thresholds saved from the Dashboard;
- keeps the Supabase credential outside the exported flow.
- stores every pending cloud write in a disk-backed queue before upload;
- retries failed Supabase writes with exponential backoff;
- publishes cloud and queue status over MQTT for the web dashboard.

## Supabase outage behaviour

Pending records are stored under the ignored `runtime/context` directory. They
survive a Node-RED restart and are removed only after Supabase returns a 2xx
response. Each queued row receives a client-generated bigint primary key, so a
retry uses `on_conflict=id` and does not create a duplicate row.

Status topics:

| Status | Topic |
|---|---|
| Cloud state and last successful sync | `smartfarm/rsd2s3g3/system/cloud` |
| Number of locally queued records | `smartfarm/rsd2s3g3/system/queue` |

## Start

Open PowerShell in this folder and run:

```powershell
.\start-node-red.ps1
```

Then open <http://127.0.0.1:1880>. The flow is already loaded and starts
automatically. It does not require additional Node-RED packages.

No sensor row is inserted until at least one supported sensor MQTT message has
been received. Rows are marked `VALID` only after all seven current sensor
values have been received; otherwise they are marked `SUSPECT`.

## Weather configuration

Set the real farm coordinates before starting Node-RED:

```powershell
$env:WEATHER_LATITUDE = "3.1390"
$env:WEATHER_LONGITUDE = "101.6869"
.\start-node-red.ps1
```

The values above are only a Kuala Lumpur demonstration default. The flow fails
safe: missing, invalid, or stale weather data produces an `OFF` AUTO decision.

## Supported topics

| Purpose | Topic |
|---|---|
| Soil raw / percent | `smartfarm/rsd2s3g3/module1/soil`, `.../soil_percent` |
| Temperature / humidity / light | `smartfarm/rsd2s3g3/module2/temperature`, `.../humidity`, `.../light` |
| Water raw / percent | `smartfarm/rsd2s3g3/module3/water`, `.../water_percent` |
| Pump safety lock | `smartfarm/rsd2s3g3/module3/pump_lock` |
| Actuator feedback | `smartfarm/rsd2s3g3/module1/pump`, `.../module2/fan`, `.../module2/led` |
| Dashboard controls and modes | `smartfarm/rsd2s3g3/control/#` |
| Dashboard automation settings | `smartfarm/rsd2s3g3/config/#` |
| Weather result | `smartfarm/rsd2s3g3/weather/#` |
| Module 4 data | `smartfarm/rsd2s3g3/module4/#` |
