idf_component_register(SRCS "edrumulus_core.c"
                    INCLUDE_DIRS "include"
                    REQUIRES "freertos" "esp_timer" "nvs_flash" "log")