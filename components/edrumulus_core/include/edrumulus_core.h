/**
 * @file edrumulus_core.h
 * @brief ESP32 E-Drum Trigger System - Core Component
 * 
 * Core system initialization and management for the ESP32-C3 based
 * electronic drum trigger system with USB MIDI support.
 */

#ifndef EDRUMULUS_CORE_H
#define EDRUMULUS_CORE_H

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#ifdef __cplusplus
extern "C" {
#endif

// System Configuration
#define EDRUMULUS_VERSION_MAJOR 1
#define EDRUMULUS_VERSION_MINOR 0
#define EDRUMULUS_VERSION_PATCH 0

#define EDRUMULUS_MAX_PADS 8
#define EDRUMULUS_SAMPLE_RATE_HZ 8000
#define EDRUMULUS_ADC_CHANNELS 6

// Task Priorities
#define EDRUMULUS_TASK_PRIORITY_HIGH 5
#define EDRUMULUS_TASK_PRIORITY_NORMAL 3
#define EDRUMULUS_TASK_PRIORITY_LOW 1

// Queue Sizes
#define EDRUMULUS_QUEUE_SIZE_SAMPLES 32
#define EDRUMULUS_QUEUE_SIZE_MIDI 16
#define EDRUMULUS_QUEUE_SIZE_CONFIG 8

/**
 * @brief System initialization configuration
 */
typedef struct {
    bool enable_usb_midi;     ///< Enable USB MIDI interface
    bool enable_debug_uart;   ///< Enable debug UART output
    uint32_t sample_rate_hz;  ///< ADC sampling rate in Hz
    uint8_t num_pads;         ///< Number of drum pads to configure
} edrumulus_config_t;

/**
 * @brief System status enumeration
 */
typedef enum {
    EDRUMULUS_STATUS_UNINITIALIZED = 0,
    EDRUMULUS_STATUS_INITIALIZING,
    EDRUMULUS_STATUS_READY,
    EDRUMULUS_STATUS_RUNNING,
    EDRUMULUS_STATUS_ERROR
} edrumulus_status_t;

/**
 * @brief Initialize the Edrumulus system
 * 
 * @param config System configuration parameters
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
esp_err_t edrumulus_init(const edrumulus_config_t *config);

/**
 * @brief Start the Edrumulus system
 * 
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
esp_err_t edrumulus_start(void);

/**
 * @brief Stop the Edrumulus system
 * 
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
esp_err_t edrumulus_stop(void);

/**
 * @brief Get current system status
 * 
 * @return edrumulus_status_t Current system status
 */
edrumulus_status_t edrumulus_get_status(void);

/**
 * @brief Get system version string
 * 
 * @return const char* Version string
 */
const char* edrumulus_get_version(void);

#ifdef __cplusplus
}
#endif

#endif // EDRUMULUS_