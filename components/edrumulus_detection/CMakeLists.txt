idf_component_register(SRCS "edrumulus_detection.c"
                    INCLUDE_DIRS "include"
                    REQUIRES "freertos" "esp_timer" "esp_adc" "log" "edrumulus_core" "edrumulus_config")