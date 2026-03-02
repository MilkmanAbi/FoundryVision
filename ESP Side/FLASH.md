# FoundryVision — ESP Firmware

Person detection on ESP32-S3-EYE with verbose telemetry streamed over WiFi UDP to the host server.

---

## Before flashing

Edit **`main/fv_config.h`** — this is the only file you need to touch:

```c
#define FV_WIFI_SSID      "your_network"
#define FV_WIFI_PASSWORD  "your_password"
#define FV_HOST_IP        "192.168.x.x"   // laptop running host/server.py
#define FV_HOST_PORT      5005            // must match host/config.py
```

Find your laptop's IP: `ipconfig` (Windows) — look for the IPv4 address under your WiFi adapter.

---

## Requirements

- [ESP-IDF v5.x](https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/get-started/)
- ESP32-S3-EYE board (or compatible ESP32-S3 + camera)
- USB-C cable

---

## Build and flash

```bash
# 1. Activate ESP-IDF environment (adjust path if needed)
. ~/esp/esp-idf/export.sh          # Linux/Mac
# or: C:\esp-idf\export.bat        # Windows

# 2. Set target
idf.py set-target esp32s3

# 3. Configure BSP (one-time)
idf.py menuconfig
# Navigate to: Application Configuration → BSP Support → ESP32-S3-EYE
# Save and exit

# 4. Build
idf.py build

# 5. Flash (replace PORT with your serial port)
idf.py -p /dev/ttyUSB0 flash      # Linux
idf.py -p COM3 flash               # Windows

# 6. Monitor (optional — see JSON stream on serial)
idf.py -p COM3 monitor
```

Combined build + flash + monitor:
```bash
idf.py -p COM3 flash monitor
```

---

## What you should see

Once flashed and connected to WiFi, the serial monitor will show JSON on every frame:

```json
{"frame":1,"ts_us":1234567,"scores":{"person":0.0312,"no_person":0.9688,"confidence":"very_low"},"perf":{"inference_ms":84.3,"avg_ms":84.3},"image":{"mean_brightness":142.0,"contrast":0.412,"dark_ratio":0.09,"bright_ratio":0.06,"clipped_pixels":14},"memory":{"arena_used":71680,"arena_total":102400,"arena_pct":70.0,"free_heap":182416},"session":{"avg_person":0.0031,"avg_ms":84.3,"detections":0,"frames":1}}
```

The host server at `http://localhost:8080` will show a live dashboard once it's running.

---

## Troubleshooting

**WiFi not connecting**  
Double-check SSID and password in `fv_config.h`. Both devices must be on the same subnet. After editing, do a full rebuild: `idf.py fullclean && idf.py build flash`.

**`AllocateTensors() failed`**  
Increase `FV_ARENA_SIZE` in `fv_config.h`. The default 100KB is sufficient for the person detection model; if you swap models this may need adjusting.

**No data on host server despite WiFi connected**  
Confirm `FV_HOST_IP` is the laptop's current IP (it can change between sessions). Confirm `FV_HOST_PORT` matches `UDP_PORT` in `host/config.py`. Check Windows Firewall isn't blocking UDP on that port.

**Build error: `static_images` not found**  
Run from inside the `esp/` directory, not the repo root. The `static_images` component is referenced relative to `esp/`.

**Camera init failed**  
Confirm BSP is set to ESP32-S3-EYE in menuconfig. Other S3 boards with different camera pinouts need a custom camera config.
