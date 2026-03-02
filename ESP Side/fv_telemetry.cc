/*
 * FoundryVision — fv_telemetry.cc
 * Image stats computation, session tracking, JSON serialisation, emit.
 *
 * JSON schema (one object per line, newline-terminated):
 * {
 *   "frame":  <uint>,
 *   "ts_us":  <int64>,
 *   "scores": { "person": <float>, "no_person": <float>, "confidence": <str> },
 *   "perf":   { "inference_ms": <float>, "avg_ms": <float> },
 *   "image":  { "mean_brightness": <float>, "contrast": <float>,
 *               "dark_ratio": <float>, "bright_ratio": <float>,
 *               "clipped_pixels": <int> },
 *   "memory": { "arena_used": <uint>, "arena_total": <uint>,
 *               "arena_pct": <float>, "free_heap": <uint> },
 *   "session":{ "avg_person": <float>, "avg_ms": <float>,
 *               "detections": <int>, "frames": <uint> }
 * }
 *
 * All field names and types are stable — the host parser depends on them.
 * Do not rename fields without updating host/core/llm.py and the dashboard.
 */

#include "fv_telemetry.h"
#include "fv_wifi.h"
#include "fv_config.h"

#include <math.h>
#include <stdio.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <esp_heap_caps.h>

static const char *TAG = FV_TAG;

/* ── Session state (persistent across frames) ───────────────────────────── */

static float s_avg_person  = 0.0f;
static float s_avg_ms      = 0.0f;
static int   s_detections  = 0;

/*
 * EMA weight for running averages.
 * α = 0.1 gives a ~10-frame smoothing window — responsive but not jittery.
 * Lower = smoother, higher = more reactive.
 */
#define EMA_ALPHA 0.1f

/* ── Image stats ────────────────────────────────────────────────────────── */

FV_ImageStats fv_compute_image_stats(const int8_t *buf, int n)
{
    /*
     * The TFLite model uses int8 inputs (range -128..127).
     * We add 128 to get back to uint8 (0..255) for human-readable stats.
     * This shift does NOT affect inference — it's for telemetry only.
     */
    FV_ImageStats s = {};
    long sum    = 0;
    long sq_sum = 0;

    for (int i = 0; i < n; i++) {
        uint8_t px = (uint8_t)((int)buf[i] + 128);
        sum    += px;
        sq_sum += (long)px * px;

        if (px < 50)             s.dark_ratio++;     /* underexposed pixel  */
        if (px > 200)            s.bright_ratio++;   /* overexposed pixel   */
        if (px == 0 || px == 255) s.clipped_pixels++;/* saturated, no info  */
    }

    s.mean_brightness = (float)sum / n;

    /*
     * contrast = σ / 128, where σ = standard deviation of pixel values.
     * Using the computational formula: σ² = E[x²] - E[x]²
     * Normalising by 128 maps the range to [0, 1].
     */
    float mean_sq = (float)sq_sum / n;
    float variance = mean_sq - s.mean_brightness * s.mean_brightness;
    s.contrast      = (variance > 0.0f) ? sqrtf(variance) / 128.0f : 0.0f;

    s.dark_ratio   = s.dark_ratio   / n;
    s.bright_ratio = s.bright_ratio / n;

    return s;
}

/* ── Session tracking ───────────────────────────────────────────────────── */

void fv_update_session(FV_Frame *f)
{
    /* Exponential moving averages — weighted toward recent frames */
    s_avg_person = EMA_ALPHA * f->person_score  + (1.0f - EMA_ALPHA) * s_avg_person;
    s_avg_ms     = EMA_ALPHA * f->inference_ms  + (1.0f - EMA_ALPHA) * s_avg_ms;

    if (f->person_score > 0.5f) s_detections++;

    f->session_avg_person  = s_avg_person;
    f->session_avg_ms      = s_avg_ms;
    f->session_detections  = s_detections;
}

/* ── Confidence label ───────────────────────────────────────────────────── */

/*
 * Maps a person_score float to a named bucket.
 * The host LLM prompt uses this string directly to vary its language.
 * Thresholds chosen empirically for this model — adjust if needed.
 */
static const char *confidence_label(float score)
{
    if (score > 0.85f) return "very_high";
    if (score > 0.65f) return "high";
    if (score > 0.45f) return "uncertain";
    if (score > 0.25f) return "low";
    return "very_low";
}

/* ── JSON serialisation ─────────────────────────────────────────────────── */

static int frame_to_json(const FV_Frame *f, char *out, size_t out_size)
{
    float arena_pct = (f->arena_total > 0)
        ? 100.0f * f->arena_used / f->arena_total
        : 0.0f;

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
        (unsigned int)f->arena_used,
        (unsigned int)f->arena_total,
        arena_pct,
        (unsigned long)f->free_heap,
        f->session_avg_person,
        f->session_avg_ms,
        f->session_detections,
        (unsigned long)f->frame
    );
}

/* ── Emit ───────────────────────────────────────────────────────────────── */

void fv_emit(const FV_Frame *f)
{
    char buf[1024];
    int len = frame_to_json(f, buf, sizeof(buf));

    if (len <= 0 || len >= (int)sizeof(buf)) {
        ESP_LOGE(TAG, "JSON overflow or serialisation error (frame %lu)",
                 (unsigned long)f->frame);
        return;
    }

#if FV_SERIAL_ECHO
    /* Always write to serial — useful with USB monitor even without WiFi */
    printf("%s", buf);
#endif

    if (fv_wifi_ready()) {
        if (!fv_wifi_send(buf, (size_t)len)) {
            /* Log at VERBOSE level — a dropped UDP frame is expected occasionally */
            ESP_LOGV(TAG, "UDP send failed frame %lu", (unsigned long)f->frame);
        }
    }
}
