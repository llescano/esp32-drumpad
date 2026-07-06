idf_component_register(SRCS "edrumulus_midi.c" "usb_descriptors.c"
                    INCLUDE_DIRS "include" "."
                    REQUIRES "freertos" "esp_timer" "log" "esp_tinyusb" "edrumulus_core" "espressif__tinyusb")