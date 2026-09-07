idf_component_register(SRCS "edrumulus_led.c"
                    INCLUDE_DIRS "include"
                    REQUIRES "freertos" "driver" "esp_driver_gpio" "led_strip" "log" "edrumulus_core")