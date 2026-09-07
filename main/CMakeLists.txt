idf_component_register(SRCS "esp32-edrumulus.c"
                    INCLUDE_DIRS "."
                    REQUIRES "freertos" "esp_timer" "log" "nvs_flash"
                             "edrumulus_core" "edrumulus_midi" "edrumulus_detection"
                             "edrumulus_config" "edrumulus_led" "edrumulus_input"
                             "edrumulus_console" "edrumulus_synthtest")
