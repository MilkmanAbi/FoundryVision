/*
 * FoundryVision — fv_wifi.c
 * WiFi STA init and UDP socket management.
 *
 * Design notes:
 *  - Uses FreeRTOS EventGroup for connection signalling (no polling).
 *  - On disconnect, automatically retries up to FV_WIFI_MAX_RETRIES times.
 *  - If all retries fail the firmware continues in serial-only mode;
 *    fv_wifi_send() becomes a harmless no-op.
 *  - UDP is intentionally stateless — no handshake, no reconnect logic needed.
 *    A dropped telemetry frame is acceptable; the host just skips it.
 */

#include "fv_wifi.h"
#include "fv_config.h"

#include <string.h>
#include <esp_wifi.h>
#include <esp_event.h>
#include <esp_netif.h>
#include <esp_log.h>
#include <nvs_flash.h>
#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>
#include <lwip/sockets.h>
#include <lwip/netdb.h>

static const char *TAG = FV_TAG;

/* ── Internal state ─────────────────────────────────────────────────────── */

static EventGroupHandle_t   s_wifi_eg;
static int                  s_sock      = -1;
static struct sockaddr_in   s_dest;
static bool                 s_ready     = false;
static int                  s_retries   = 0;

#define WIFI_CONNECTED_BIT  BIT0

/* ── Event handler ──────────────────────────────────────────────────────── */

static void on_wifi_event(void *arg, esp_event_base_t base,
                          int32_t id, void *data)
{
    if (base == WIFI_EVENT) {
        if (id == WIFI_EVENT_STA_START) {
            ESP_LOGI(TAG, "WiFi STA started, connecting to \"%s\"...", FV_WIFI_SSID);
            esp_wifi_connect();

        } else if (id == WIFI_EVENT_STA_DISCONNECTED) {
            s_ready = false;
            if (s_retries < FV_WIFI_MAX_RETRIES) {
                s_retries++;
                ESP_LOGW(TAG, "Disconnected, retry %d/%d", s_retries, FV_WIFI_MAX_RETRIES);
                esp_wifi_connect();
            } else {
                ESP_LOGE(TAG, "WiFi failed after %d retries — serial-only mode", FV_WIFI_MAX_RETRIES);
            }
        }

    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *ev = (ip_event_got_ip_t *)data;
        ESP_LOGI(TAG, "Connected — IP " IPSTR, IP2STR(&ev->ip_info.ip));
        s_retries = 0;
        xEventGroupSetBits(s_wifi_eg, WIFI_CONNECTED_BIT);
    }
}

/* ── Public API ─────────────────────────────────────────────────────────── */

bool fv_wifi_init(void)
{
    s_wifi_eg = xEventGroupCreate();

    /* netif + event loop */
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    /* WiFi driver */
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    /* Register events */
    ESP_ERROR_CHECK(esp_event_handler_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, on_wifi_event, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, on_wifi_event, NULL));

    /* Credentials */
    wifi_config_t wc = {};
    strncpy((char *)wc.sta.ssid,     FV_WIFI_SSID,     sizeof(wc.sta.ssid)     - 1);
    strncpy((char *)wc.sta.password, FV_WIFI_PASSWORD, sizeof(wc.sta.password) - 1);

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wc));
    ESP_ERROR_CHECK(esp_wifi_start());

    /* Block until connected or timeout (20 s) */
    EventBits_t bits = xEventGroupWaitBits(
        s_wifi_eg, WIFI_CONNECTED_BIT,
        pdFALSE, pdTRUE, pdMS_TO_TICKS(20000));

    if (!(bits & WIFI_CONNECTED_BIT)) {
        ESP_LOGE(TAG, "Connection timed out");
        return false;
    }

    /* UDP socket */
    s_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (s_sock < 0) {
        ESP_LOGE(TAG, "socket() failed: errno %d", errno);
        return false;
    }

    memset(&s_dest, 0, sizeof(s_dest));
    s_dest.sin_family = AF_INET;
    s_dest.sin_port   = htons(FV_HOST_PORT);
    inet_aton(FV_HOST_IP, &s_dest.sin_addr);

    s_ready = true;
    ESP_LOGI(TAG, "UDP socket open → %s:%d", FV_HOST_IP, FV_HOST_PORT);
    return true;
}

bool fv_wifi_send(const char *data, size_t len)
{
    if (!s_ready || s_sock < 0) return false;
    int n = sendto(s_sock, data, len, 0,
                   (struct sockaddr *)&s_dest, sizeof(s_dest));
    return (n == (int)len);
}

bool fv_wifi_ready(void)
{
    return s_ready;
}
