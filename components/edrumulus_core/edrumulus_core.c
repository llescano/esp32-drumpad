/**
 * @file edrumulus_core.c
 * @brief ESP32 E-Drum Trigger System - Core Component Implementation
 */

#include "edrumulus_core.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "nvs_flash.h"

static const char *TAG = "edrumulus_core";

// Global system state
static edrumulus_status_t g_system_status = EDRUMULUS_STATUS_UNINITIALIZED;
static edrumulus_config_t g_system_config = {0};

// Version string
static char g_version_string[32];

esp_err_t edrumulus_init(const edrumulus_config_t *config)
{
    if (config == NULL) {
        ESP_LOGE(TAG, "Configuration cannot be NULL");
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "Initializing Edrumulus system v%d.%d.%d", 
             EDRUMULUS_VERSION_MAJOR, EDRUMULUS_VERSION_MINOR, EDRUMULUS_VERSION_PATCH);

    g_system_status = EDRUMULUS_STATUS_INITIALIZING;

    // Copy configuration
    g_system_config = *config;

    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // Generate version string
    snprintf(g_version_string, sizeof(g_version_string), 
             "v%d.%d.%d", EDRUMULUS_VERSION_MAJOR, 
             EDRUMULUS_VERSION_MINOR, EDRUMULUS_VERSION_PATCH);

    ESP_LOGI(TAG, "Core initialization completed");
    ESP_LOGI(TAG, "Configuration: USB MIDI=%s, Debug UART=%s, Sample Rate=%lu Hz, Pads=%d",
             config->enable_usb_midi ? "enabled" : "disabled",
             config->enable_debug_uart ? "enabled" : "disabled",
             config->sample_rate_hz,
             config->num_pads);

    g_system_status = EDRUMULUS_STATUS_READY;
    return ESP_OK;
}

esp_err_t edrumulus_start(void)
{
    if (g_system_status != EDRUMULUS_STATUS_READY) {
        ESP_LOGE(TAG, "System not ready for start (status: %d)", g_system_status);
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "Starting Edrumulus system");
    
    // TODO: Start all subsystem tasks
    // - ADC sampling task
    // - Signal processing task
    // - MIDI output task
    // - Configuration task
    // - LED control task
    // - Input handling task

    g_system_status = EDRUMULUS_STATUS_RUNNING;
    ESP_LOGI(TAG, "Edrumulus system started successfully");
    
    return ESP_OK;
}

esp_err_t edrumulus_stop(void)
{
    if (g_system_status != EDRUMULUS_STATUS_RUNNING) {
        ESP_LOGE(TAG, "System not running");
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "Stopping Edrumulus system");
    
    // TODO: Stop all subsystem tasks gracefully
    
    g_system_status = EDRUMULUS_STATUS_READY;
    ESP_LOGI(TAG, "Edrumulus system stopped");
    
    return ESP_OK;
}

edrumulus_status_t edrumulus_get_status(void)
{
    return g_system_status;
}

const char* edrumulus_get_version(void)
{
    return g_version_string;
}