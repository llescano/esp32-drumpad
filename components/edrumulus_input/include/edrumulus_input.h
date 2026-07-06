/**
 * @file edrumulus_input.h
 * @brief ESP32 E-Drum Trigger System - Input Component
 * 
 * User input handling for rotary encoder and boot button
 */

#ifndef EDRUMULUS_INPUT_H
#define EDRUMULUS_INPUT_H

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#ifdef __cplusplus
extern "C" {
#endif

// GPIO Pin Definitions for ESP32-S3
#define EDRUMULUS_GPIO_ENCODER_A    1
#define EDRUMULUS_GPIO_ENCODER_B    2
#define EDRUMULUS_GPIO_ENCODER_BTN  3
#define EDRUMULUS_GPIO_BOOT_BTN     0

/**
 * @brief Input configuration structure
 */
typedef struct {
    QueueHandle_t event_queue;       ///< Queue for input events
    bool enable_encoder;             ///< Enable rotary encoder
    bool enable_boot_button;         ///< Enable boot button
} edrumulus_input_config_t;

/**
 * @brief Input event types
 */
typedef enum {
    EDRUMULUS_INPUT_ENCODER_CW,      ///< Encoder clockwise rotation
    EDRUMULUS_INPUT_ENCODER_CCW,     ///< Encoder counter-clockwise rotation
    EDRUMULUS_INPUT_ENCODER_PRESS,   ///< Encoder button press
    EDRUMULUS_INPUT_BOOT_PRESS,      ///< Boot button press
    EDRUMULUS_INPUT_BOOT_HOLD        ///< Boot button hold
} edrumulus_input_event_type_t;

/**
 * @brief Input event structure
 */
typedef struct {
    edrumulus_input_event_type_t type;
    uint32_t timestamp;
    int32_t value;                   ///< Event-specific value
} edrumulus_input_event_t;

/**
 * @brief Initialize input subsystem
 * 
 * @param config Input configuration
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
esp_err_t edrumulus_input_init(const edrumulus_input_config_t *config);

/**
 * @brief Deinitialize input subsystem
 * 
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
esp_err_t edrumulus_input_deinit(void);

/**
 * @brief Get input event from queue (non-blocking)
 * 
 * @param event Pointer to store the received event
 * @param timeout_ms Timeout in milliseconds (0 for non-blocking)
 * @return esp_err_t ESP_OK if event received, ESP_ERR_TIMEOUT if no event, error code otherwise
 */
esp_err_t edrumulus_input_get_event(edrumulus_input_event_t *event, uint32_t timeout_ms);

/**
 * @brief Check if boot button is currently pressed
 * 
 * @return bool true if pressed, false otherwise
 */
bool edrumulus_input_is_boot_pressed(void);

#ifdef __cplusplus
}
#endif

#endif // EDRUMULUS_INPUT_H