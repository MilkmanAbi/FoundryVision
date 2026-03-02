/*
 * FoundryVision — main_functions.cc
 * Inference loop. WiFi, telemetry, and image stats are in their own modules.
 * This file should stay thin — it just orchestrates.
 */

#include "main_functions.h"
#include "fv_config.h"
#include "fv_wifi.h"
#include "fv_telemetry.h"

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

static const char* TAG = FV_LOG_TAG;

// ─── Static TFLite objects ────────────────────────────────────────────────────
namespace {
    const tflite::Model*      model       = nullptr;
    tflite::MicroInterpreter* interpreter = nullptr;
    TfLiteTensor*             input       = nullptr;
    uint8_t*                  tensor_arena = nullptr;

    uint32_t frame_counter = 0;
}

// ─── Setup ───────────────────────────────────────────────────────────────────

void setup() {
    ESP_LOGI(TAG, "FoundryVision v0.1 — ESP32-S3");

    // NVS required for WiFi
    esp_err_t nvs_ret = nvs_flash_init();
    if (nvs_ret == ESP_ERR_NVS_NO_FREE_PAGES ||
        nvs_ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        nvs_ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(nvs_ret);

    // WiFi — non-fatal if it fails; we still stream over serial
    if (!fv_wifi_init()) {
        ESP_LOGW(TAG, "WiFi unavailable — serial-only mode");
    }

    // Model
    model = tflite::GetModel(g_person_detect_model_data);
    if (model->version() != TFLITE_SCHEMA_VERSION) {
        MicroPrintf("Model schema mismatch (%d vs %d)",
                    model->version(), TFLITE_SCHEMA_VERSION);
        return;
    }

    // Tensor arena in PSRAM
    tensor_arena = (uint8_t*)heap_caps_malloc(
        FV_TENSOR_ARENA_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!tensor_arena) {
        ESP_LOGE(TAG, "Failed to allocate %d byte tensor arena", FV_TENSOR_ARENA_SIZE);
        return;
    }
    ESP_LOGI(TAG, "Tensor arena: %d KB in PSRAM", FV_TENSOR_ARENA_SIZE / 1024);

    // Op resolver — only ops this model uses
    static tflite::MicroMutableOpResolver<5> resolver;
    resolver.AddAveragePool2D();
    resolver.AddConv2D();
    resolver.AddDepthwiseConv2D();
    resolver.AddReshape();
    resolver.AddSoftmax();

    // Interpreter
    static tflite::MicroInterpreter static_interpreter(
        model, resolver, tensor_arena, FV_TENSOR_ARENA_SIZE);
    interpreter = &static_interpreter;

    if (interpreter->AllocateTensors() != kTfLiteOk) {
        MicroPrintf("AllocateTensors() failed");
        return;
    }

    size_t used = interpreter->arena_used_bytes();
    ESP_LOGI(TAG, "Arena: %u / %d bytes (%.1f%%)",
             (unsigned)used, FV_TENSOR_ARENA_SIZE,
             100.0f * used / FV_TENSOR_ARENA_SIZE);

    input = interpreter->input(0);

    if (InitCamera() != kTfLiteOk) {
        MicroPrintf("InitCamera() failed");
        return;
    }

    ESP_LOGI(TAG, "Ready. Streaming every %d frame(s).", FV_STREAM_EVERY_N);
}

// ─── Loop ────────────────────────────────────────────────────────────────────

void loop() {
    // Capture frame into input tensor
    if (GetImage(kNumCols, kNumRows, kNumChannels, input->data.int8) != kTfLiteOk) {
        MicroPrintf("GetImage() failed");
        return;
    }

    // Compute image stats before inference (these read the input tensor)
    FV_ImageStats img_stats = fv_compute_image_stats(
        input->data.int8, kNumCols * kNumRows * kNumChannels);

    // Timed inference
    int64_t t0 = esp_timer_get_time();
    if (interpreter->Invoke() != kTfLiteOk) {
        MicroPrintf("Invoke() failed");
        return;
    }
    int64_t elapsed_us = esp_timer_get_time() - t0;

    // Dequantize output scores
    TfLiteTensor* output = interpreter->output(0);
    auto dequant = [&](int idx) -> float {
        return (output->data.int8[idx] - output->params.zero_point)
               * output->params.scale;
    };
    float person_score    = dequant(kPersonIndex);
    float no_person_score = dequant(kNotAPersonIndex);

    frame_counter++;

    // Build telemetry frame
    FV_Frame f = {};
    f.frame          = frame_counter;
    f.ts_us          = esp_timer_get_time();
    f.person_score   = person_score;
    f.no_person_score= no_person_score;
    f.inference_ms   = elapsed_us / 1000.0f;
    f.image          = img_stats;
    f.arena_used     = interpreter->arena_used_bytes();
    f.arena_total    = FV_TENSOR_ARENA_SIZE;
    f.free_heap      = esp_get_free_heap_size();

    fv_update_session(&f);

    // Emit every N frames
    if (frame_counter % FV_STREAM_EVERY_N == 0) {
        fv_emit(&f);
    }

    // Drive onboard LED / display
    RespondToDetection(person_score, no_person_score);

    vTaskDelay(1);
}
