/*
 * FoundryVision — fv_config.h
 * All user-configurable settings in one place.
 * Edit this file before flashing.
 */

#pragma once

// =============================================================================
// NETWORK
// =============================================================================

// WiFi credentials
#define FV_WIFI_SSID        "YOUR_WIFI_SSID"
#define FV_WIFI_PASSWORD    "YOUR_WIFI_PASSWORD"

// Host PC — IP of the machine running host/server.py
// Run: ipconfig (Windows) or ip a (Linux/Mac) to find your local IP
#define FV_HOST_IP          "192.168.1.100"
#define FV_HOST_PORT        5005            // UDP port, must match host config

// WiFi retry behaviour
#define FV_WIFI_MAX_RETRIES 10

// =============================================================================
// INFERENCE
// =============================================================================

// Frames to skip between streams (0 = stream every frame)
// Higher = less WiFi load, lower latency budget for LLM
#define FV_STREAM_EVERY_N   1

// Tensor arena size. 100KB base + scratch for optimised kernels.
#if CONFIG_NN_OPTIMIZED
  #define FV_SCRATCH_BUF_SIZE (60 * 1024)
#else
  #define FV_SCRATCH_BUF_SIZE 0
#endif
#define FV_TENSOR_ARENA_SIZE (100 * 1024 + FV_SCRATCH_BUF_SIZE)

// =============================================================================
// LOGGING
// =============================================================================

// Tag for ESP_LOG* calls
#define FV_LOG_TAG "FV"

// Print JSON to serial (USB) in addition to WiFi stream
#define FV_SERIAL_ECHO 1
