# Module 4 - Farm Safety and Crop Health Warning

This laptop application uses a USB webcam, OpenCV HSV colour segmentation and
Module 2 MQTT climate data to produce a prototype GREEN/AMBER/RED warning.

It is not a plant-disease diagnosis system and never applies automatic
treatment. Published outputs are limited to HSV colour percentages
(`module4/yellowing`), a prototype risk score (`module4/risk`), a
GREEN/AMBER/RED status (`module4/status`) and a human-readable warning
(`module4/alert`).

## Setup

From the repository root:

```powershell
C:\Users\wei\.cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe -m venv module4\.venv
.\module4\.venv\Scripts\python.exe -m pip install -r .\module4\requirements.txt
```

Set `SUPABASE_URL` and `SUPABASE_PUBLISHABLE_KEY` in the parent workspace
`.env`; Module 4 loads that same file automatically. Optionally set
`MODULE4_CAMERA_INDEX` and capture interval; see `.env.example`.

This project defaults to camera index `1`, which is the external USB Webcam on
the current Windows laptop. Use `--camera 0` only when intentionally testing the
built-in laptop camera.

## Run with a USB webcam

```powershell
$env:PYTHONPATH = ".\module4"
.\module4\.venv\Scripts\python.exe -m agrisense
```

Use `Ctrl+C` to stop. Captures and trend history remain under ignored local
runtime folders if cloud synchronization fails.

If Supabase is temporarily unavailable, a validated capture is stored under
`module4/runtime/pending-uploads/`. Module 4 retries the oldest jobs on each
capture cycle. Storage and database writes use the image path as an idempotent
key, so retrying cannot create duplicate growth-diary rows.

Every webcam attempt, including an image rejected by validation, is saved to:

```text
module4/runtime/latest_capture.jpg
```

Open that file to see exactly what Module 4 is analysing. Valid growth-diary
captures are additionally stored with timestamps under `module4/captures/`.

For a quick visible capture test, position the plant in front of the webcam and
run:

```powershell
$env:PYTHONPATH = ".\module4"
.\module4\.venv\Scripts\python.exe -m agrisense --once --no-mqtt --no-upload
```

The command may return exit code 2 when validation rejects the picture, but the
latest camera image is still preserved at the path above for inspection.

## Safe dry-run

Analyse one local image without MQTT or Supabase:

```powershell
$env:PYTHONPATH = ".\module4"
.\module4\.venv\Scripts\python.exe -m agrisense --image C:\path\to\plant.jpg --no-mqtt --no-upload
```

## Tests

```powershell
$env:PYTHONPATH = ".\module4"
.\module4\.venv\Scripts\python.exe -m unittest discover -s .\module4\tests -v
```
