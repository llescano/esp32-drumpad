/**
 * @file edrumulus_config.h
 * @brief ESP32 E-Drum Trigger System - Configuration Component
 * 
 * Configuration management using NVS (Non-Volatile Storage)
 */

#ifndef EDRUMULUS_CONFIG_H
#define EDRUMULUS_CONFIG_H

#include "esp_err.h"
#include "freertos/FreeRTOS.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief System configuration structure
 */
typedef struct {
    uint8_t midi_channel;            ///< Default MIDI channel
    uint32_t sample_rate_hz;         ///< ADC sampling rate
    uint8_t num_pads;                ///< Number of active pads
    bool enable_usb_midi;            ///< Enable USB MIDI
    uint8_t led_brightness;          ///< LED brightness (0-255)
} edrumulus_system_config_t;

/**
 * @brief Pad configuration structure
 */
typedef struct {
    uint8_t threshold;               ///< Detection threshold
    uint8_t sensitivity;             ///< Sensitivity setting
    uint8_t midi_note;               ///< MIDI note number
    uint8_t midi_note_rim;           ///< MIDI note for rimshot
    uint8_t curve;                   ///< Velocity curve
    bool enable_rimshot;             ///< Enable rimshot detection
    bool enable_crosstalk_cancel;    ///< Enable crosstalk cancellation
} edrumulus_pad_config_t;

/**
 * @brief Initialize configuration subsystem
 * 
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
esp_err_t edrumulus_config_init(void);

/**
 * @brief Save system configuration to NVS
 * 
 * @param config System configuration to save
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
esp_err_t edrumulus_config_save_system(const edrumulus_system_config_t *config);

/**
 * @brief Load system configuration from NVS
 * 
 * @param config Pointer to store loaded configuration
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
esp_err_t edrumulus_config_load_system(edrumulus_system_config_t *config);

/**
 * @brief Save pad configuration to NVS
 * 
 * @param pad_id Pad ID (0-7)
 * @param config Pad configuration to save
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
esp_err_t edrumulus_config_save_pad(uint8_t pad_id, const edrumulus_pad_config_t *config);

/**
 * @brief Load pad configuration from NVS
 * 
 * @param pad_id Pad ID (0-7)
 * @param config Pointer to store loaded configuration
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
esp_err_t edrumulus_config_load_pad(uint8_t pad_id, edrumulus_pad_config_t *config);

/**
 * @brief Load default configuration
 * 
 * @param config Pointer to store default configuration
 */
void edrumulus_config_load_defaults(edrumulus_system_config_t *config);

/**
 * @brief Factory reset - erase all configuration
 * 
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
esp_err_t edrumulus_config_factory_reset(void);

/**
 * @brief Deinitialize configuration subsystem
 * 
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
esp_err_t edrumulus_config_deinit(void);

#ifdef __cplusplus
}
#endif

#endif // EDRUMULUS_CONFIG_H