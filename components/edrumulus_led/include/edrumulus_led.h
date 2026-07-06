/**
 * @file edrumulus_led.h
 * @brief ESP32 E-Drum Trigger System - LED Component
 * 
 * Status LED control using WS2812 addressable RGB LED
 */

#ifndef EDRUMULUS_LED_H
#define EDRUMULUS_LED_H

#include "esp_err.h"
#include "freertos/FreeRTOS.h"

#ifdef __cplusplus
extern "C" {
#endif

// GPIO Pin Definition for ESP32-S3 (Waveshare S3 Zero: WS2812 on GPIO21)
#define EDRUMULUS_GPIO_LED    21

/**
 * @brief LED configuration structure
 */
typedef struct {
    uint8_t brightness;              ///< LED brightness (0-255)
    bool enable_status_indication;   ///< Enable status indication
} edrumulus_led_config_t;

/**
 * @brief LED status colors
 */
typedef enum {
    EDRUMULUS_LED_OFF,               ///< LED off
    EDRUMULUS_LED_GREEN,             ///< Normal operation (green)
    EDRUMULUS_LED_BLUE,              ///< MIDI connected (blue)
    EDRUMULUS_LED_RED,               ///< Error state (red)
    EDRUMULUS_LED_YELLOW,            ///< Configuration mode (yellow)
    EDRUMULUS_LED_PURPLE             ///< Boot mode (purple)
} edrumulus_led_status_t;

/**
 * @brief Initialize LED subsystem
 * 
 * @param config LED configuration
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
esp_err_t edrumulus_led_init(const edrumulus_led_config_t *config);

/**
 * @brief Set LED color
 * 
 * @param red Red component (0-255)
 * @param green Green component (0-255)
 * @param blue Blue component (0-255)
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
esp_err_t edrumulus_led_set_color(uint8_t red, uint8_t green, uint8_t blue);

/**
 * @brief Set LED status
 * 
 * @param status LED status
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
esp_err_t edrumulus_led_set_status(edrumulus_led_status_t status);

/**
 * @brief Deinitialize LED subsystem
 * 
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
esp_err_t edrumulus_led_deinit(void);

#ifdef __cplusplus
}
#endif

#endif // EDRUMULUS_LED_H