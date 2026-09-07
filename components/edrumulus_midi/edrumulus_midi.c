/**
 * @file edrumulus_midi.c
 * @brief ESP32 E-Drum Trigger System - MIDI Component Implementation
 */

#include "edrumulus_midi.h"
#include "edrumulus_core.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "tinyusb.h"
#include "tusb.h"

static const char *TAG = "edrumulus_midi";

// MIDI subsystem state
static bool g_midi_initialized = false;
static bool g_midi_running = false;
static edrumulus_midi_config_t g_midi_config = {0};
static TaskHandle_t g_midi_task_handle = NULL;

// Statistics
static uint32_t g_sent_events = 0;
static bool g_usb_connected = false;

// Forward declarations
static void midi_task(void *pvParameters);
static void midi_send_raw(uint8_t *data, size_t len);

esp_err_t edrumulus_midi_init(const edrumulus_midi_config_t *config)
{
    if (config == NULL) {
        ESP_LOGE(TAG, "MIDI configuration cannot be NULL");
        return ESP_ERR_INVALID_ARG;
    }

    if (g_midi_initialized) {
        ESP_LOGW(TAG, "MIDI already initialized");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Inicializando subsistema MIDI...");

    // Copy configuration
    g_midi_config = *config;

    // Verificar soporte USB OTG
    ESP_LOGI(TAG, "USB OTG Supported: %s", CONFIG_SOC_USB_OTG_SUPPORTED ? "YES" : "NO");
    ESP_LOGI(TAG, "TinyUSB MIDI Count: %d", CONFIG_TINYUSB_MIDI_COUNT);
#ifdef CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG
    ESP_LOGI(TAG, "USB Serial JTAG: ENABLED");
#else
    ESP_LOGI(TAG, "USB Serial JTAG: DISABLED");
#endif
    
    // NOTA: TinyUSB debe ser inicializado desde el main usando tinyusb_driver_install()
    // No inicializamos aquí para evitar conflictos
    ESP_LOGI(TAG, "TinyUSB será inicializado desde main usando tinyusb_driver_install()");
    
    // Verificar estado del dispositivo USB
    ESP_LOGI(TAG, "Verificando estado USB...");
    if (tud_mounted()) {
        ESP_LOGI(TAG, "Dispositivo USB montado");
    } else {
        ESP_LOGW(TAG, "Dispositivo USB no montado aún");
    }

    ESP_LOGI(TAG, "MIDI subsystem initialized (channel: %d)", config->default_channel);
    g_midi_initialized = true;
    
    return ESP_OK;
}

esp_err_t edrumulus_midi_start(void)
{
    if (!g_midi_initialized) {
        ESP_LOGE(TAG, "MIDI not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    if (g_midi_running) {
        ESP_LOGW(TAG, "MIDI already running");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Starting MIDI subsystem");

    // Create MIDI processing task
    BaseType_t task_ret = xTaskCreate(
        midi_task,
        "midi_task",
        4096,
        NULL,
        EDRUMULUS_TASK_PRIORITY_NORMAL,
        &g_midi_task_handle
    );

    if (task_ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create MIDI task");
        return ESP_ERR_NO_MEM;
    }

    // TinyUSB task is handled automatically by esp_tinyusb
    ESP_LOGI(TAG, "TinyUSB task will be managed automatically");

    g_midi_running = true;
    ESP_LOGI(TAG, "MIDI subsystem started");
    
    return ESP_OK;
}

esp_err_t edrumulus_midi_stop(void)
{
    if (!g_midi_running) {
        ESP_LOGW(TAG, "MIDI not running");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Stopping MIDI subsystem");

    // TinyUSB task is managed automatically by esp_tinyusb
    ESP_LOGI(TAG, "TinyUSB task managed automatically");

    // Stop tasks
    if (g_midi_task_handle) {
        vTaskDelete(g_midi_task_handle);
        g_midi_task_handle = NULL;
    }

    g_midi_running = false;
    ESP_LOGI(TAG, "MIDI subsystem stopped");
    
    return ESP_OK;
}

esp_err_t edrumulus_midi_send_note_on(uint8_t channel, uint8_t note, uint8_t velocity)
{
    if (!g_midi_running) {
        return ESP_ERR_INVALID_STATE;
    }

    if (channel > 15 || note > 127 || velocity == 0 || velocity > 127) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t midi_data[3] = {
        EDRUMULUS_MIDI_NOTE_ON | channel,
        note,
        velocity
    };

    midi_send_raw(midi_data, sizeof(midi_data));
    g_sent_events++;
    
    ESP_LOGD(TAG, "Note ON: ch=%d, note=%d, vel=%d", channel, note, velocity);
    return ESP_OK;
}

esp_err_t edrumulus_midi_send_note_off(uint8_t channel, uint8_t note)
{
    if (!g_midi_running) {
        return ESP_ERR_INVALID_STATE;
    }

    if (channel > 15 || note > 127) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t midi_data[3] = {
        EDRUMULUS_MIDI_NOTE_OFF | channel,
        note,
        0
    };

    midi_send_raw(midi_data, sizeof(midi_data));
    g_sent_events++;
    
    ESP_LOGD(TAG, "Note OFF: ch=%d, note=%d", channel, note);
    return ESP_OK;
}

esp_err_t edrumulus_midi_send_cc(uint8_t channel, uint8_t controller, uint8_t value)
{
    if (!g_midi_running) {
        return ESP_ERR_INVALID_STATE;
    }

    if (channel > 15 || controller > 127 || value > 127) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t midi_data[3] = {
        EDRUMULUS_MIDI_CONTROL_CHANGE | channel,
        controller,
        value
    };

    midi_send_raw(midi_data, sizeof(midi_data));
    g_sent_events++;
    
    ESP_LOGD(TAG, "CC: ch=%d, ctrl=%d, val=%d", channel, controller, value);
    return ESP_OK;
}

esp_err_t edrumulus_midi_queue_event(const edrumulus_midi_event_t *event)
{
    if (!g_midi_running || !event) {
        return ESP_ERR_INVALID_STATE;
    }

    if (g_midi_config.event_queue) {
        BaseType_t ret = xQueueSend(g_midi_config.event_queue, event, 0);
        return (ret == pdTRUE) ? ESP_OK : ESP_ERR_TIMEOUT;
    }

    return ESP_ERR_NOT_SUPPORTED;
}

bool edrumulus_midi_is_connected(void)
{
    return tud_midi_mounted();
}

esp_err_t edrumulus_midi_get_stats(uint32_t *sent_events, uint32_t *queue_depth)
{
    if (sent_events) {
        *sent_events = g_sent_events;
    }
    
    if (queue_depth && g_midi_config.event_queue) {
        *queue_depth = uxQueueMessagesWaiting(g_midi_config.event_queue);
    }
    
    return ESP_OK;
}

// Private functions
static void midi_send_raw(uint8_t *data, size_t len)
{
    if (len > 0) {
        // Send MIDI data via TinyUSB
        if (tud_midi_mounted()) {
            uint8_t packet[4] = {0};
            
            // Create USB MIDI packet
            if (len == 3) {
                packet[0] = 0x09; // Cable number 0, Code Index Number for 3-byte message
                packet[1] = data[0];
                packet[2] = data[1];
                packet[3] = data[2];
                
                tud_midi_stream_write(0, packet, 4);
                g_usb_connected = true;
                ESP_LOGD(TAG, "MIDI sent: %02X %02X %02X", data[0], data[1], data[2]);
            }
        } else {
            g_usb_connected = false;
            ESP_LOGD(TAG, "MIDI not mounted, data not sent");
        }
    }
}

static void midi_task(void *pvParameters)
{
    ESP_LOGI(TAG, "MIDI task started");
    
    edrumulus_midi_event_t event;
    
    while (g_midi_running) {
        if (g_midi_config.event_queue) {
            if (xQueueReceive(g_midi_config.event_queue, &event, pdMS_TO_TICKS(100)) == pdTRUE) {
                // Process MIDI event
                switch (event.type) {
                    case EDRUMULUS_MIDI_NOTE_ON:
                        edrumulus_midi_send_note_on(event.channel, event.note, event.velocity);
                        break;
                    case EDRUMULUS_MIDI_NOTE_OFF:
                        edrumulus_midi_send_note_off(event.channel, event.note);
                        break;
                    case EDRUMULUS_MIDI_CONTROL_CHANGE:
                        edrumulus_midi_send_cc(event.channel, event.note, event.velocity);
                        break;
                }
            }
        } else {
            vTaskDelay(pdMS_TO_TICKS(100));
        }
    }
    
    ESP_LOGI(TAG, "MIDI task ended");
    vTaskDelete(NULL);
}

// USB device task is handled automatically by esp_tinyusb

//--------------------------------------------------------------------+
// TinyUSB Callbacks for USB Events Debug
//--------------------------------------------------------------------+

// Invoked when device is mounted
void tud_mount_cb(void)
{
    ESP_LOGI(TAG, "*** USB DEVICE MOUNTED ***");
    g_usb_connected = true;
}

// Invoked when device is unmounted
void tud_umount_cb(void)
{
    ESP_LOGI(TAG, "*** USB DEVICE UNMOUNTED ***");
    g_usb_connected = false;
}

// Invoked when usb bus is suspended
void tud_suspend_cb(bool remote_wakeup_en)
{
    ESP_LOGI(TAG, "*** USB SUSPENDED (remote_wakeup: %s) ***", remote_wakeup_en ? "YES" : "NO");
    (void) remote_wakeup_en;
}

// Invoked when usb bus is resumed
void tud_resume_cb(void)
{
    ESP_LOGI(TAG, "*** USB RESUMED ***");
}

// Invoked when MIDI interface is mounted
void tud_midi_mount_cb(uint8_t itf)
{
    ESP_LOGI(TAG, "*** MIDI INTERFACE %d MOUNTED ***", itf);
    g_usb_connected = true;
}

// Invoked when MIDI interface is unmounted
void tud_midi_umount_cb(uint8_t itf)
{
    ESP_LOGI(TAG, "*** MIDI INTERFACE %d UNMOUNTED ***", itf);
    g_usb_connected = false;
}

// Invoked when received MIDI data
void tud_midi_rx_cb(uint8_t itf)
{
    ESP_LOGD(TAG, "MIDI RX data received on interface %d", itf);
    // We don't process incoming MIDI for now, just log it
    uint8_t packet[4];
    while (tud_midi_available()) {
        if (tud_midi_packet_read(packet)) {
            ESP_LOGD(TAG, "MIDI RX: %02X %02X %02X %02X", packet[0], packet[1], packet[2], packet[3]);
        }
    }
}