idf_component_register(SRCS "edrumulus_input.c"
                    INCLUDE_DIRS "include"
                    REQUIRES "freertos" "driver" "esp_driver_gpio" "log" "edrumulus_core")