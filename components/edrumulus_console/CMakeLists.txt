idf_component_register(
    SRCS "edrumulus_console.c"
    INCLUDE_DIRS "include"
    REQUIRES "edrumulus_detection" "edrumulus_config" "edrumulus_synthtest" "nvs_flash" "console" "driver"
)