/*
 * FoundryVision — fv_telemetry.cc
 * Image stats, JSON serialisation, telemetry emission.
 */

#include "fv_telemetry.h"
#include "fv_wifi.h"
#include "fv_config.h"

#include <math.h>
#include <stdio.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <esp_heap_caps.h>

static const char* TAG = FV_LOG_TAG;

// Session state (persistent across frames)
static float    s_avg_person = 0.0f;
static float    s_avg_ms     = 0.0f;
static int      s_detections = 0;
static const float EMA       = 0.1f;   // exponential moving average weight

// ─── Image stats ─────────────────────────────────────────────────────────────

FV_ImageStats fv_compute_image_stats(const int8_t* buf, int n) {
    FV_ImageStats s = {};
    long   sum    = 0;
    long   sq_sum = 0;

    for (int i = 0; i < n; i++) {
        // int8 input → shift back to uint8 for intuitive stats
        uint8_t px = (uint8_t)(buf[i] + 128);
        sum    += px;
        sq_sum += (long)px * px;
        if (px < 50)             s.dark_ratio++;
        if (px > 200)            s.bright_ratio++;
        if (px == 0 || px == 255) s.clipped_pixels++;
    }

    s.mean_brightness = (float)sum / n;
    float msq         = (float)sq_sum / n;
    // contrast = normalised std deviation
    s.contrast        = sqrtf(msq - s.mean_brightness * s.mean_brightness) / 128.0f;
    s.dark_ratio      = s.dark_ratio   / n;
    s.bright_ratio    = s.bright_ratio / n;
    return s;
}

// ─── Session stats ────────────────────────────────────────────────────────────

void fv_update_session(FV_Frame* f) {
    s_avg_person = EMA * f->person_score  + (1.0f - EMA) * s_avg_person;
    s_avg_ms     = EMA * f->inference_ms  + (1.0f - EMA) * s_avg_ms;
    if (f->person_score > 0.5f) s_detections++;

    f->session_avg_person = s_avg_person;
    f->session_avg_ms     = s_avg_ms;
    f->session_detections = s_detections;
}

// ─── JSON serialisation ───────────────────────────────────────────────────────

// Confidence bucket label — keeps the LLM prompt compact.
static const char* confidence_label(float score) {
    if (score > 0.85f) return "very_high";
    if (score > 0.65f) return "high";
    if (score > 0.45f) return "uncertain";
    if (score > 0.25f) return "low";
    return "very_low";
}

int fv_frame_to_json(const FV_Frame* f, char* out, size_t out_size) {
    return snprintf(out, out_size,
        "{"
          "\"frame\":%lu,"
          "\"ts_us\":%lld,"
          "\"scores\":{"
            "\"person\":%.4f,"
            "\"no_person\":%.4f,"
            "\"confidence\":\"%s\""
          "},"
          "\"perf\":{"
            "\"inference_ms\":%.2f,"
            "\"avg_ms\":%.2f"
          "},"
          "\"image\":{"
            "\"mean_brightness\":%.1f,"
            "\"contrast\":%.3f,"
            "\"dark_ratio\":%.3f,"
            "\"bright_ratio\":%.3f,"
            "\"clipped_pixels\":%d"
          "},"
          "\"memory\":{"
            "\"arena_used\":%u,"
            "\"arena_total\":%u,"
            "\"arena_pct\":%.1f,"
            "\"free_heap\":%lu"
          "},"
          "\"session\":{"
            "\"avg_person\":%.4f,"
            "\"avg_ms\":%.2f,"
            "\"detections\":%d,"
            "\"frames\":%lu"
          "}"
        "}\n",
        (unsigned long)f->frame,
        (long long)f->ts_us,
        f->person_score,
        f->no_person_score,
        confidence_label(f->person_score),
        f->inference_ms,
        f->session_avg_ms,
        f->image.mean_brightness,
        f->image.contrast,
        f->image.dark_ratio,
        f->image.bright_ratio,
        f->image.clipped_pixels,
        (unsigned)f->arena_used,
        (unsigned)f->arena_total,
        100.0f * f->arena_used / f->arena_total,
        (unsigned long)f->free_heap,
        f->session_avg_person,
        f->session_avg_ms,
        f->session_detections,
        (unsigned long)f->frame
    );
}

// ─── Emit ─────────────────────────────────────────────────────────────────────

void fv_emit(const FV_Frame* f) {
    char buf[1024];
    int  len = fv_frame_to_json(f, buf, sizeof(buf));
    if (len <= 0) {
        ESP_LOGE(TAG, "JSON serialisation failed");
        return;
    }

#if FV_SERIAL_ECHO
    printf("%s", buf);
#endif

    if (fv_wifi_ready()) {
        if (!fv_wifi_send(buf, (size_t)len)) {
            ESP_LOGW(TAG, "UDP send failed (frame %lu)", (unsigned long)f->frame);
        }
    }
}
