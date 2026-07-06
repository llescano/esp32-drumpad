/**
 * @file esp32-edrumulus.c
 * @brief ESP32 E-Drum Trigger System - Main Application
 * 
 * Main application entry point for the ESP32-C3 based electronic drum
 * trigger system with USB MIDI support and Edrumulus detection algorithms.
 */

#include <stdio.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "tinyusb.h"

#include "edrumulus_core.h"
#include "edrumulus_midi.h"
#include "edrumulus_input.h"
#include "edrumulus_detection.h"
#include "edrumulus_led.h"
#include "edrumulus_console.h"
#include "usb_descriptors.h"

// Phase 3 Validation Test Functions
static void run_phase3_validation_tests(void);

static const char *TAG = "main";

void app_main(void)
{
    ESP_LOGI(TAG, "ESP32 E-Drum Trigger System Starting...");
    ESP_LOGI(TAG, "Target: ESP32-S3 with USB MIDI support");
    ESP_LOGI(TAG, "TinyUSB MIDI Count: %d", CONFIG_TINYUSB_MIDI_COUNT);
    ESP_LOGI(TAG, "USB OTG Supported: %s", CONFIG_USB_OTG_SUPPORTED ? "YES" : "NO");
    
    // ESP32-S3 USB Pin Information
    ESP_LOGI(TAG, "=== ESP32-S3 USB Configuration ===");
    ESP_LOGI(TAG, "USB D+ Pin: GPIO20 (fixed)");
    ESP_LOGI(TAG, "USB D- Pin: GPIO19 (fixed)");
    ESP_LOGI(TAG, "USB OTG Port: Native USB (not UART bridge)");
    ESP_LOGI(TAG, "Programming Port: UART bridge via USB-to-Serial chip");
    ESP_LOGI(TAG, "IMPORTANT: Use USB-C connector for MIDI, not micro-USB!");
    ESP_LOGI(TAG, "=========================================");

    // System configuration
    edrumulus_config_t sys_config = {
        .enable_usb_midi = true,
        .enable_debug_uart = true,
        .sample_rate_hz = EDRUMULUS_SAMPLE_RATE_HZ,
        .num_pads = 4  // Start with 4 pads for initial testing
    };

    // Initialize core system
    ESP_LOGI(TAG, "Initializing core system...");
    esp_err_t ret = edrumulus_init(&sys_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize Edrumulus system: %s", esp_err_to_name(ret));
        return;
    }
    ESP_LOGI(TAG, "Core system initialized successfully");
    ESP_LOGI(TAG, "Version: %s", edrumulus_get_version());

    // Create MIDI event queue
    ESP_LOGI(TAG, "Creating MIDI queue...");
    QueueHandle_t midi_queue = xQueueCreate(EDRUMULUS_QUEUE_SIZE_MIDI, sizeof(edrumulus_midi_event_t));
    if (!midi_queue) {
        ESP_LOGE(TAG, "Failed to create MIDI queue");
        return;
    }
    ESP_LOGI(TAG, "MIDI queue created successfully");

    // Create input event queue
    ESP_LOGI(TAG, "Creating input queue...");
    QueueHandle_t input_queue = xQueueCreate(10, sizeof(edrumulus_input_event_t));
    if (!input_queue) {
        ESP_LOGE(TAG, "Failed to create input queue");
        return;
    }
    ESP_LOGI(TAG, "Input queue created successfully");

    // Create detection event queue for piezo
    ESP_LOGI(TAG, "Creating detection queue...");
    QueueHandle_t detection_queue = xQueueCreate(20, sizeof(edrumulus_hit_event_t));
    if (!detection_queue) {
        ESP_LOGE(TAG, "Failed to create detection queue");
        return;
    }
    ESP_LOGI(TAG, "Detection queue created successfully");

    // Initialize TinyUSB first - CRÍTICO para USB MIDI
    ESP_LOGI(TAG, "Initializing TinyUSB driver...");
    tinyusb_config_t const tusb_cfg = {
        .device_descriptor = &desc_device,
        .string_descriptor = string_desc_arr,
        .string_descriptor_count = 5, // 5 string descriptors defined
        .external_phy = false,
#if (TUD_OPT_HIGH_SPEED)
        .fs_configuration_descriptor = desc_fs_configuration,
        .hs_configuration_descriptor = desc_hs_configuration,
        .qualifier_descriptor = NULL,
#else
        .configuration_descriptor = desc_fs_configuration,
#endif
    };
    
    ret = tinyusb_driver_install(&tusb_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to install TinyUSB driver: %s", esp_err_to_name(ret));
        return;
    }
    ESP_LOGI(TAG, "TinyUSB driver installed successfully");

    // MIDI configuration
    edrumulus_midi_config_t midi_config = {
        .default_channel = EDRUMULUS_MIDI_CHANNEL_DEFAULT,
        .enable_note_off = true,
        .note_off_delay_ms = 100,
        .event_queue = midi_queue
    };

    // Initialize MIDI subsystem
    ESP_LOGI(TAG, "Initializing MIDI subsystem...");
    ret = edrumulus_midi_init(&midi_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize MIDI subsystem: %s", esp_err_to_name(ret));
        return;
    }
    ESP_LOGI(TAG, "MIDI subsystem initialized successfully");

    // Start MIDI subsystem
    ESP_LOGI(TAG, "Starting MIDI subsystem...");
    ret = edrumulus_midi_start();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start MIDI subsystem: %s", esp_err_to_name(ret));
        return;
    }
    ESP_LOGI(TAG, "MIDI subsystem started successfully");

    // Input configuration
    edrumulus_input_config_t input_config = {
        .event_queue = input_queue,
        .enable_encoder = true,         // Enable encoder
        .enable_boot_button = true      // Enable boot button
    };

    // Initialize input subsystem
    ESP_LOGI(TAG, "Initializing input subsystem...");
    ret = edrumulus_input_init(&input_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize input subsystem: %s", esp_err_to_name(ret));
        return;
    }
    ESP_LOGI(TAG, "Input subsystem initialized successfully");

    // Initialize LED subsystem
    ESP_LOGI(TAG, "Initializing LED subsystem...");
    edrumulus_led_config_t led_config = {
        .brightness = 128,              // Medium brightness
        .enable_status_indication = true
    };
    
    ret = edrumulus_led_init(&led_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize LED subsystem: %s", esp_err_to_name(ret));
        return;
    }
    ESP_LOGI(TAG, "LED subsystem initialized successfully");
    
    // Set initial LED status to purple (boot mode)
    edrumulus_led_set_status(EDRUMULUS_LED_PURPLE);

    // Initialize console subsystem for dynamic parameter adjustment
    ESP_LOGI(TAG, "Initializing console subsystem...");
    ret = edrumulus_console_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize console subsystem: %s", esp_err_to_name(ret));
        return;
    }
    ESP_LOGI(TAG, "Console subsystem initialized successfully");
    ESP_LOGI(TAG, "Console commands available for Phase 3 parameter adjustment");

    // Initialize detection subsystem
    ESP_LOGI(TAG, "Initializing detection subsystem...");
    edrumulus_detection_config_t detection_config = {
        .sample_rate_hz = EDRUMULUS_ADC_SAMPLE_RATE,
        .num_channels = 1,  // Start with 1 channel for piezo
        .event_queue = detection_queue,
        .enable_crosstalk_cancel = false
    };
    
    ret = edrumulus_detection_init(&detection_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize detection subsystem: %s", esp_err_to_name(ret));
        return;
    }
    ESP_LOGI(TAG, "Detection subsystem initialized successfully");

    // Initialize piezo sensor on channel 0 (GPIO4)
    ESP_LOGI(TAG, "Initializing piezo sensor...");
    ret = edrumulus_detection_init_piezo(0, 150);  // Channel 0, threshold 150
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize piezo sensor: %s", esp_err_to_name(ret));
        return;
    }
    ESP_LOGI(TAG, "Piezo sensor initialized successfully");

    // Start piezo monitoring
    ESP_LOGI(TAG, "Starting piezo monitoring...");
    ret = edrumulus_detection_start_piezo_monitor(0, detection_queue);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start piezo monitoring: %s", esp_err_to_name(ret));
        return;
    }
    ESP_LOGI(TAG, "Piezo monitoring started successfully");

    // Start core system
    ESP_LOGI(TAG, "Starting core system...");
    ret = edrumulus_start();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start Edrumulus system: %s", esp_err_to_name(ret));
        return;
    }
    ESP_LOGI(TAG, "Core system started successfully");

    ESP_LOGI(TAG, "ESP32 E-Drum Trigger System initialized successfully");
    ESP_LOGI(TAG, "System status: %d", edrumulus_get_status());
    ESP_LOGI(TAG, "USB MIDI: %s", edrumulus_midi_is_connected() ? "Connected" : "Disconnected");
    
    // Set LED to green (normal operation)
    edrumulus_led_set_status(EDRUMULUS_LED_GREEN);
    ESP_LOGI(TAG, "LED status set to normal operation (green)");

    // Run Phase 3 Validation Tests
    ESP_LOGI(TAG, "Starting Phase 3 validation tests...");
    run_phase3_validation_tests();
    ESP_LOGI(TAG, "Phase 3 validation tests completed");

    // Buffer para comandos de consola
    static char console_buffer[256];
    static int console_pos = 0;
    
    // Main application loop
    uint32_t loop_count = 0;
    while (1) {
        // Check for console input (non-blocking)
        int c = getchar();
        if (c != EOF) {
            if (c == '\n' || c == '\r') {
                if (console_pos > 0) {
                    console_buffer[console_pos] = '\0';
                    printf("Comando recibido: %s\n", console_buffer);
                    edrumulus_console_process_command(console_buffer);
                    console_pos = 0;
                    printf("> ");
                }
            } else if (c == '\b' || c == 127) { // Backspace
                if (console_pos > 0) {
                    console_pos--;
                    printf("\b \b");
                }
            } else if (console_pos < sizeof(console_buffer) - 1) {
                console_buffer[console_pos++] = c;
                printf("%c", c);
            }
        }
        // Check for input events (non-blocking)
        edrumulus_input_event_t input_event;
        if (edrumulus_input_get_event(&input_event, 0) == ESP_OK) {
            switch (input_event.type) {
                case EDRUMULUS_INPUT_BOOT_PRESS:
                    // PRIORITY 1: Send MIDI immediately
                    edrumulus_midi_send_note_on(9, MIDI_NOTE_KICK_DRUM, 100);
                    
                    // PRIORITY 2: Visual feedback
                    edrumulus_led_set_status(EDRUMULUS_LED_BLUE);
                    
                    // PRIORITY 3: Debug info (minimal)
                    ESP_LOGI(TAG, "Boot button: MIDI kick drum");
                    
                    // Schedule note off with reduced delay
                    vTaskDelay(pdMS_TO_TICKS(80));
                    edrumulus_midi_send_note_off(9, MIDI_NOTE_KICK_DRUM);
                    
                    // Return LED to normal state
                    edrumulus_led_set_status(EDRUMULUS_LED_GREEN);
                    break;
                    
                case EDRUMULUS_INPUT_ENCODER_CW:
                    // PRIORITY 1: Send MIDI immediately
                    edrumulus_midi_send_note_on(9, MIDI_NOTE_HIHAT_CLOSED, 80);
                    
                    // PRIORITY 2: Visual feedback
                    edrumulus_led_set_status(EDRUMULUS_LED_YELLOW);
                    
                    // PRIORITY 3: Debug info (minimal)
                    ESP_LOGI(TAG, "Encoder CW: MIDI hi-hat closed");
                    
                    // Reduced delay for better responsiveness
                    vTaskDelay(pdMS_TO_TICKS(40));
                    edrumulus_midi_send_note_off(9, MIDI_NOTE_HIHAT_CLOSED);
                    
                    // Return LED to normal state
                    edrumulus_led_set_status(EDRUMULUS_LED_GREEN);
                    break;
                    
                case EDRUMULUS_INPUT_ENCODER_CCW:
                    // PRIORITY 1: Send MIDI immediately
                    edrumulus_midi_send_note_on(9, MIDI_NOTE_HIHAT_OPEN, 80);
                    
                    // PRIORITY 2: Visual feedback
                    edrumulus_led_set_status(EDRUMULUS_LED_YELLOW);
                    
                    // PRIORITY 3: Debug info (minimal)
                    ESP_LOGI(TAG, "Encoder CCW: MIDI hi-hat open");
                    
                    // Reduced delay for better responsiveness
                    vTaskDelay(pdMS_TO_TICKS(40));
                    edrumulus_midi_send_note_off(9, MIDI_NOTE_HIHAT_OPEN);
                    
                    // Return LED to normal state
                    edrumulus_led_set_status(EDRUMULUS_LED_GREEN);
                    break;
                    
                case EDRUMULUS_INPUT_ENCODER_PRESS:
                    // PRIORITY 1: Send MIDI immediately
                    edrumulus_midi_send_note_on(9, MIDI_NOTE_CRASH_CYMBAL, 120);
                    
                    // PRIORITY 2: Visual feedback
                    edrumulus_led_set_status(EDRUMULUS_LED_PURPLE);
                    
                    // PRIORITY 3: Debug info (minimal)
                    ESP_LOGI(TAG, "Encoder press: MIDI crash cymbal");
                    
                    // Reduced delay for better responsiveness (crash sustains longer)
                    vTaskDelay(pdMS_TO_TICKS(150));
                    edrumulus_midi_send_note_off(9, MIDI_NOTE_CRASH_CYMBAL);
                    
                    // Return LED to normal state
                    edrumulus_led_set_status(EDRUMULUS_LED_GREEN);
                    break;
                    
                default:
                    ESP_LOGW(TAG, "Unknown input event type: %d", input_event.type);
                    break;
            }
        }
        
        // Check for piezo detection events (non-blocking)
        edrumulus_hit_event_t hit_event;
        if (xQueueReceive(detection_queue, &hit_event, 0) == pdTRUE) {
            // PRIORITY 1: Send MIDI immediately for minimum latency
            edrumulus_midi_send_note_on(9, hit_event.note, hit_event.velocity);
            
            // PRIORITY 2: Visual feedback
            edrumulus_led_set_status(EDRUMULUS_LED_RED);
            
            // PRIORITY 3: Debug info (minimal)
            ESP_LOGI(TAG, "Piezo hit: Ch=%d, Note=%d, Vel=%d", 
                     hit_event.channel, hit_event.note, hit_event.velocity);
            
            // Schedule note off with minimal delay for better responsiveness
            vTaskDelay(pdMS_TO_TICKS(30));
            edrumulus_midi_send_note_off(9, hit_event.note);
            
            // Return LED to normal state
            edrumulus_led_set_status(EDRUMULUS_LED_GREEN);
            
            // *** ELIMINADO: delay de 500ms ***
            // El sistema de mask time inteligente en edrumulus_detection.c
            // ahora maneja la prevención de retriggering con latencia <3ms
            // en lugar del delay fijo de 500ms anterior
        }
        
        // System monitoring and status updates (commented out to reduce terminal spam)
        // if (loop_count % 50 == 0) {  // Every 5 seconds
        //     uint32_t sent_events, queue_depth;
        //     edrumulus_midi_get_stats(&sent_events, &queue_depth);
        //     
        //     ESP_LOGI(TAG, "Status - Events sent: %lu, Queue depth: %lu, USB: %s",
        //              sent_events, queue_depth,
        //              edrumulus_midi_is_connected() ? "Connected" : "Disconnected");
        // }

        // Test MIDI note removed - only send MIDI on real piezo hits

        vTaskDelay(pdMS_TO_TICKS(10));  // 10ms loop for responsive input
        loop_count++;
    }
}

/**
 * @brief Run Phase 3 Validation Tests
 * 
 * Ejecuta todas las pruebas de validación de Phase 3:
 * - Edge Detector Validation
 * - Decay Analyzer Validation  
 * - Velocity Validator Validation
 * - Adaptive Threshold Validation
 * - Full System Validation
 */
static void run_phase3_validation_tests(void)
{
    ESP_LOGI(TAG, "Phase 3 Validation Tests (Basic)");
    
    // Initialize Phase 3 validation subsystem
    esp_err_t ret = edrumulus_phase3_validation_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize Phase 3 validation: %s", esp_err_to_name(ret));
        return;
    }
    ESP_LOGI(TAG, "Phase 3 validation subsystem initialized");
    
    // Run only basic edge detector test to avoid stack overflow
    ESP_LOGI(TAG, "Running Edge Detector validation...");
    ret = edrumulus_phase3_test_edge_detector(0);
    ESP_LOGI(TAG, "Edge Detector test: %s", ret == ESP_OK ? "PASSED" : "FAILED");
    
    // Get basic validation results
    edrumulus_phase3_validation_t *results;
    ret = edrumulus_phase3_get_validation_results(0, &results);
    if (ret == ESP_OK && results) {
        ESP_LOGI(TAG, "Edge Detector: %s", results->edge_detector_criteria_met ? "PASSED" : "FAILED");
        ESP_LOGI(TAG, "Validation enabled: %s", results->validation_enabled ? "YES" : "NO");
    } else {
        ESP_LOGW(TAG, "Failed to get validation results");
    }
    
    ESP_LOGI(TAG, "Phase 3 Validation Tests Completed (Basic)");
}
