#
# FoundryVision — esp/main/CMakeLists.txt
# Registers the main component. Lists every source file and required IDF
# components, including the WiFi/networking stack needed by fv_wifi.c.
#

idf_component_register(
    SRCS
        # ── FoundryVision sources ──────────────────────────────────────────
        "fv_wifi.c"               # WiFi STA init + UDP socket
        "fv_telemetry.cc"         # Image stats, JSON serialisation, emit
        "main_functions.cc"       # Inference loop (setup + loop)

        # ── Upstream esp-tflite-micro (unmodified) ────────────────────────
        "detection_responder.cc"  # LED/display response to scores
        "image_provider.cc"       # Camera capture → input tensor
        "main.cc"                 # FreeRTOS task entry, calls setup/loop
        "model_settings.cc"       # kNumCols, kNumRows, kCategoryLabels
        "person_detect_model_data.cc"  # Compiled model weights (~250KB)
        "app_camera_esp.c"        # ESP camera driver wrapper
        "esp_cli.c"               # CLI inference mode (unchanged)

    INCLUDE_DIRS "."

    PRIV_REQUIRES
        # Core
        console
        spi_flash
        static_images

        # FoundryVision additions — WiFi + networking
        esp_wifi
        esp_event
        esp_netif
        nvs_flash
        lwip
)
