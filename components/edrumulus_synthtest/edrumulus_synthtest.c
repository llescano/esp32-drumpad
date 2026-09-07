/**
 * @file edrumulus_synthtest.c
 * @brief ESP32 E-Drum Trigger - Synthetic piezo signal generator (issue #19)
 *
 * Producer alternative to the ADC DMA callback: a task paces 8 kHz per
 * channel, synthesizes damped sine bursts for piezo1 (ADC CH4/GPIO4) and
 * piezo2 (ADC CH5/GPIO5) and pushes them into the detection ring buffer via
 * edrumulus_detection_inject_sample(). The DSP task on Core 1 consumes them
 * through the regular pipeline.
 */

#include "edrumulus_synthtest.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_timer.h"
#include "edrumulus_detection.h"

#include <math.h>
#include <string.h>

static const char *TAG = "edrumulus_synthtest";

// ADC channels of pad 0 (must match g_adc_pattern in edrumulus_detection.c)
#define SYNTH_CH_PIEZO1         4
#define SYNTH_CH_PIEZO2         5

// Timeline: pair period = 125 us -> 8 kHz per channel, like the real ADC
#define SYNTH_PAIR_PERIOD_US    125
#define SYNTH_PAIRS_PER_TICK    8       // 1 ms per tick -> 16 samples
#define SYNTH_TASK_STACK_SIZE   4096
#define SYNTH_TASK_PRIORITY     4       // below DSP task (5)
#define SYNTH_TASK_CORE         0
#define SYNTH_PENDING_QUEUE_LEN 8
#define SYNTH_MAX_ACTIVE_BURSTS 6
#define SYNTH_TRIGGER_LEAD_US   2000    // schedule queued hits 2 ms ahead
#define SYNTH_ADC_BASELINE      2048    // mid-scale bias, like the analog frontend

/**
 * @brief An active (or queued) synthetic hit
 */
typedef struct {
    bool     active;
    uint64_t start_us;      ///< Synthetic-timeline start of the piezo1 burst
    float    amp;           ///< Peak amplitude in normalized units (0-1)
    int32_t  delta_us;      ///< Piezo2 burst offset (TDOA position encoding)
    uint8_t  velocity;      ///< Requested velocity
    uint8_t  position;      ///< Requested position
} synth_burst_t;

/**
 * @brief Item received from the trigger queue (filled by public API)
 */
typedef struct {
    uint8_t  velocity;
    uint8_t  position;
    uint32_t delay_us;      ///< Lead time from current timeline position
} synth_trigger_msg_t;

static struct {
    bool        mode_enabled;
    bool        adc_was_running;    ///< ADC state when the mode was enabled
    TaskHandle_t task_handle;
    volatile bool task_running;

    QueueHandle_t pending_queue;    ///< synth_trigger_msg_t from public API

    edrumulus_synthtest_params_t params;

    // Timeline (only touched by the synth task)
    uint64_t timeline_us;           ///< Timestamp of the next sample pair
    uint64_t next_auto_us;          ///< Timeline time of the next auto hit (0=arm)
    uint32_t hits_injected;
} s_state = {
    .params = {
        .frequency_hz = EDRUMULUS_SYNTHTEST_DEFAULT_FREQ_HZ,
        .decay_ms     = EDRUMULUS_SYNTHTEST_DEFAULT_DECAY_MS,
        .rise_ms      = EDRUMULUS_SYNTHTEST_DEFAULT_RISE_MS,
        .duration_ms  = EDRUMULUS_SYNTHTEST_DEFAULT_DURATION_MS,
    },
};

// Auto mode flags (written by API, read by task; single-word writes are atomic)
static volatile bool     s_auto_active = false;
static volatile uint32_t s_auto_interval_ms = 500;

// === WAVEFORM GENERATION ===

/**
 * @brief Map requested velocity to burst amplitude.
 *
 * The pipeline computes velocity = peak * 127 on the DC-free filtered signal,
 * whose peak is about half the burst amplitude -> amplitude = 2*vel/127.
 */
static float synth_velocity_to_amplitude(uint8_t velocity)
{
    float amp = (2.0f * (float)velocity) / 127.0f;
    if (amp > 1.0f) amp = 1.0f;
    if (amp < 0.0f) amp = 0.0f;
    return amp;
}

/**
 * @brief Map requested position to the piezo2 burst offset (TDOA encoding).
 *
 * The pipeline maps dt = t_peak2 - t_peak1 back to position with
 * pos = (dt/MAX + 1) * 63.5; inverting it keeps both consistent.
 */
static int32_t synth_position_to_delta_us(uint8_t position)
{
    return (int32_t)(((float)position - 63.5f) / 63.5f *
                     (float)EDRUMULUS_TDOA_MAX_DELTA_US);
}

/**
 * @brief Synthesize one sample of a damped sine burst (baseline if outside)
 */
static uint16_t synth_waveform_value(uint64_t t_us, uint64_t start_us, float amp)
{
    if (t_us < start_us) return SYNTH_ADC_BASELINE;

    float t_ms = (float)(double)(t_us - start_us) / 1000.0f;
    if (t_ms > s_state.params.duration_ms) return SYNTH_ADC_BASELINE;

    float env;
    if (t_ms < s_state.params.rise_ms) {
        env = t_ms / s_state.params.rise_ms;                    // attack
    } else {
        env = expf(-(t_ms - s_state.params.rise_ms) /
                   s_state.params.decay_ms);                    // decay
    }

    float t_s = (float)(double)(t_us - start_us) / 1000000.0f;
    float v = (float)SYNTH_ADC_BASELINE +
              2047.0f * amp * env *
              sinf(2.0f * (float)M_PI * s_state.params.frequency_hz * t_s);

    if (v < 0.0f) v = 0.0f;
    if (v > 4095.0f) v = 4095.0f;
    return (uint16_t)v;
}

// === BURST SCHEDULING (task context) ===

static bool schedule_burst(uint8_t velocity, uint8_t position, uint64_t start_us,
                           synth_burst_t *active)
{
    for (int i = 0; i < SYNTH_MAX_ACTIVE_BURSTS; i++) {
        if (!active[i].active) {
            active[i].active    = true;
            active[i].start_us  = start_us;
            active[i].amp       = synth_velocity_to_amplitude(velocity);
            active[i].delta_us  = synth_position_to_delta_us(position);
            active[i].velocity  = velocity;
            active[i].position  = position;
            s_state.hits_injected++;
            return true;
        }
    }
    ESP_LOGW(TAG, "No free burst slot, hit dropped");
    return false;
}

static bool burst_is_expired(const synth_burst_t *b, uint64_t now_us)
{
    uint64_t end_us = b->start_us +
                      (uint64_t)(s_state.params.duration_ms * 1000.0f) +
                      (uint64_t)((b->delta_us < 0) ? -b->delta_us : b->delta_us);
    return now_us > (end_us + SYNTH_PAIR_PERIOD_US);
}

// === GENERATOR TASK ===

static void synthtest_task(void *arg)
{
    (void)arg;
    synth_burst_t active[SYNTH_MAX_ACTIVE_BURSTS];
    memset(active, 0, sizeof(active));

    ESP_LOGI(TAG, "Synthetic generator task started on Core %d (8 kHz/ch)",
             xPortGetCoreID());

    while (s_state.task_running) {
        // 1. Drain on-demand triggers queued by the public API
        synth_trigger_msg_t msg;
        while (xQueueReceive(s_state.pending_queue, &msg, 0) == pdTRUE) {
            if (schedule_burst(msg.velocity, msg.position,
                               s_state.timeline_us + msg.delay_us, active)) {
                ESP_LOGI(TAG, "Hit queued: vel=%u pos=%u (dt=%d us, amp=%.2f)",
                         msg.velocity, msg.position,
                         synth_position_to_delta_us(msg.position),
                         synth_velocity_to_amplitude(msg.velocity));
            }
        }

        // 2. Periodic auto hits
        if (s_auto_active) {
            if (s_state.next_auto_us == 0) {
                s_state.next_auto_us = s_state.timeline_us +
                                       (uint64_t)s_auto_interval_ms * 1000ULL;
            }
            if (s_state.timeline_us >= s_state.next_auto_us) {
                schedule_burst(100, EDRUMULUS_POSITION_CENTER,
                               s_state.next_auto_us, active);
                s_state.next_auto_us += (uint64_t)s_auto_interval_ms * 1000ULL;
            }
        } else {
            s_state.next_auto_us = 0;
        }

        // 3. Generate this tick's sample pairs and inject them
        for (int k = 0; k < SYNTH_PAIRS_PER_TICK; k++) {
            uint32_t ts = (uint32_t)(s_state.timeline_us & 0xFFFFFFFFULL);

            edrumulus_adc_sample_t s1 = {
                .channel = SYNTH_CH_PIEZO1,
                .raw_value = SYNTH_ADC_BASELINE,
                .timestamp_us = ts,
            };
            edrumulus_adc_sample_t s2 = {
                .channel = SYNTH_CH_PIEZO2,
                .raw_value = SYNTH_ADC_BASELINE,
                .timestamp_us = ts,
            };

            for (int i = 0; i < SYNTH_MAX_ACTIVE_BURSTS; i++) {
                if (!active[i].active) continue;
                s1.raw_value = synth_waveform_value(s_state.timeline_us,
                                                    active[i].start_us,
                                                    active[i].amp);
                s2.raw_value = synth_waveform_value(s_state.timeline_us,
                                                    active[i].start_us + active[i].delta_us,
                                                    active[i].amp);
            }

            edrumulus_detection_inject_sample(&s1);
            edrumulus_detection_inject_sample(&s2);
            s_state.timeline_us += SYNTH_PAIR_PERIOD_US;
        }

        // 4. Expire finished bursts
        for (int i = 0; i < SYNTH_MAX_ACTIVE_BURSTS; i++) {
            if (active[i].active && burst_is_expired(&active[i], s_state.timeline_us)) {
                ESP_LOGI(TAG, "Burst done: vel=%u pos=%u",
                         active[i].velocity, active[i].position);
                active[i].active = false;
            }
        }

        vTaskDelay(pdMS_TO_TICKS(1));
    }

    ESP_LOGI(TAG, "Synthetic generator task stopped (%lu hits injected)",
             (unsigned long)s_state.hits_injected);
    s_state.task_handle = NULL;
    vTaskDelete(NULL);
}

// === PUBLIC API ===

esp_err_t edrumulus_synthtest_init(void)
{
    if (s_state.pending_queue != NULL) {
        ESP_LOGW(TAG, "Synthetic test already initialized");
        return ESP_OK;
    }

    s_state.pending_queue = xQueueCreate(SYNTH_PENDING_QUEUE_LEN,
                                         sizeof(synth_trigger_msg_t));
    if (s_state.pending_queue == NULL) {
        ESP_LOGE(TAG, "Failed to create trigger queue");
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "Synthetic test subsystem ready (freq=%.0f Hz decay=%.0f ms)",
             s_state.params.frequency_hz, s_state.params.decay_ms);
    return ESP_OK;
}

esp_err_t edrumulus_synthtest_mode_enable(void)
{
    if (s_state.mode_enabled) {
        ESP_LOGW(TAG, "Synthetic mode already enabled");
        return ESP_OK;
    }
    if (s_state.pending_queue == NULL) {
        esp_err_t ret = edrumulus_synthtest_init();
        if (ret != ESP_OK) return ret;
    }

    // Single-producer rule: stop the ADC DMA before becoming the producer
    s_state.adc_was_running = edrumulus_detection_adc_is_running();
    if (s_state.adc_was_running) {
        esp_err_t ret = edrumulus_detection_adc_continuous_stop();
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to stop ADC: %s", esp_err_to_name(ret));
            return ret;
        }
    }
    edrumulus_detection_ringbuf_reset();

    s_state.timeline_us = (uint64_t)esp_timer_get_time();
    s_state.hits_injected = 0;
    s_state.task_running = true;

    BaseType_t ok = xTaskCreatePinnedToCore(
        synthtest_task,
        "synthtest",
        SYNTH_TASK_STACK_SIZE,
        NULL,
        SYNTH_TASK_PRIORITY,
        &s_state.task_handle,
        SYNTH_TASK_CORE);

    if (ok != pdPASS) {
        s_state.task_running = false;
        if (s_state.adc_was_running) {
            edrumulus_detection_adc_continuous_start();
        }
        ESP_LOGE(TAG, "Failed to create generator task");
        return ESP_FAIL;
    }

    s_state.mode_enabled = true;
    ESP_LOGI(TAG, "Synthetic mode ENABLED (ADC stopped, generator on Core %d)",
             SYNTH_TASK_CORE);
    return ESP_OK;
}

esp_err_t edrumulus_synthtest_mode_disable(void)
{
    if (!s_state.mode_enabled) {
        return ESP_OK;
    }

    edrumulus_synthtest_auto_stop();
    s_state.task_running = false;
    vTaskDelay(pdMS_TO_TICKS(50));  // allow the task to exit its loop
    s_state.task_handle = NULL;

    edrumulus_detection_ringbuf_reset();

    if (s_state.adc_was_running && !edrumulus_detection_adc_is_running()) {
        esp_err_t ret = edrumulus_detection_adc_continuous_start();
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to restart ADC: %s", esp_err_to_name(ret));
            return ret;
        }
    }

    s_state.mode_enabled = false;
    ESP_LOGI(TAG, "Synthetic mode DISABLED (ADC restored)");
    return ESP_OK;
}

bool edrumulus_synthtest_mode_is_enabled(void)
{
    return s_state.mode_enabled;
}

esp_err_t edrumulus_synthtest_trigger(uint8_t velocity, uint8_t position)
{
    if (!s_state.mode_enabled || s_state.pending_queue == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (velocity == 0 || velocity > 127) velocity = 100;
    if (position > 127) position = EDRUMULUS_POSITION_CENTER;

    synth_trigger_msg_t msg = {
        .velocity = velocity,
        .position = position,
        .delay_us = SYNTH_TRIGGER_LEAD_US,
    };
    if (xQueueSend(s_state.pending_queue, &msg, 0) != pdTRUE) {
        ESP_LOGW(TAG, "Trigger queue full, hit dropped");
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

esp_err_t edrumulus_synthtest_auto_start(uint32_t interval_ms)
{
    if (!s_state.mode_enabled) {
        return ESP_ERR_INVALID_STATE;
    }
    if (interval_ms < EDRUMULUS_SYNTHTEST_MIN_AUTO_INTERVAL_MS) {
        interval_ms = EDRUMULUS_SYNTHTEST_MIN_AUTO_INTERVAL_MS;
    }
    s_auto_interval_ms = interval_ms;
    s_auto_active = true;
    ESP_LOGI(TAG, "Auto test started (interval=%lu ms)", (unsigned long)interval_ms);
    return ESP_OK;
}

esp_err_t edrumulus_synthtest_auto_stop(void)
{
    s_auto_active = false;
    return ESP_OK;
}

bool edrumulus_synthtest_auto_is_active(void)
{
    return s_auto_active;
}

esp_err_t edrumulus_synthtest_set_params(const edrumulus_synthtest_params_t *params)
{
    if (params == NULL) return ESP_ERR_INVALID_ARG;
    if (params->frequency_hz <= 0.0f || params->frequency_hz > 4000.0f) {
        return ESP_ERR_INVALID_ARG;
    }
    if (params->decay_ms <= 0.0f || params->rise_ms <= 0.0f ||
        params->duration_ms <= 0.0f) {
        return ESP_ERR_INVALID_ARG;
    }
    s_state.params = *params;
    ESP_LOGI(TAG, "Params set: freq=%.1f Hz decay=%.1f ms rise=%.2f ms dur=%.0f ms",
             params->frequency_hz, params->decay_ms, params->rise_ms,
             params->duration_ms);
    return ESP_OK;
}

esp_err_t edrumulus_synthtest_get_params(edrumulus_synthtest_params_t *params)
{
    if (params == NULL) return ESP_ERR_INVALID_ARG;
    *params = s_state.params;
    return ESP_OK;
}

uint32_t edrumulus_synthtest_get_hits_injected(void)
{
    return s_state.hits_injected;
}
