/*
 * FoundryVision — fv_wifi.h
 * WiFi station + UDP socket to host PC.
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Connect to WiFi and open UDP socket to FV_HOST_IP:FV_HOST_PORT.
 * Blocks until connected or FV_WIFI_MAX_RETRIES exceeded.
 * Returns true on success. On failure, fv_wifi_send() becomes a no-op
 * and the firmware continues in serial-only mode.
 */
bool fv_wifi_init(void);

/*
 * Send `len` bytes of `data` to the host over UDP.
 * Fire-and-forget — UDP gives no delivery guarantee, which is fine for
 * telemetry where a dropped frame is acceptable.
 * Returns false silently if WiFi is not ready.
 */
bool fv_wifi_send(const char *data, size_t len);

/* True once WiFi is up and the UDP socket is open. */
bool fv_wifi_ready(void);

#ifdef __cplusplus
}
#endif
