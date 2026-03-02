/*
 * FoundryVision — main_functions.cc
 * ─────────────────────────────────────────────────────────────────────────────
 * Inference loop. Setup and one frame of loop() per call.
 *
 * This file is intentionally thin — it owns only the TFLite lifecycle and
 * orchestrates the modules:
 *
 *   fv_config.h     ← user settings (SSID, IP, port)
 *   fv_wifi         ← WiFi init + UDP socket
 *   fv_telemetry    ← image stats, session tracking, JSON emit
 *
 * If you want to change what gets streamed, edit fv_telemetry.cc.
 * If you want to change WiFi behaviour, edit fv_wifi.c.
 * If you want to change settings, edit fv_config.h.
 */

#include "main_functions.h"
#include "fv_config.h"
#include "fv_wifi.h"
#include "fv_telemetry.h"

/* Upstream TFLite Micro headers (unmodified) */
#include "detection_responder.h"
#include "image_provider.h"
#include "model_settings.h"
#include "person_detect_model_data.h"
#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/micro/micro_log.h"
#include "tensorflow/lite/micro/micro_mutable_op_resolver.h"
#include "tensorflow/lite/schema/schema_generated.h"

#include <esp_heap_caps.h>
#include <esp_timer.h>
#include <esp_log.h>
#include <nvs_flash.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

static const char *TAG = FV_TAG;

/* ── TFLite objects (static, allocated once) ─────────────────────────────── */

namespace {
    const tflite::Model       *model       = nullptr;
    tflite::MicroInterpreter  *interpreter = nullptr;
    TfLiteTensor              *input       = nullptr;
    uint8_t                   *arena       = nullptr;
    uint32_t                   frame_n     = 0;
}

/* ── setup() ─────────────────────────────────────────────────────────────── */

void setup()
{
    ESP_LOGI(TAG, "FoundryVision v0.1");

    /* ── NVS (required by WiFi driver) ─────────────────────────────────── */
    esp_err_t nvs = nvs_flash_init();
    if (nvs == ESP_ERR_NVS_NO_FREE_PAGES || nvs == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        nvs = nvs_flash_init();
    }
    ESP_ERROR_CHECK(nvs);

    /* ── WiFi ───────────────────────────────────────────────────────────── */
    /*
     * Non-fatal: if WiFi fails (wrong credentials, host unreachable) the
     * firmware continues and streams telemetry over USB serial only.
     * FV_SERIAL_ECHO must be 1 in fv_config.h for serial to work.
     */
    if (!fv_wifi_init()) {
        ESP_LOGW(TAG, "WiFi unavailable — serial-only mode active");
    }

    /* ── Model ──────────────────────────────────────────────────────────── */
    model = tflite::GetModel(g_person_detect_model_data);
    if (model->version() != TFLITE_SCHEMA_VERSION) {
        MicroPrintf("Model schema version mismatch: got %d, need %d",
                    model->version(), TFLITE_SCHEMA_VERSION);
        return;
    }

    /* ── Tensor arena (allocated in PSRAM) ──────────────────────────────── */
    /*
     * We allocate from PSRAM (SPIRAM) rather than internal SRAM because the
     * arena needs ~100KB and internal SRAM is scarce on the S3.
     * MALLOC_CAP_8BIT is required for tensor data (byte-aligned access).
     */
    arena = (uint8_t *)heap_caps_malloc(FV_ARENA_SIZE,
                                        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!arena) {
        ESP_LOGE(TAG, "Failed to malloc %d bytes for tensor arena", FV_ARENA_SIZE);
        return;
    }
    ESP_LOGI(TAG, "Tensor arena: %d KB allocated in PSRAM", FV_ARENA_SIZE / 1024);

    /* ── Op resolver ────────────────────────────────────────────────────── */
    /*
     * Only register the ops this model actually uses.
     * Using AllOpsResolver would waste ~30KB of flash on unused kernels.
     * If you swap the model for a different one, update this list.
     */
    static tflite::MicroMutableOpResolver<5> resolver;
    resolver.AddAveragePool2D();
    resolver.AddConv2D();
    resolver.AddDepthwiseConv2D();
    resolver.AddReshape();
    resolver.AddSoftmax();

    /* ── Interpreter ────────────────────────────────────────────────────── */
    static tflite::MicroInterpreter static_interp(
        model, resolver, arena, FV_ARENA_SIZE);
    interpreter = &static_interp;

    TfLiteStatus alloc = interpreter->AllocateTensors();
    if (alloc != kTfLiteOk) {
        MicroPrintf("AllocateTensors() failed — arena may be too small");
        return;
    }

    size_t used = interpreter->arena_used_bytes();
    ESP_LOGI(TAG, "Arena usage: %u / %d bytes (%.1f%%)",
             (unsigned)used, FV_ARENA_SIZE, 100.0f * used / FV_ARENA_SIZE);

    /* ── Camera ─────────────────────────────────────────────────────────── */
    input = interpreter->input(0);

    if (InitCamera() != kTfLiteOk) {
        MicroPrintf("InitCamera() failed");
        return;
    }

    ESP_LOGI(TAG, "Ready — %dx%d greyscale, streaming every %d frame(s)",
             kNumCols, kNumRows, FV_STREAM_EVERY_N);
}

/* ── loop() ──────────────────────────────────────────────────────────────── */

void loop()
{
    /* ── 1. Capture image into input tensor ─────────────────────────────── */
    if (GetImage(kNumCols, kNumRows, kNumChannels, input->data.int8) != kTfLiteOk) {
        MicroPrintf("GetImage() failed");
        return;
    }

    /* ── 2. Image stats (reads input tensor before Invoke corrupts it) ──── */
    /*
     * We compute stats here, before Invoke(), because some TFLite kernels
     * may use the input buffer as scratch space during inference.
     * Reading it after Invoke() could give garbage values.
     */
    FV_ImageStats img = fv_compute_image_stats(
        input->data.int8, kNumCols * kNumRows * kNumChannels);

    /* ── 3. Inference ───────────────────────────────────────────────────── */
    int64_t t_start = esp_timer_get_time();

    if (interpreter->Invoke() != kTfLiteOk) {
        MicroPrintf("Invoke() failed");
        return;
    }

    int64_t elapsed_us = esp_timer_get_time() - t_start;

    /* ── 4. Dequantize output scores ─────────────────────────────────────── */
    /*
     * The model output is quantized int8. We convert back to float using
     * the output tensor's scale and zero_point parameters, which were set
     * during post-training quantization.
     *
     * Formula: float_value = (int8_value - zero_point) * scale
     */
    TfLiteTensor *output = interpreter->output(0);

    auto dequant = [&](int idx) -> float {
        return ((float)output->data.int8[idx] - output->params.zero_point)
               * output->params.scale;
    };

    float person_score    = dequant(kPersonIndex);
    float no_person_score = dequant(kNotAPersonIndex);

    /* ── 5. Build telemetry frame ────────────────────────────────────────── */
    frame_n++;

    FV_Frame f       = {};
    f.frame          = frame_n;
    f.ts_us          = esp_timer_get_time();
    f.person_score   = person_score;
    f.no_person_score= no_person_score;
    f.inference_ms   = (float)elapsed_us / 1000.0f;
    f.image          = img;
    f.arena_used     = interpreter->arena_used_bytes();
    f.arena_total    = (size_t)FV_ARENA_SIZE;
    f.free_heap      = esp_get_free_heap_size();

    fv_update_session(&f);  /* fills f.session_* fields */

    /* ── 6. Emit every N frames ──────────────────────────────────────────── */
    if (frame_n % FV_STREAM_EVERY_N == 0) {
        fv_emit(&f);
    }

    /* ── 7. Drive onboard LED / display (original upstream behaviour) ────── */
    RespondToDetection(person_score, no_person_score);

    /*
     * Yield to FreeRTOS scheduler.
     * Without this, the WiFi stack's housekeeping tasks can starve,
     * causing disconnects under continuous inference load.
     */
    vTaskDelay(1);
}
