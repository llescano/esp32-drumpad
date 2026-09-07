/**
 * @file edrumulus_led.c
 * @brief ESP32 E-Drum Trigger System - LED Component Implementation
 */

#include "edrumulus_led.h"
#include "esp_log.h"
#include "led_strip.h"

static const char *TAG = "edrumulus_led";

// LED subsystem state
static bool g_led_initialized = false;
static led_strip_handle_t g_led_strip = NULL;

esp_err_t edrumulus_led_init(const edrumulus_led_config_t *config)
{
    if (config == NULL) {
        ESP_LOGE(TAG, "LED configuration cannot be NULL");
        return ESP_ERR_INVALID_ARG;
    }

    if (g_led_initialized) {
        ESP_LOGW(TAG, "LED already initialized");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Initializing LED subsystem");
    
    // Configure LED strip
    // led_strip >=3.x: GRB/WS2812 is the default layout, no explicit format fields
    led_strip_config_t strip_config = {
        .strip_gpio_num = EDRUMULUS_GPIO_LED,
        .max_leds = 1,
        .flags.invert_out = false,
    };
    
    led_strip_rmt_config_t rmt_config = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = 10 * 1000 * 1000, // 10MHz
        .flags.with_dma = false,
    };
    
    esp_err_t ret = led_strip_new_rmt_device(&strip_config, &rmt_config, &g_led_strip);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create LED strip: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Clear LED
    led_strip_clear(g_led_strip);
    
    g_led_initialized = true;
    ESP_LOGI(TAG, "LED subsystem initialized successfully");
    
    return ESP_OK;
}

esp_err_t edrumulus_led_set_color(uint8_t red, uint8_t green, uint8_t blue)
{
    if (!g_led_initialized) {
        ESP_LOGE(TAG, "LED not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    esp_err_t ret = led_strip_set_pixel(g_led_strip, 0, red, green, blue);
    if (ret == ESP_OK) {
        ret = led_strip_refresh(g_led_strip);
    }
    
    return ret;
}

esp_err_t edrumulus_led_set_status(edrumulus_led_status_t status)
{
    if (!g_led_initialized) {
        ESP_LOGE(TAG, "LED not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    uint8_t red = 0, green = 0, blue = 0;
    
    switch (status) {
        case EDRUMULUS_LED_OFF:
            red = 0; green = 0; blue = 0;
            break;
        case EDRUMULUS_LED_GREEN:
            red = 0; green = 32; blue = 0;  // Reducido de 64 a 32 (50% intensidad)
            break;
        case EDRUMULUS_LED_BLUE:
            red = 0; green = 0; blue = 32;  // Reducido de 64 a 32 (50% intensidad)
            break;
        case EDRUMULUS_LED_RED:
            red = 32; green = 0; blue = 0;  // Reducido de 64 a 32 (50% intensidad)
            break;
        case EDRUMULUS_LED_YELLOW:
            red = 32; green = 32; blue = 0;  // Reducido de 64 a 32 (50% intensidad)
            break;
        case EDRUMULUS_LED_PURPLE:
            red = 16; green = 0; blue = 16;  // Reducido de 32 a 16 (50% intensidad)
            break;
        default:
            ESP_LOGW(TAG, "Unknown LED status: %d", status);
            return ESP_ERR_INVALID_ARG;
    }
    
    ESP_LOGI(TAG, "Setting LED status to %d (R:%d G:%d B:%d)", status, red, green, blue);
    return edrumulus_led_set_color(red, green, blue);
}

esp_err_t edrumulus_led_deinit(void)
{
    if (!g_led_initialized) {
        ESP_LOGW(TAG, "LED not initialized");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Deinitializing LED subsystem");
    
    if (g_led_strip) {
        led_strip_del(g_led_strip);
        g_led_strip = NULL;
    }
    
    g_led_initialized = false;
    ESP_LOGI(TAG, "LED subsystem deinitialized");
    
    return ESP_OK;
}