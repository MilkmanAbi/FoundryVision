/*
 * FoundryVision — fv_config.h
 * ─────────────────────────────────────────────────────────────────────────────
 * The only file you need to edit before flashing.
 * Everything else is automatic.
 */

#pragma once

// =============================================================================
// 1. WiFi credentials
// =============================================================================
#define FV_WIFI_SSID        "YOUR_WIFI_SSID"
#define FV_WIFI_PASSWORD    "YOUR_WIFI_PASSWORD"

// =============================================================================
// 2. Host machine (the laptop running host/server.py)
//    Find your IP:  ipconfig (Windows)  |  ip a (Linux/Mac)
//    Must be on the same WiFi network as the ESP.
// =============================================================================
#define FV_HOST_IP          "192.168.1.100"
#define FV_HOST_PORT        5005            // match host/config.py UDP_PORT

// =============================================================================
// 3. Behaviour (safe to leave as-is)
// =============================================================================

// Stream every N frames. 1 = every frame. Increase if WiFi is congested.
#define FV_STREAM_EVERY_N   1

// Print JSON to USB serial as well as WiFi (useful for debugging without WiFi)
#define FV_SERIAL_ECHO      1

// Max WiFi connection attempts before giving up and falling back to serial-only
#define FV_WIFI_MAX_RETRIES 10

// =============================================================================
// 4. Tensor arena — tuned for ESP32-S3 + PSRAM
// =============================================================================
#if CONFIG_NN_OPTIMIZED
  #define FV_SCRATCH_SIZE (60 * 1024)
#else
  #define FV_SCRATCH_SIZE 0
#endif
#define FV_ARENA_SIZE (100 * 1024 + FV_SCRATCH_SIZE)

// =============================================================================
// Internal — don't touch
// =============================================================================
#define FV_TAG "FV"
