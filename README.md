# FoundryVision

Real-time TensorFlow Lite Micro inference on an ESP32-S3, with live telemetry streamed to a local LLM that narrates the model's reasoning. Built as a teaching tool for a CCA computer vision workshop.

---

## What it does

An ESP32-S3-EYE runs a quantised person detection model at ~10fps. After each inference it streams a JSON packet — confidence scores, inference time, image characteristics, memory usage — over WiFi UDP to a host machine. A Python server receives the packets, optionally calls a local Foundry LLM every few frames to produce an educational commentary, and pushes everything to a live browser dashboard via WebSocket.

The goal is to make the model's internal state visible and legible. Students see not just "person detected" but *why* the model is confident or uncertain, and what the numbers actually mean.

---

## Architecture

```
ESP32-S3
  Camera → TFLite Micro inference
         → fv_telemetry: compute image stats + serialise JSON
         → fv_wifi: UDP → host:5005

Host machine
  UDP :5005 → receiver.py
            → llm.py     → Foundry Local (phi3.5-mini / qwen2.5 / etc.)
            → logger.py  → logs/session_YYYYMMDD.jsonl
            → server.py  → WebSocket → dashboard (http://localhost:8080)
```

---

## Repository layout

```
FoundryVision/
├── esp/
│   └── main/
│       ├── fv_config.h          # all user settings — edit before flashing
│       ├── fv_wifi.c/h          # WiFi init and UDP socket
│       ├── fv_telemetry.cc/h    # image stats, JSON serialisation, emit
│       ├── main_functions.cc    # inference loop (thin orchestration)
│       └── ...                  # upstream esp-tflite-micro files (unmodified)
├── host/
│   ├── config.py                # all host settings
│   ├── server.py                # entry point
│   ├── core/
│   │   ├── receiver.py          # UDP and serial receivers
│   │   ├── llm.py               # prompt engine + Foundry API call
│   │   └── logger.py            # JSONL session logger
│   ├── dashboard/
│   │   └── index.html           # live browser dashboard
│   └── prompts/
│       └── system.txt           # LLM system prompt
└── install/
    └── setup.ps1                # Windows one-shot installer
```

---

## Telemetry format

Each UDP packet is one JSON object followed by a newline:

```json
{
  "frame": 142,
  "ts_us": 1234567890,
  "scores": {
    "person": 0.9123,
    "no_person": 0.0877,
    "confidence": "very_high"
  },
  "perf": {
    "inference_ms": 84.3,
    "avg_ms": 86.1
  },
  "image": {
    "mean_brightness": 138.0,
    "contrast": 0.412,
    "dark_ratio": 0.09,
    "bright_ratio": 0.06,
    "clipped_pixels": 14
  },
  "memory": {
    "arena_used": 71680,
    "arena_total": 102400,
    "arena_pct": 70.0,
    "free_heap": 182416
  },
  "session": {
    "avg_person": 0.8104,
    "avg_ms": 85.7,
    "detections": 98,
    "frames": 142
  }
}
```

`confidence` is one of: `very_high` `high` `uncertain` `low` `very_low`.  
`contrast` is normalised standard deviation of pixel values: 0 = flat grey, 1 = extreme.  
`clipped_pixels` = pixels at 0 or 255 (saturated, no recoverable detail).  
All `avg_*` values use exponential moving average with α = 0.1.

---

## Requirements

**ESP board:** ESP32-S3-EYE (or any ESP32-S3 with a compatible camera module).  
The C3 does not support the esp32-camera component — use an S3.

**Host machine:** Windows 10/11, Python 3.11+, ~2 GB disk for the LLM model.

**Network:** ESP and host on the same WiFi, or USB serial as a fallback.

---

## Setup

### Host (Windows)

```powershell
powershell -ExecutionPolicy Bypass -File install\setup.ps1
```

This installs Python packages, Foundry Local, pulls the default model, detects your local IP, and creates launch scripts.

### ESP firmware

1. Edit `esp/main/fv_config.h`:
   ```c
   #define FV_WIFI_SSID      "your_network"
   #define FV_WIFI_PASSWORD  "your_password"
   #define FV_HOST_IP        "192.168.x.x"   // your laptop's IP from setup output
   ```

2. Build and flash (requires ESP-IDF v5.x):
   ```bash
   cd esp
   idf.py set-target esp32s3
   idf.py menuconfig   # BSP → ESP32-S3-EYE
   idf.py build flash
   ```

The facilitator pre-flashes all boards before the workshop. Attendees do not touch the firmware.

### Running

```powershell
# Terminal 1 — LLM server
.\start_foundry.ps1

# Terminal 2 — workshop host
.\start_server.ps1

# Browser
http://localhost:8080
```

Serial/USB fallback (no WiFi needed):

```powershell
python -m host.server --serial COM3
```

---

## Changing the LLM model

Edit `FOUNDRY_MODEL` in `host/config.py`. Any model supported by Foundry Local works. Smaller models respond faster; `phi3.5-mini-instruct` and `qwen2.5-0.5b` are reasonable defaults for narration latency under ~1s on most hardware.

To pull a different model:

```
foundry model run qwen2.5-0.5b
```

---

## Changing the prompt

The LLM's personality and focus are controlled by `host/prompts/system.txt`. Edit it to change what aspects of inference the narration emphasises — memory pressure, image quality, score trends, etc. No code changes needed.

---

## Troubleshooting

**No data on dashboard / terminal**  
Check `FV_HOST_IP` in the firmware matches your actual IP (`ipconfig`). Both devices must be on the same subnet. UDP 5005 must be allowed through Windows Firewall. Use `--serial` mode to bypass networking entirely.

**LLM shows `[LLM offline]`**  
`start_foundry.ps1` must be running in a separate terminal before starting the server. Wait for the model to finish loading before testing.

**Low confidence scores in reasonable conditions**  
The model input is 96×96 greyscale. Check `mean_brightness` in telemetry — below ~60 means the scene is underlit. `contrast` below 0.15 means flat lighting with few edges. Both hurt detection significantly.

**`idf.py build` fails**  
Confirm target is `esp32s3` (`idf.py set-target esp32s3`). Confirm BSP is set to `ESP32-S3-EYE` in menuconfig. Confirm ESP-IDF v5.x is activated in your shell.

---

## Status

v0.1 — base pipeline working. Person detection only.

Planned:
- Multi-model support (gesture, classification)
- Per-op timing from `MicroProfiler`
- Mac/Linux installer
- Workshop slide deck

---

## License

MIT. Upstream `esp-tflite-micro` files in `esp/main/` are Apache 2.0 (Espressif / Google).
