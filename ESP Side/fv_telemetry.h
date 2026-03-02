/*
 * FoundryVision — fv_telemetry.h
 * Per-frame telemetry: image stats, inference results, JSON emit.
 *
 * One FV_Frame is built per inference and sent to fv_emit(), which
 * serialises it to JSON and sends it over serial and/or WiFi UDP.
 *
 * The host-side server (host/server.py) parses this JSON and feeds it
 * to the LLM and dashboard.
 */

#pragma once

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C++" {
#endif

/* ── Image stats ─────────────────────────────────────────────────────────
 * Computed from the raw int8 input tensor — what the model actually saw.
 * Useful for diagnosing bad lighting, low contrast, sensor clipping, etc.
 */
typedef struct {
    float mean_brightness;  /* 0–255: average pixel intensity               */
    float contrast;         /* 0–1: normalised std dev (0=flat, 1=extreme)  */
    float dark_ratio;       /* fraction of pixels < 50  (underexposed)      */
    float bright_ratio;     /* fraction of pixels > 200 (overexposed)       */
    int   clipped_pixels;   /* pixels at exactly 0 or 255 (saturated)       */
} FV_ImageStats;

/* ── Per-frame telemetry ─────────────────────────────────────────────────
 * Everything the host needs for narration, display, and logging.
 * Built in main_functions.cc, passed to fv_update_session() then fv_emit().
 */
typedef struct {
    /* Identity */
    uint32_t frame;         /* monotonically increasing frame counter        */
    int64_t  ts_us;         /* esp_timer_get_time() at emit                  */

    /* Inference results */
    float    person_score;      /* dequantised float [0, 1]                  */
    float    no_person_score;   /* dequantised float [0, 1]                  */
    float    inference_ms;      /* wall-clock time for interpreter->Invoke() */

    /* What the model saw */
    FV_ImageStats image;

    /* Memory */
    size_t   arena_used;    /* interpreter->arena_used_bytes()               */
    size_t   arena_total;   /* FV_ARENA_SIZE                                 */
    uint32_t free_heap;     /* esp_get_free_heap_size()                      */

    /* Session running stats (filled by fv_update_session) */
    float    session_avg_person;    /* EMA of person_score across session    */
    float    session_avg_ms;        /* EMA of inference_ms across session    */
    int      session_detections;    /* frames where person_score > 0.5       */
} FV_Frame;

/*
 * fv_compute_image_stats — analyse an int8 input tensor buffer.
 * Call this before Invoke() so stats reflect exactly what the model sees.
 * n = total element count (width * height * channels).
 */
FV_ImageStats fv_compute_image_stats(const int8_t *buf, int n);

/*
 * fv_update_session — update EMA running stats inside the frame struct.
 * Call after Invoke(), before fv_emit().
 */
void fv_update_session(FV_Frame *f);

/*
 * fv_emit — serialise frame to JSON, write to serial and/or WiFi UDP.
 * JSON schema matches what host/core/receiver.py expects.
 * One frame = one line (newline-terminated JSON object).
 */
void fv_emit(const FV_Frame *f);

#ifdef __cplusplus
}
#endif
