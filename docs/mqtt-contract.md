# AgriSense MQTT Contract

Canonical project prefix:

```text
smartfarm/rsd2s3g3
```

All components must build topics from this prefix. The public HiveMQ broker is
used for the assignment prototype only; never publish credentials or images.

## Sensors and status

| Topic suffix | Payload |
| --- | --- |
| `module1/soil` | Soil sensor raw ADC, `0..4095` |
| `module1/soil_percent` | Calibrated soil moisture, `0..100` |
| `module1/pump` | `ON` or `OFF` |
| `module1/pump/mode` | `AUTO` or `MANUAL` |
| `module2/temperature` | Celsius |
| `module2/humidity` | Percentage, `0..100` |
| `module2/light` | LDR raw ADC, `0..4095` |
| `module2/fan` | `ON` or `OFF` |
| `module2/fan/mode` | `AUTO` or `MANUAL` |
| `module2/led` | Prototype grow-light indicator, `ON` or `OFF` |
| `module2/led/mode` | `AUTO` or `MANUAL` |
| `module3/water` | Water-level brick raw ADC, `0..4095` |
| `module3/water_percent` | Calibrated level, `0..100` |
| `module3/low_water` | `TRUE` or `FALSE` |
| `module3/pump_lock` | `LOCKED` or `CLEAR` |
| `module3/pump_runtime_seconds` | Pump ON time since ESP32 restart, non-negative seconds |
| `module3/estimated_water_litres` | Prototype estimate from runtime × configured pump flow; not a flow-sensor reading |
| `module4/yellowing` | Prototype yellow-area percentage |
| `module4/risk` | Prototype risk score, `0..100` |
| `module4/status` | `GREEN`, `AMBER`, or `RED` |
| `module4/alert` | Human-readable warning |
| `module4/online` | Retained `ONLINE`/`OFFLINE` service availability |
| `system/alert` | System or safety warning |
| `system/source` | `pc-simulator` marker published immediately before simulated sensor data |

## Dashboard commands

| Topic suffix | Payload and rule |
| --- | --- |
| `control/pump/mode` | `AUTO` or `MANUAL` |
| `control/pump` | `ON` or `OFF`; accepted only in MANUAL and never bypasses low-water lock |
| `control/pump/auto` | `ON`, `OFF`, `DELAY`, or `LOCAL`; `LOCAL` activates ESP32 soil-only fallback |
| `control/fan/mode` | `AUTO` or `MANUAL` |
| `control/fan` | `ON` or `OFF`; accepted only in MANUAL |
| `control/led/mode` | `AUTO` or `MANUAL` |
| `control/led` | `ON` or `OFF`; accepted only in MANUAL |

## Weather topics

| Topic suffix | Payload |
| --- | --- |
| `weather/rain_probability` | Maximum forecast probability for the configured look-ahead window |
| `weather/rain_expected` | `TRUE` or `FALSE` |
| `weather/status` | `OK`, `STALE`, or `ERROR` |

## Runtime automation settings

The Dashboard publishes these values with `retain=true`. Node-RED and the ESP32
load the retained settings after reconnecting and use the documented defaults
until a retained value is received.

| Topic suffix | Range | Default | Consumer |
| --- | ---: | ---: | --- |
| `config/soil/pump_on_percent` | `5..60` | `30` | Node-RED |
| `config/soil/pump_off_percent` | `10..90` | `45` | Node-RED |
| `config/irrigation/pump_pulse_seconds` | `0.5..10` | `1` | ESP32 and PC Simulator |
| `config/fan/on_temperature_c` | `15..50` | `30` | ESP32 and PC Simulator |
| `config/fan/off_temperature_c` | `10..45` | `28` | ESP32 and PC Simulator |
| `config/light/dark_raw` | `0..4095` | `1200` | ESP32 and PC Simulator |
| `config/water/low_percent` | `5..50` | `20` | ESP32 and PC Simulator |
| `config/irrigation/rain_probability` | `0..100` | `65` | Node-RED |
| `config/irrigation/humidity_percent` | `50..100` | `85` | Node-RED |

The pump-off soil threshold must be higher than the pump-on threshold. The fan
ON temperature must be higher than its OFF temperature.

## Control priority

```text
LOW-WATER SAFETY LOCK > MANUAL > AUTO
```

- The safety lock always forces the pump OFF.
- MANUAL actuator commands are never overwritten by AUTO rules.
- Returning to AUTO allows automatic rules to resume.
- A fresh Node-RED decision uses soil, humidity and forecast data.
- If Node-RED or its weather data becomes stale, ESP32 falls back to local soil-moisture hysteresis.
- Local fallback keeps the low-water lock, configurable 0.5–10 second total ON-time budget and 60-second soak delay active.
- AUTO irrigation demand starts below the configured soil threshold and clears at the configured recovery threshold.
- Rain probability at or above the configured threshold, or at least 1 mm forecast in the next six hours, delays irrigation.
- AUTO watering defaults to 1 second total ON time, delivered as 0.5 s ON, 2.5 s OFF, then 0.5 s ON. A 60-second soil soak follows before re-evaluation.

Development exception: when `ENABLE_MANUAL_PUMP_TEST_BYPASS` is explicitly
enabled in the firmware, a supervised MANUAL pump test may bypass the low-water
lock for at most 10 seconds. This flag must be disabled for the final system.
