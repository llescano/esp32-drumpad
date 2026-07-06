/**
 * @file edrumulus_input.c
 * @brief ESP32 E-Drum Trigger System - Input Component Implementation
 */

#include "edrumulus_input.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_timer.h"
#include <string.h>

static const char *TAG = "edrumulus_input";

// Input subsystem state
static bool g_input_initialized = false;
static edrumulus_input_config_t g_input_config = {0};
static volatile uint32_t g_last_boot_press_time = 0;

// Encoder state
static volatile int g_encoder_a_state = 0;
static volatile int g_encoder_b_state = 0;
static volatile uint32_t g_last_encoder_time = 0;
static volatile uint32_t g_last_encoder_btn_time = 0;

// Debounce time in microseconds (50ms for button, 5ms for encoder)
#define DEBOUNCE_TIME_US 50000
#define ENCODER_DEBOUNCE_TIME_US 5000

/**
 * @brief GPIO interrupt handler for boot button
 */
static void IRAM_ATTR boot_button_isr_handler(void* arg)
{
    uint32_t current_time = esp_timer_get_time();
    
    // Simple debouncing
    if (current_time - g_last_boot_press_time < DEBOUNCE_TIME_US) {
        return;
    }
    g_last_boot_press_time = current_time;
    
    // Check if button is pressed (active low)
    int level = gpio_get_level(EDRUMULUS_GPIO_BOOT_BTN);
    if (level == 0) {  // Button pressed (active low)
        // Send event to queue from ISR
        if (g_input_config.event_queue) {
            edrumulus_input_event_t event = {
                .type = EDRUMULUS_INPUT_BOOT_PRESS,
                .timestamp = current_time,
                .value = 1
            };
            
            BaseType_t xHigherPriorityTaskWoken = pdFALSE;
            xQueueSendFromISR(g_input_config.event_queue, &event, &xHigherPriorityTaskWoken);
            
            if (xHigherPriorityTaskWoken) {
                portYIELD_FROM_ISR();
            }
        }
    }
}

/**
 * @brief GPIO interrupt handler for encoder A pin
 */
static void IRAM_ATTR encoder_a_isr_handler(void* arg)
{
    uint32_t current_time = esp_timer_get_time();
    
    // Debouncing for encoder
    if (current_time - g_last_encoder_time < ENCODER_DEBOUNCE_TIME_US) {
        return;
    }
    g_last_encoder_time = current_time;
    
    int a_state = gpio_get_level(EDRUMULUS_GPIO_ENCODER_A);
    int b_state = gpio_get_level(EDRUMULUS_GPIO_ENCODER_B);
    
    // Determine rotation direction
    edrumulus_input_event_type_t event_type;
    if (a_state != g_encoder_a_state) {
        if (a_state == b_state) {
            event_type = EDRUMULUS_INPUT_ENCODER_CCW;  // Counter-clockwise
        } else {
            event_type = EDRUMULUS_INPUT_ENCODER_CW;   // Clockwise
        }
        
        // Send event to queue from ISR
        if (g_input_config.event_queue) {
            edrumulus_input_event_t event = {
                .type = event_type,
                .timestamp = current_time,
                .value = (event_type == EDRUMULUS_INPUT_ENCODER_CW) ? 1 : -1
            };
            
            BaseType_t xHigherPriorityTaskWoken = pdFALSE;
            xQueueSendFromISR(g_input_config.event_queue, &event, &xHigherPriorityTaskWoken);
            
            if (xHigherPriorityTaskWoken) {
                portYIELD_FROM_ISR();
            }
        }
    }
    
    g_encoder_a_state = a_state;
    g_encoder_b_state = b_state;
}

/**
 * @brief GPIO interrupt handler for encoder button
 */
static void IRAM_ATTR encoder_button_isr_handler(void* arg)
{
    uint32_t current_time = esp_timer_get_time();
    
    // Debouncing for encoder button
    if (current_time - g_last_encoder_btn_time < DEBOUNCE_TIME_US) {
        return;
    }
    g_last_encoder_btn_time = current_time;
    
    // Check if button is pressed (active low)
    int level = gpio_get_level(EDRUMULUS_GPIO_ENCODER_BTN);
    if (level == 0) {  // Button pressed (active low)
        // Send event to queue from ISR
        if (g_input_config.event_queue) {
            edrumulus_input_event_t event = {
                .type = EDRUMULUS_INPUT_ENCODER_PRESS,
                .timestamp = current_time,
                .value = 1
            };
            
            BaseType_t xHigherPriorityTaskWoken = pdFALSE;
            xQueueSendFromISR(g_input_config.event_queue, &event, &xHigherPriorityTaskWoken);
            
            
            if (xHigherPriorityTaskWoken) {
                portYIELD_FROM_ISR();
            }
        }
    }
}

esp_err_t edrumulus_input_init(const edrumulus_input_config_t *config)
{
    if (config == NULL) {
        ESP_LOGE(TAG, "Input configuration cannot be NULL");
        return ESP_ERR_INVALID_ARG;
    }

    if (g_input_initialized) {
        ESP_LOGW(TAG, "Input already initialized");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Initializing input subsystem");
    
    // Copy configuration
    g_input_config = *config;
    
    // Configure boot button if enabled
    if (config->enable_boot_button) {
        ESP_LOGI(TAG, "Configuring boot button on GPIO%d", EDRUMULUS_GPIO_BOOT_BTN);
        
        // Configure GPIO
        gpio_config_t io_conf = {
            .pin_bit_mask = (1ULL << EDRUMULUS_GPIO_BOOT_BTN),
            .mode = GPIO_MODE_INPUT,
            .pull_up_en = GPIO_PULLUP_ENABLE,  // Enable pull-up (button is active low)
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_NEGEDGE     // Trigger on falling edge (button press)
        };
        
        esp_err_t ret = gpio_config(&io_conf);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to configure boot button GPIO: %s", esp_err_to_name(ret));
            return ret;
        }
        
        // Install GPIO ISR service
        ret = gpio_install_isr_service(0);
        if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
            ESP_LOGE(TAG, "Failed to install GPIO ISR service: %s", esp_err_to_name(ret));
            return ret;
        }
        
        // Add ISR handler for boot button
        ret = gpio_isr_handler_add(EDRUMULUS_GPIO_BOOT_BTN, boot_button_isr_handler, NULL);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to add ISR handler for boot button: %s", esp_err_to_name(ret));
            return ret;
        }
        
        ESP_LOGI(TAG, "Boot button configured successfully");
    }
    
    // Initialize rotary encoder if enabled
    if (config->enable_encoder) {
        ESP_LOGI(TAG, "Configuring rotary encoder on GPIO%d, %d, %d", 
                 EDRUMULUS_GPIO_ENCODER_A, EDRUMULUS_GPIO_ENCODER_B, EDRUMULUS_GPIO_ENCODER_BTN);
        
        // Configure encoder A pin
        gpio_config_t encoder_a_conf = {
            .pin_bit_mask = (1ULL << EDRUMULUS_GPIO_ENCODER_A),
            .mode = GPIO_MODE_INPUT,
            .pull_up_en = GPIO_PULLUP_ENABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_ANYEDGE  // Trigger on both edges
        };
        
        esp_err_t ret = gpio_config(&encoder_a_conf);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to configure encoder A GPIO: %s", esp_err_to_name(ret));
            return ret;
        }
        
        // Configure encoder B pin
        gpio_config_t encoder_b_conf = {
            .pin_bit_mask = (1ULL << EDRUMULUS_GPIO_ENCODER_B),
            .mode = GPIO_MODE_INPUT,
            .pull_up_en = GPIO_PULLUP_ENABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE  // No interrupt for B pin
        };
        
        ret = gpio_config(&encoder_b_conf);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to configure encoder B GPIO: %s", esp_err_to_name(ret));
            return ret;
        }
        
        // Configure encoder button pin
        gpio_config_t encoder_btn_conf = {
            .pin_bit_mask = (1ULL << EDRUMULUS_GPIO_ENCODER_BTN),
            .mode = GPIO_MODE_INPUT,
            .pull_up_en = GPIO_PULLUP_ENABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_NEGEDGE  // Trigger on falling edge
        };
        
        ret = gpio_config(&encoder_btn_conf);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to configure encoder button GPIO: %s", esp_err_to_name(ret));
            return ret;
        }
        
        // Install GPIO ISR service if not already installed
        ret = gpio_install_isr_service(0);
        if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
            ESP_LOGE(TAG, "Failed to install GPIO ISR service: %s", esp_err_to_name(ret));
            return ret;
        }
        
        // Add ISR handlers
        ret = gpio_isr_handler_add(EDRUMULUS_GPIO_ENCODER_A, encoder_a_isr_handler, NULL);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to add ISR handler for encoder A: %s", esp_err_to_name(ret));
            return ret;
        }
        
        ret = gpio_isr_handler_add(EDRUMULUS_GPIO_ENCODER_BTN, encoder_button_isr_handler, NULL);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to add ISR handler for encoder button: %s", esp_err_to_name(ret));
            return ret;
        }
        
        // Initialize encoder state
        g_encoder_a_state = gpio_get_level(EDRUMULUS_GPIO_ENCODER_A);
        g_encoder_b_state = gpio_get_level(EDRUMULUS_GPIO_ENCODER_B);
        
        ESP_LOGI(TAG, "Rotary encoder configured successfully");
    }
    
    g_input_initialized = true;
    ESP_LOGI(TAG, "Input subsystem initialized successfully");
    
    return ESP_OK;
}

esp_err_t edrumulus_input_deinit(void)
{
    if (!g_input_initialized) {
        ESP_LOGW(TAG, "Input not initialized");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Deinitializing input subsystem");
    
    // Remove ISR handlers
    if (g_input_config.enable_boot_button) {
        gpio_isr_handler_remove(EDRUMULUS_GPIO_BOOT_BTN);
        ESP_LOGI(TAG, "Boot button ISR handler removed");
    }
    
    if (g_input_config.enable_encoder) {
        gpio_isr_handler_remove(EDRUMULUS_GPIO_ENCODER_A);
        gpio_isr_handler_remove(EDRUMULUS_GPIO_ENCODER_BTN);
        ESP_LOGI(TAG, "Encoder ISR handlers removed");
    }
    
    // Reset configuration
    memset(&g_input_config, 0, sizeof(g_input_config));
    
    g_input_initialized = false;
    ESP_LOGI(TAG, "Input subsystem deinitialized");
    
    return ESP_OK;
}

esp_err_t edrumulus_input_get_event(edrumulus_input_event_t *event, uint32_t timeout_ms)
{
    if (!g_input_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    if (event == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (g_input_config.event_queue == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    
    TickType_t ticks_to_wait = (timeout_ms == 0) ? 0 : pdMS_TO_TICKS(timeout_ms);
    
    if (xQueueReceive(g_input_config.event_queue, event, ticks_to_wait) == pdTRUE) {
        ESP_LOGD(TAG, "Input event received: type=%d, value=%ld", event->type, event->value);
        return ESP_OK;
    }
    
    return ESP_ERR_TIMEOUT;
}

bool edrumulus_input_is_boot_pressed(void)
{
    if (!g_input_initialized) {
        return false;
    }
    
    // Boot button is active low (pressed = 0)
    return (gpio_get_level(EDRUMULUS_GPIO_BOOT_BTN) == 0);
}