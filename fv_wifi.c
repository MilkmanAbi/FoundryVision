/*
 * FoundryVision — fv_wifi.c
 * WiFi station init and UDP socket to host PC.
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

static const char* TAG = FV_LOG_TAG;

static EventGroupHandle_t s_wifi_events;
#define CONNECTED_BIT BIT0

static int         s_sock     = -1;
static struct sockaddr_in s_host;
static bool        s_ready    = false;
static int         s_retries  = 0;

// ─── Event handler ───────────────────────────────────────────────────────────

static void wifi_handler(void* arg, esp_event_base_t base,
                         int32_t id, void* data) {
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();

    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_retries < FV_WIFI_MAX_RETRIES) {
            s_retries++;
            ESP_LOGW(TAG, "WiFi disconnected, retry %d/%d", s_retries, FV_WIFI_MAX_RETRIES);
            esp_wifi_connect();
        } else {
            ESP_LOGE(TAG, "WiFi failed after %d retries", FV_WIFI_MAX_RETRIES);
        }

    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* ev = (ip_event_got_ip_t*)data;
        ESP_LOGI(TAG, "WiFi connected — " IPSTR, IP2STR(&ev->ip_info.ip));
        s_retries = 0;
        xEventGroupSetBits(s_wifi_events, CONNECTED_BIT);
    }
}

// ─── Public API ──────────────────────────────────────────────────────────────

bool fv_wifi_init(void) {
    s_wifi_events = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT,   IP_EVENT_STA_GOT_IP, wifi_handler, NULL));

    wifi_config_t wc = {};
    strncpy((char*)wc.sta.ssid,     FV_WIFI_SSID,     sizeof(wc.sta.ssid));
    strncpy((char*)wc.sta.password, FV_WIFI_PASSWORD, sizeof(wc.sta.password));

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wc));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "Connecting to \"%s\" ...", FV_WIFI_SSID);

    EventBits_t bits = xEventGroupWaitBits(
        s_wifi_events, CONNECTED_BIT, false, true,
        pdMS_TO_TICKS(20000));

    if (!(bits & CONNECTED_BIT)) {
        ESP_LOGE(TAG, "WiFi connection timed out");
        return false;
    }

    // Open UDP socket
    s_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (s_sock < 0) {
        ESP_LOGE(TAG, "Socket creation failed");
        return false;
    }

    memset(&s_host, 0, sizeof(s_host));
    s_host.sin_family = AF_INET;
    s_host.sin_port   = htons(FV_HOST_PORT);
    inet_aton(FV_HOST_IP, &s_host.sin_addr);

    s_ready = true;
    ESP_LOGI(TAG, "Streaming → %s:%d", FV_HOST_IP, FV_HOST_PORT);
    return true;
}

bool fv_wifi_send(const char* data, size_t len) {
    if (!s_ready || s_sock < 0) return false;
    int sent = sendto(s_sock, data, len, 0,
                      (struct sockaddr*)&s_host, sizeof(s_host));
    return sent == (int)len;
}

bool fv_wifi_ready(void) {
    return s_ready;
}
