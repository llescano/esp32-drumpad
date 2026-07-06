idf_component_register(
    SRCS "edrumulus_console.c"
    INCLUDE_DIRS "include"
    REQUIRES "edrumulus_detection" "edrumulus_config" "nvs_flash" "console" "driver"
)