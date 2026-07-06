/**
 * @file edrumulus_midi.h
 * @brief ESP32 E-Drum Trigger System - MIDI Component
 * 
 * USB MIDI interface implementation using esp_tinyusb for ESP32-C3
 */

#ifndef EDRUMULUS_MIDI_H
#define EDRUMULUS_MIDI_H

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#ifdef __cplusplus
extern "C" {
#endif

// MIDI Configuration
#define EDRUMULUS_MIDI_CHANNEL_DEFAULT 9  // Channel 10 (0-indexed)
#define EDRUMULUS_MIDI_VELOCITY_MIN 1
#define EDRUMULUS_MIDI_VELOCITY_MAX 127

// Standard MIDI Note Numbers for Drums (GM Standard)
#define MIDI_NOTE_KICK_DRUM 36
#define MIDI_NOTE_SNARE_DRUM 38
#define MIDI_NOTE_HIHAT_CLOSED 42
#define MIDI_NOTE_HIHAT_OPEN 46
#define MIDI_NOTE_CRASH_CYMBAL 49
#define MIDI_NOTE_RIDE_CYMBAL 51
#define MIDI_NOTE_TOM_HIGH 50
#define MIDI_NOTE_TOM_MID 47
#define MIDI_NOTE_TOM_LOW 43

/**
 * @brief MIDI message types
 */
typedef enum {
    EDRUMULUS_MIDI_NOTE_ON = 0x90,
    EDRUMULUS_MIDI_NOTE_OFF = 0x80,
    EDRUMULUS_MIDI_CONTROL_CHANGE = 0xB0
} edrumulus_midi_msg_type_t;

/**
 * @brief MIDI event structure
 */
typedef struct {
    edrumulus_midi_msg_type_t type;  ///< MIDI message type
    uint8_t channel;                 ///< MIDI channel (0-15)
    uint8_t note;                    ///< MIDI note number (0-127)
    uint8_t velocity;                ///< MIDI velocity (0-127)
    uint32_t timestamp_us;           ///< Timestamp in microseconds
} edrumulus_midi_event_t;

/**
 * @brief MIDI configuration structure
 */
typedef struct {
    uint8_t default_channel;         ///< Default MIDI channel
    bool enable_note_off;            ///< Send note off messages
    uint16_t note_off_delay_ms;      ///< Delay before sending note off
    QueueHandle_t event_queue;       ///< Queue for MIDI events
} edrumulus_midi_config_t;

/**
 * @brief Initialize MIDI subsystem
 * 
 * @param config MIDI configuration
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
esp_err_t edrumulus_midi_init(const edrumulus_midi_config_t *config);

/**
 * @brief Start MIDI subsystem
 * 
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
esp_err_t edrumulus_midi_start(void);

/**
 * @brief Stop MIDI subsystem
 * 
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
esp_err_t edrumulus_midi_stop(void);

/**
 * @brief Send MIDI note on message
 * 
 * @param channel MIDI channel (0-15)
 * @param note MIDI note number (0-127)
 * @param velocity MIDI velocity (1-127)
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
esp_err_t edrumulus_midi_send_note_on(uint8_t channel, uint8_t note, uint8_t velocity);

/**
 * @brief Send MIDI note off message
 * 
 * @param channel MIDI channel (0-15)
 * @param note MIDI note number (0-127)
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
esp_err_t edrumulus_midi_send_note_off(uint8_t channel, uint8_t note);

/**
 * @brief Send MIDI control change message
 * 
 * @param channel MIDI channel (0-15)
 * @param controller Controller number (0-127)
 * @param value Controller value (0-127)
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
esp_err_t edrumulus_midi_send_cc(uint8_t channel, uint8_t controller, uint8_t value);

/**
 * @brief Queue MIDI event for processing
 * 
 * @param event MIDI event to queue
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
esp_err_t edrumulus_midi_queue_event(const edrumulus_midi_event_t *event);

/**
 * @brief Check if USB MIDI is connected
 * 
 * @return bool true if connected, false otherwise
 */
bool edrumulus_midi_is_connected(void);

/**
 * @brief Get MIDI statistics
 * 
 * @param sent_events Number of events sent
 * @param queue_depth Current queue depth
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
esp_err_t edrumulus_midi_get_stats(uint32_t *sent_events, uint32_t *queue_depth);

#ifdef __cplusplus
}
#endif

#endif // EDRUMULUS_MIDI_H