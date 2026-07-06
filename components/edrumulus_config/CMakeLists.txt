idf_component_register(SRCS "edrumulus_config.c"
                    INCLUDE_DIRS "include"
                    REQUIRES "freertos" "nvs_flash" "log" "edrumulus_core")