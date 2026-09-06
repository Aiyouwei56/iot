# AgriSense Exception Handling and Robustness Test Checklist

Use this checklist during the physical demonstration and attach screenshots or
short video evidence to the final report. Record the actual result instead of
marking a test PASS from source-code inspection alone.

## Test environment

- ESP32 firmware version / Git commit:
- Test date and tester:
- Soil dry/wet calibration values:
- Water empty/full calibration values:
- Measured pump flow used for the estimate (L/s): 0.0015 L/s (3 s ≈ 5 mL; 10 s ≈ 15 mL)
- Evidence folder or video name:

## Fault recovery and safety tests

| ID | Test procedure | Expected result and pass criterion | Actual result / evidence | Status |
| --- | --- | --- | --- | --- |
| EH-01 | Disconnect Wi-Fi while the ESP32 is running. | LCD/buttons and low-water lock continue. AUTO irrigation changes to local soil fallback no later than the two-minute decision timeout. Wi-Fi reconnects automatically when restored. |  | ☐ |
| EH-02 | Stop Node-RED while ESP32 remains online. | After the decision timeout, the ESP32 uses `LOCAL` soil hysteresis, configured 0.5–10 second total ON time delivered in 0.5-second micro-pulses, and a 60-second soak. Low-water lock still has priority. |  | ☐ |
| EH-03 | Make Supabase temporarily unreachable while Node-RED receives MQTT data. | Node-RED stays running, `Pending sync` increases, and queued rows upload without duplicates after recovery. |  | ☐ |
| EH-04 | Disconnect and restore the browser network/MQTT connection. | Dashboard shows reconnecting/offline state and reconnects without a manual refresh. |  | ☐ |
| EH-05 | Disconnect the USB webcam, then run Module 4. | Module 4 reports a camera/capture error without crashing the ESP32 or Modules 1–3. Reconnecting and restarting only Module 4 restores capture. |  | ☐ |
| EH-06 | Make Supabase unavailable during a valid Module 4 capture, then restore it. | The image remains under `module4/captures`, a JSON job remains under `module4/runtime/pending-uploads`, and a later cycle uploads it exactly once. |  | ☐ |
| EH-07 | Temporarily set malformed Module 4 environment values such as `MQTT_PORT=abc`. | Module 4 logs a warning and loads the documented safe default instead of terminating with `ValueError`. |  | ☐ |

## Input validation and business-rule tests

| ID | Test procedure | Expected result and pass criterion | Actual result / evidence | Status |
| --- | --- | --- | --- | --- |
| IV-01 | Enter a Pump OFF threshold less than or equal to Pump ON, then save. | Dashboard rejects the settings and publishes nothing. |  | ☐ |
| IV-02 | Enter a Fan OFF temperature greater than or equal to Fan ON. | Dashboard rejects the settings and publishes nothing. |  | ☐ |
| IV-03 | Publish a non-numeric or out-of-range automation MQTT payload. | ESP32/Node-RED ignore it and retain the last valid setting. |  | ☐ |
| IV-04 | Disconnect DHT11 or force an invalid reading. | Invalid temperature/humidity is not published; a warning is produced and the main loop continues. |  | ☐ |
| IV-05 | Present a dark, overexposed, blurred or plant-free image. | Module 4 rejects the image, publishes an AMBER warning, and does not create a valid growth-diary row. |  | ☐ |
| IV-06 | Lower the water reading below the configured threshold. | Pump turns OFF immediately, lock becomes `LOCKED`, buzzer/LCD/dashboard warn, and manual ON is rejected. |  | ☐ |

## End-to-end output tests

| ID | Test procedure | Expected result and pass criterion | Actual result / evidence | Status |
| --- | --- | --- | --- | --- |
| IO-01 | Switch only the pump in MANUAL mode. | Pump relay changes; fan relay remains unchanged. MQTT feedback matches the physical state. |  | ☐ |
| IO-02 | Switch only the fan in MANUAL mode. | Fan relay changes; pump relay remains unchanged. MQTT feedback matches the physical state. |  | ☐ |
| IO-03 | Press PREV and NEXT buttons once each. | LCD moves exactly one page backward/forward with no repeated change from switch bounce. |  | ☐ |
| IO-04 | Trigger GREEN, AMBER and RED Module 4 states. | RGB shows green/yellow/red respectively; buzzer sounds for RED. |  | ☐ |
| IO-05 | Run one AUTO drip cycle with adequate tank water. | Default sequence is 0.5 s ON, 2.5 s OFF, 0.5 s ON, then a 60 s soak. Runtime increases by about 1 second and the calibrated estimate by about 0.0015 L. | Physical calibration: 3 s total ON time produced about 5 mL; 10 s produced about 15 mL. Verify the final 1-second cycle and automatic stop after reflashing. | RETEST |
| IO-06 | Leave Node-RED running for at least two snapshot intervals. | Supabase contains new sensor rows including runtime and estimated water fields; dashboard retrieves and displays them. | 2026-09-06: Node-RED wrote consecutive 15-second snapshots to the IOT Supabase project with both new fields populated. | PASS |

## Final acceptance rule

The system is ready for submission only when every critical safety test
(`EH-01`, `EH-02`, `EH-03`, `EH-06`, `IV-06`, `IO-01`, and `IO-02`) has actual
evidence and no unresolved FAIL result. A code compile or simulator result alone
does not replace physical relay, sensor, LCD, RGB and buzzer evidence.
