# CMakeLists.txt para ejemplos de ESP32 E-Drum

idf_component_register(
    SRCS "phase1_validation_example.c"
    INCLUDE_DIRS "."
    REQUIRES "edrumulus_detection" "esp_timer" "freertos"
)

# Configuración específica para ejemplos
target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wall
    -Wextra
    -Wno-unused-parameter
)

# Definir macros para ejemplos
target_compile_definitions(${COMPONENT_LIB} PRIVATE
    EDRUMULUS_EXAMPLE_BUILD=1
    EDRUMULUS_VALIDATION_ENABLED=1
)