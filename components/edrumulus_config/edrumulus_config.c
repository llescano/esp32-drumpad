/**
 * @file edrumulus_config.c
 * @brief ESP32 E-Drum Trigger System - Configuration Component Implementation
 */

#include "edrumulus_config.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"

static const char *TAG = "edrumulus_config";
static const char *NVS_NAMESPACE = "edrumulus";

// Configuration subsystem state
static bool g_config_initialized = false;
static nvs_handle_t g_nvs_handle = 0;

esp_err_t edrumulus_config_init(void)
{
    if (g_config_initialized) {
        ESP_LOGW(TAG, "Config already initialized");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Initializing configuration subsystem");
    
    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize NVS: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Open NVS handle
    ret = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &g_nvs_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS handle: %s", esp_err_to_name(ret));
        return ret;
    }
    
    g_config_initialized = true;
    ESP_LOGI(TAG, "Configuration subsystem initialized successfully");
    
    return ESP_OK;
}

esp_err_t edrumulus_config_save_system(const edrumulus_system_config_t *config)
{
    if (!g_config_initialized) {
        ESP_LOGE(TAG, "Config not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    if (config == NULL) {
        ESP_LOGE(TAG, "Config cannot be NULL");
        return ESP_ERR_INVALID_ARG;
    }
    
    esp_err_t ret = nvs_set_blob(g_nvs_handle, "system_config", config, sizeof(edrumulus_system_config_t));
    if (ret == ESP_OK) {
        ret = nvs_commit(g_nvs_handle);
    }
    
    return ret;
}

esp_err_t edrumulus_config_load_system(edrumulus_system_config_t *config)
{
    if (!g_config_initialized) {
        ESP_LOGE(TAG, "Config not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    if (config == NULL) {
        ESP_LOGE(TAG, "Config cannot be NULL");
        return ESP_ERR_INVALID_ARG;
    }
    
    size_t required_size = sizeof(edrumulus_system_config_t);
    esp_err_t ret = nvs_get_blob(g_nvs_handle, "system_config", config, &required_size);
    
    if (ret == ESP_ERR_NVS_NOT_FOUND) {
        // Load default configuration
        ESP_LOGI(TAG, "No saved config found, loading defaults");
        edrumulus_config_load_defaults(config);
        ret = ESP_OK;
    }
    
    return ret;
}

void edrumulus_config_load_defaults(edrumulus_system_config_t *config)
{
    if (config == NULL) {
        return;
    }
    
    // Set default system configuration
    config->midi_channel = 9;  // Channel 10 (0-indexed)
    config->sample_rate_hz = 8000;
    config->num_pads = 6;
    config->enable_usb_midi = true;
    config->led_brightness = 128;
    
    ESP_LOGI(TAG, "Default configuration loaded");
}

esp_err_t edrumulus_config_deinit(void)
{
    if (!g_config_initialized) {
        ESP_LOGW(TAG, "Config not initialized");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Deinitializing configuration subsystem");
    
    if (g_nvs_handle) {
        nvs_close(g_nvs_handle);
        g_nvs_handle = 0;
    }
    
    g_config_initialized = false;
    ESP_LOGI(TAG, "Configuration subsystem deinitialized");
    
    return ESP_OK;
}