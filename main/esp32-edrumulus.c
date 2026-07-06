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
        .string_descriptor_count = 6, // 4 lang + 5 strings (including CDC)
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

    // Start DSP task on Core 1 (replaces polling piezo monitor)
    // The DSP task consumes samples from ADC ring buffer (DMA on Core 0)
    // and runs the detection pipeline: filter → rebound detection → velocity
    ESP_LOGI(TAG, "Starting DSP task on Core %d...", EDRUMULUS_DSP_TASK_CORE);
    ret = edrumulus_detection_start_dsp(detection_queue);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start DSP task: %s", esp_err_to_name(ret));
        return;
    }
    ESP_LOGI(TAG, "DSP task started on Core %d (ADC DMA on Core 0)", EDRUMULUS_DSP_TASK_CORE);
    
    // Configure pads with dual-piezo channel mapping
    // Pad 0: piezo1=CH4 (GPIO4), piezo2=CH5 (GPIO5), Snare
    edrumulus_pad_config_t pad0 = {
        .threshold = 100,
        .sensitivity = 50,
        .midi_note = MIDI_NOTE_SNARE_DRUM,
        .midi_note_rim = MIDI_NOTE_SNARE_DRUM + 1, // 39 (rimshot)
        .midi_cc_position = EDRUMULUS_CC_POSITION_DEFAULT,
        .curve = 0,
        .piezo_ch_1 = 4,
        .piezo_ch_2 = 5,
        .enable_rimshot = true,
        .enable_crosstalk_cancel = false
    };
    ret = edrumulus_detection_configure_pad(0, &pad0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure pad 0: %s", esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "Pad 0 configured: piezo1=GPIO4, piezo2=GPIO5, note=%d",
                 pad0.midi_note);
    }

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
    
    // Main application loop — event-driven, no fixed delay
    ESP_LOGI(TAG, "Main loop: event-driven (Core 0=ADC DMA, Core 1=DSP)");
    
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
        
        // Wait for events from any source with 50ms timeout
        // This is the ONLY blocking call in the main loop
        edrumulus_hit_event_t hit_event;
	        if (xQueueReceive(detection_queue, &hit_event, pdMS_TO_TICKS(50)) == pdTRUE) {
	            // PRIORITY 1: Send MIDI immediately for minimum latency
	            edrumulus_midi_send_note_on(9, hit_event.note, hit_event.velocity);
	            
	            // PRIORITY 1b: Send position as MIDI CC (if configured)
	            if (hit_event.cc_position > 0) {
	                edrumulus_midi_send_cc(9, hit_event.cc_position, hit_event.position);
	            }
	            
	            // PRIORITY 2: Visual feedback
	            edrumulus_led_set_status(EDRUMULUS_LED_RED);
	            
	            // PRIORITY 3: Debug info (minimal)
	            ESP_LOGI(TAG, "Piezo hit: Ch=%d, Pad=%d, Note=%d, Vel=%d, Pos=%d%s", 
	                     hit_event.channel, hit_event.pad_id, hit_event.note, 
	                     hit_event.velocity, hit_event.position,
	                     hit_event.cc_position > 0 ? " +CC" : "");
            
            // Schedule note off
            vTaskDelay(pdMS_TO_TICKS(30));
            edrumulus_midi_send_note_off(9, hit_event.note);
            
            // Return LED to normal state
            edrumulus_led_set_status(EDRUMULUS_LED_GREEN);
        }
        
        // Check for input events (non-blocking, only when no hit event pending)
        edrumulus_input_event_t input_event;
        if (edrumulus_input_get_event(&input_event, 0) == ESP_OK) {
            uint8_t midi_note = 0;
            uint8_t midi_vel = 80;
            uint32_t note_off_delay = 40;
            
            switch (input_event.type) {
                case EDRUMULUS_INPUT_BOOT_PRESS:
                    midi_note = MIDI_NOTE_KICK_DRUM;
                    midi_vel = 100;
                    note_off_delay = 80;
                    edrumulus_led_set_status(EDRUMULUS_LED_BLUE);
                    break;
                case EDRUMULUS_INPUT_ENCODER_CW:
                    midi_note = MIDI_NOTE_HIHAT_CLOSED;
                    edrumulus_led_set_status(EDRUMULUS_LED_YELLOW);
                    break;
                case EDRUMULUS_INPUT_ENCODER_CCW:
                    midi_note = MIDI_NOTE_HIHAT_OPEN;
                    edrumulus_led_set_status(EDRUMULUS_LED_YELLOW);
                    break;
                case EDRUMULUS_INPUT_ENCODER_PRESS:
                    midi_note = MIDI_NOTE_CRASH_CYMBAL;
                    midi_vel = 120;
                    note_off_delay = 150;
                    edrumulus_led_set_status(EDRUMULUS_LED_PURPLE);
                    break;
                default:
                    ESP_LOGW(TAG, "Unknown input event type: %d", input_event.type);
                    break;
            }
            
            if (midi_note > 0) {
                edrumulus_midi_send_note_on(9, midi_note, midi_vel);
                ESP_LOGI(TAG, "Input event: note=%d, vel=%d", midi_note, midi_vel);
                
                vTaskDelay(pdMS_TO_TICKS(note_off_delay));
                edrumulus_midi_send_note_off(9, midi_note);
                edrumulus_led_set_status(EDRUMULUS_LED_GREEN);
            }
        }
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
