/**
 * @file edrumulus_synthtest.h
 * @brief ESP32 E-Drum Trigger - Synthetic piezo signal generator (test mode)
 *
 * Generates synthetic drum-hit waveforms (damped sine burst with exponential
 * decay) and injects them into the ADC ring buffer, bypassing the ADC DMA.
 * The DSP pipeline (filter -> rebound -> velocity -> TDOA -> MIDI) processes
 * the injected samples exactly as if they came from real piezos.
 *
 * While synthetic mode is enabled the ADC continuous conversion is STOPPED,
 * so the ring buffer always keeps a single producer (issue #19).
 *
 * Waveform model (per piezo channel):
 *   v(t) = 2048 + 2047 * A * env(t) * sin(2*pi*f*(t - t0))
 *   env(t) = t/rise            (t < rise, attack)
 *   env(t) = exp(-(t-rise)/tau (t >= rise, exponential decay)
 *
 * Position mapping (consistent with the TDOA algorithm in the pipeline):
 *   dt = t_peak2 - t_peak1 = ((pos - 63.5) / 63.5) * TDOA_MAX_DELTA_US
 *   pos 127 -> piezo2 peaks 3 ms after piezo1, pos 0 -> 3 ms before.
 * Both channels use the same amplitude, so the amplitude-ratio estimator
 * stays near center and TDOA carries the position (70% hybrid weight).
 *
 * Known characterization: hit velocity is computed from the peak of the
 * DC-free (band-passed) signal, so observed MIDI velocity ~= min(requested, 63).
 */

#ifndef EDRUMULUS_SYNTHTEST_H
#define EDRUMULUS_SYNTHTEST_H

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Default synthetic hit parameters
#define EDRUMULUS_SYNTHTEST_DEFAULT_FREQ_HZ     340.0f  ///< Resonant frequency (Hz). Passes the effective band-pass (design@1kHz run@8kHz -> ~320 Hz..3.2 kHz)
#define EDRUMULUS_SYNTHTEST_DEFAULT_DECAY_MS    120.0f  ///< Exponential decay constant (ms)
#define EDRUMULUS_SYNTHTEST_DEFAULT_RISE_MS     1.0f    ///< Attack time (ms)
#define EDRUMULUS_SYNTHTEST_DEFAULT_DURATION_MS 250.0f  ///< Total burst length (ms)

#define EDRUMULUS_SYNTHTEST_MIN_AUTO_INTERVAL_MS 50     ///< Minimum auto-test period (ms)

/**
 * @brief Synthetic hit generation parameters (adjustable at runtime)
 */
typedef struct {
    float frequency_hz;     ///< Resonant frequency of the burst (Hz)
    float decay_ms;         ///< Exponential decay time constant (ms)
    float rise_ms;          ///< Attack/rise time (ms)
    float duration_ms;      ///< Total burst duration (ms)
} edrumulus_synthtest_params_t;

/**
 * @brief Initialize the synthetic test subsystem (no task started yet)
 *
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
esp_err_t edrumulus_synthtest_init(void);

/**
 * @brief Enable synthetic test mode
 *
 * Stops the ADC continuous conversion (single-producer rule), resets the
 * ring buffer and starts the generator task at 8 kHz per channel.
 *
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
esp_err_t edrumulus_synthtest_mode_enable(void);

/**
 * @brief Disable synthetic test mode
 *
 * Stops the generator task, resets the ring buffer and restarts the ADC
 * continuous conversion (only if it was running when the mode was enabled).
 *
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
esp_err_t edrumulus_synthtest_mode_disable(void);

/**
 * @brief Check whether synthetic test mode is enabled
 */
bool edrumulus_synthtest_mode_is_enabled(void);

/**
 * @brief Queue a synthetic hit (on-demand)
 *
 * @param velocity Requested velocity (1-127). Maps to burst amplitude.
 * @param position Requested position (0-127, 64=center). Maps to inter-piezo delay.
 * @return esp_err_t ESP_OK on success, ESP_ERR_INVALID_STATE if mode disabled
 */
esp_err_t edrumulus_synthtest_trigger(uint8_t velocity, uint8_t position);

/**
 * @brief Start periodic synthetic hits (continuous test)
 *
 * @param interval_ms Period between hits (>= 50 ms)
 * @return esp_err_t ESP_OK on success, ESP_ERR_INVALID_STATE if mode disabled
 */
esp_err_t edrumulus_synthtest_auto_start(uint32_t interval_ms);

/**
 * @brief Stop periodic synthetic hits
 *
 * @return esp_err_t ESP_OK on success
 */
esp_err_t edrumulus_synthtest_auto_stop(void);

/**
 * @brief Check whether periodic auto mode is active
 */
bool edrumulus_synthtest_auto_is_active(void);

/**
 * @brief Adjust generation parameters at runtime
 *
 * @param params New parameters (all fields used; NULL rejected)
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
esp_err_t edrumulus_synthtest_set_params(const edrumulus_synthtest_params_t *params);

/**
 * @brief Get current generation parameters
 */
esp_err_t edrumulus_synthtest_get_params(edrumulus_synthtest_params_t *params);

/**
 * @brief Total number of synthetic hits queued for injection since enable
 */
uint32_t edrumulus_synthtest_get_hits_injected(void);

#ifdef __cplusplus
}
#endif

#endif // EDRUMULUS_SYNTHTEST_H
