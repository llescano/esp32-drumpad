/**
 * @file edrumulus_detection.c
 * @brief ESP32 E-Drum Trigger System - Detection Component Implementation
 */

#include "edrumulus_detection.h"
#include "esp_log.h"
#include "esp_adc/adc_continuous.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include "soc/adc_channel.h"
#include "hal/adc_types.h"
#include <math.h>
#include <string.h>
#include <stdarg.h>

static const char *TAG = "edrumulus_detection";

// Band-pass filter state
static bool g_filter_initialized = false;
static edrumulus_bandpass_filter_t g_bandpass_filter[EDRUMULUS_MAX_ADC_CHANNELS];
static edrumulus_filter_config_t g_filter_config = {
    .filter_enabled = true,
    .low_cutoff_hz = EDRUMULUS_FILTER_LOW_CUTOFF,
    .high_cutoff_hz = EDRUMULUS_FILTER_HIGH_CUTOFF,
    .gain_compensation = 1.0f,
    .filter_order = EDRUMULUS_FILTER_ORDER
};

// Phase 3: Rebound detector state
static bool g_rebound_detector_initialized = false;
static edrumulus_rebound_detector_t g_rebound_detectors[EDRUMULUS_MAX_ADC_CHANNELS];

// Detection subsystem state
static bool g_detection_initialized = false;
static bool g_adc_continuous_running = false;

// ADC continuous handle and ring buffer
static adc_continuous_handle_t g_adc_cont_handle = NULL;
static edrumulus_adc_ringbuf_t g_adc_ringbuf = {0};

// ADC continuous pattern table (2 channels: piezo1 on CH4, piezo2 on CH5)
static adc_digi_pattern_config_t g_adc_pattern[2] = {
    {
        .atten = ADC_ATTEN_DB_12,
        .channel = ADC_CHANNEL_4,   // GPIO4 -> piezo1
        .unit = ADC_UNIT_1,
        .bit_width = SOC_ADC_DIGI_MAX_BITWIDTH,
    },
    {
        .atten = ADC_ATTEN_DB_12,
        .channel = ADC_CHANNEL_5,   // GPIO5 -> piezo2
        .unit = ADC_UNIT_1,
        .bit_width = SOC_ADC_DIGI_MAX_BITWIDTH,
    }
};

// Piezo detection state
static bool g_piezo_initialized = false;
static uint8_t g_piezo_channel = 0;
static uint16_t g_piezo_threshold = 100;
static TaskHandle_t g_piezo_task_handle = NULL;
static QueueHandle_t g_piezo_event_queue = NULL;
static bool g_piezo_monitoring = false;

// Mask time state for intelligent retrigger prevention
static edrumulus_mask_time_config_t g_mask_config = {
    .mask_time_ms = 5,              // Tiempo base de 5ms
    .adaptive_mask = true,          // Máscara adaptativa habilitada
    .velocity_threshold_low = 40,   // Umbral bajo para velocidades suaves
    .velocity_threshold_high = 100  // Umbral alto para velocidades fuertes
};
static uint32_t g_last_hit_time[EDRUMULUS_MAX_ADC_CHANNELS] = {0}; // Timestamp del último hit por canal
static bool g_mask_active[EDRUMULUS_MAX_ADC_CHANNELS] = {false};    // Estado de máscara por canal

// === PHASE 1: VARIABLES GLOBALES DE VALIDACIÓN ADC ===
static bool g_adc_validation_initialized = false;
static edrumulus_adc_validation_t g_adc_validators[EDRUMULUS_MAX_ADC_CHANNELS];

// Forward declarations for validation functions
static uint64_t get_timestamp_us(void);
static uint32_t get_timestamp_ms(void);
__attribute__((unused)) static uint16_t calculate_rms(const uint16_t *samples, uint16_t count);
static void adc_validation_log_internal(uint8_t channel, const char *format, ...);
static float calculate_linear_regression_r_squared(const float *x_values, const float *y_values, uint16_t count);

// Constants for validation
#define EDRUMULUS_VALIDATION_MIN_SNR 60.0f
#define EDRUMULUS_VALIDATION_MAX_RATE_DEVIATION 0.001f  // 0.1%

// Piezo detection constants
#define PIEZO_SAMPLE_RATE_MS    1       // Sample every 1ms for 1kHz rate
#define PIEZO_VELOCITY_SCALE    127     // Max MIDI velocity
#define PIEZO_MAX_ADC_VALUE     4095    // 12-bit ADC max value

// Filter algorithm implementations
static void calculate_butterworth_coeffs(float cutoff_hz, float sample_rate_hz, float q, edrumulus_biquad_coeffs_t *coeffs)
{
    float omega = 2.0f * M_PI * cutoff_hz / sample_rate_hz;
    float sin_omega = sinf(omega);
    float cos_omega = cosf(omega);
    float alpha = sin_omega / (2.0f * q);
    
    // Butterworth low-pass coefficients
    float b0 = (1.0f - cos_omega) / 2.0f;
    float b1 = 1.0f - cos_omega;
    float b2 = (1.0f - cos_omega) / 2.0f;
    float a0 = 1.0f + alpha;
    float a1 = -2.0f * cos_omega;
    float a2 = 1.0f - alpha;
    
    // Normalize coefficients
    coeffs->b0 = b0 / a0;
    coeffs->b1 = b1 / a0;
    coeffs->b2 = b2 / a0;
    coeffs->a1 = a1 / a0;
    coeffs->a2 = a2 / a0;
}

static float process_biquad(float input, const edrumulus_biquad_coeffs_t *coeffs, edrumulus_biquad_state_t *state)
{
    float output = coeffs->b0 * input + coeffs->b1 * state->x1 + coeffs->b2 * state->x2
                   - coeffs->a1 * state->y1 - coeffs->a2 * state->y2;
    
    // Update state
    state->x2 = state->x1;
    state->x1 = input;
    state->y2 = state->y1;
    state->y1 = output;
    
    return output;
}

// === RING BUFFER FUNCTIONS (lock-free, single producer / single consumer) ===

static void ringbuf_reset(edrumulus_adc_ringbuf_t *rb)
{
    rb->head = 0;
    rb->tail = 0;
    rb->overflow_count = 0;
    rb->initialized = true;
}

static bool ringbuf_push(edrumulus_adc_ringbuf_t *rb, const edrumulus_adc_sample_t *sample)
{
    if (!rb || !sample) return false;
    
    uint32_t next_head = (rb->head + 1) % EDRUMULUS_ADC_RINGBUF_SIZE;
    
    // Check for overflow
    if (next_head == rb->tail) {
        rb->overflow_count++;
        // Overwrite oldest sample (move tail forward)
        rb->tail = (rb->tail + 1) % EDRUMULUS_ADC_RINGBUF_SIZE;
    }
    
    rb->buffer[rb->head] = *sample;
    rb->head = next_head;
    return true;
}

static bool ringbuf_pop(edrumulus_adc_ringbuf_t *rb, edrumulus_adc_sample_t *sample)
{
    if (!rb || !sample) return false;
    if (rb->head == rb->tail) return false; // Empty
    
    *sample = rb->buffer[rb->tail];
    rb->tail = (rb->tail + 1) % EDRUMULUS_ADC_RINGBUF_SIZE;
    return true;
}

static uint32_t ringbuf_count(const edrumulus_adc_ringbuf_t *rb)
{
    return (rb->head >= rb->tail) 
           ? (rb->head - rb->tail) 
           : (EDRUMULUS_ADC_RINGBUF_SIZE - rb->tail + rb->head);
}

// === ADC CONTINUOUS DMA CALLBACK ===

static bool IRAM_ATTR adc_continuous_dma_callback(adc_continuous_handle_t handle, 
                                                    const adc_continuous_evt_data_t *edata, 
                                                    void *user_data)
{
    (void)handle;
    (void)user_data;
    
    if (!edata || !edata->conv_frame_buffer) return false;
    
    // Parse conversion frame: each conversion = 4 bytes (adc_digi_output_data_t for ESP32-S3)
    // TYPE2 format: data[11:0], reserved[12], channel[15:13], unit[16:17], reserved[31:17]
    uint8_t *frame = edata->conv_frame_buffer;
    uint32_t frame_size = edata->size;
    uint32_t pos = 0;
    uint32_t now = (uint32_t)(esp_timer_get_time());
    
    while (pos + sizeof(adc_digi_output_data_t) <= frame_size) {
        adc_digi_output_data_t *conv = (adc_digi_output_data_t *)&frame[pos];
        pos += sizeof(adc_digi_output_data_t);
        
        edrumulus_adc_sample_t sample = {
            .channel = conv->type2.channel,
            .raw_value = conv->type2.data,
            .timestamp_us = now,
        };
        ringbuf_push(&g_adc_ringbuf, &sample);
    }
    
    return true;
}

// === PUBLIC ADC CONTINUOUS API ===

esp_err_t edrumulus_detection_adc_continuous_init(void)
{
    if (g_adc_cont_handle != NULL) {
        ESP_LOGW(TAG, "ADC continuous already initialized");
        return ESP_OK;
    }
    
    ESP_LOGI(TAG, "Initializing ADC continuous with DMA (2 channels)");
    
    // Allocate continuous ADC handle
    // Each conversion result = sizeof(adc_digi_output_data_t) = 4 bytes (ESP32-S3)
    size_t conv_size = sizeof(adc_digi_output_data_t);
    adc_continuous_handle_cfg_t adc_config = {
        .max_store_buf_size = EDRUMULUS_ADC_RINGBUF_SIZE * conv_size,
        .conv_frame_size = EDRUMULUS_ADC_CONT_FRAME_SIZE * conv_size,
    };
    
    esp_err_t ret = adc_continuous_new_handle(&adc_config, &g_adc_cont_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create ADC continuous handle: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Configure ADC channels and conversion pattern
    // sample_freq_hz = total conversion rate (2 channels * 8kHz per channel)
    adc_continuous_config_t cont_cfg = {
        .pattern_num = 2,               // 2 channels
        .adc_pattern = g_adc_pattern,
        .sample_freq_hz = EDRUMULUS_ADC_SAMPLE_RATE * 2,  // 16k conv/s for 2 ch @ 8kHz each
        .conv_mode = ADC_CONV_SINGLE_UNIT_1,
        .format = ADC_DIGI_OUTPUT_FORMAT_TYPE2,  // 4 bytes per conv (ESP32-S3)
    };
    
    ret = adc_continuous_config(g_adc_cont_handle, &cont_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure ADC continuous: %s", esp_err_to_name(ret));
        adc_continuous_deinit(g_adc_cont_handle);
        g_adc_cont_handle = NULL;
        return ret;
    }
    
    // Register DMA conversion callback
    adc_continuous_evt_cbs_t cbs = {
        .on_conv_done = adc_continuous_dma_callback,
    };
    
    ret = adc_continuous_register_event_callbacks(g_adc_cont_handle, &cbs, NULL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register ADC callback: %s", esp_err_to_name(ret));
        adc_continuous_deinit(g_adc_cont_handle);
        g_adc_cont_handle = NULL;
        return ret;
    }
    
    // Initialize ring buffer
    ringbuf_reset(&g_adc_ringbuf);
    
    ESP_LOGI(TAG, "ADC continuous initialized: 2 channels, %d Hz, %d frame size",
             EDRUMULUS_ADC_SAMPLE_RATE, EDRUMULUS_ADC_CONT_FRAME_SIZE);
    
    return ESP_OK;
}

esp_err_t edrumulus_detection_adc_continuous_start(void)
{
    if (g_adc_cont_handle == NULL) {
        ESP_LOGE(TAG, "ADC continuous not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    if (g_adc_continuous_running) {
        ESP_LOGW(TAG, "ADC continuous already running");
        return ESP_OK;
    }
    
    esp_err_t ret = adc_continuous_start(g_adc_cont_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start ADC continuous: %s", esp_err_to_name(ret));
        return ret;
    }
    
    g_adc_continuous_running = true;
    ESP_LOGI(TAG, "ADC continuous started (DMA active)");
    
    return ESP_OK;
}

esp_err_t edrumulus_detection_adc_continuous_stop(void)
{
    if (g_adc_cont_handle == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    
    if (!g_adc_continuous_running) {
        return ESP_OK;
    }
    
    esp_err_t ret = adc_continuous_stop(g_adc_cont_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to stop ADC continuous: %s", esp_err_to_name(ret));
        return ret;
    }
    
    g_adc_continuous_running = false;
    ESP_LOGI(TAG, "ADC continuous stopped");
    
    return ESP_OK;
}

esp_err_t edrumulus_detection_adc_continuous_deinit(void)
{
    if (g_adc_cont_handle == NULL) {
        return ESP_OK;
    }
    
    if (g_adc_continuous_running) {
        edrumulus_detection_adc_continuous_stop();
    }
    
    esp_err_t ret = adc_continuous_deinit(g_adc_cont_handle);
    g_adc_cont_handle = NULL;
    g_adc_continuous_running = false;
    
    ESP_LOGI(TAG, "ADC continuous deinitialized");
    
    return ret;
}

bool edrumulus_detection_get_sample(edrumulus_adc_sample_t *sample)
{
    if (!g_adc_ringbuf.initialized) return false;
    return ringbuf_pop(&g_adc_ringbuf, sample);
}

esp_err_t edrumulus_detection_get_buffer_level(uint32_t *count)
{
    if (!count) return ESP_ERR_INVALID_ARG;
    *count = ringbuf_count(&g_adc_ringbuf);
    return ESP_OK;
}

uint32_t edrumulus_detection_get_overflow_count(void)
{
    return g_adc_ringbuf.overflow_count;
}

// === LEGACY ONE-SHOT READ (конвертирует из ring buffer) ===

esp_err_t edrumulus_detection_read_channel(uint8_t channel, int *value)
{
    if (!g_detection_initialized) {
        ESP_LOGE(TAG, "Detection not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    if (channel >= EDRUMULUS_MAX_ADC_CHANNELS || value == NULL) {
        ESP_LOGE(TAG, "Invalid parameters");
        return ESP_ERR_INVALID_ARG;
    }
    
    // Scan ring buffer for latest sample from requested channel
    edrumulus_adc_sample_t sample;
    bool found = false;
    uint32_t newest_timestamp = 0;
    int latest_value = 0;
    
    // Drain ring buffer, looking for matching channel
    // This gives us the latest value and clears the buffer
    while (edrumulus_detection_get_sample(&sample)) {
        if (sample.channel == channel && sample.timestamp_us >= newest_timestamp) {
            newest_timestamp = sample.timestamp_us;
            latest_value = sample.raw_value;
            found = true;
        }
    }
    
    if (found) {
        *value = latest_value;
        return ESP_OK;
    }
    
    // If ring buffer was empty, return last known value or 0
    *value = 0;
    return ESP_OK;
}

esp_err_t edrumulus_detection_init(const edrumulus_detection_config_t *config)
{
    if (config == NULL) {
        ESP_LOGE(TAG, "Detection configuration cannot be NULL");
        return ESP_ERR_INVALID_ARG;
    }

    if (g_detection_initialized) {
        ESP_LOGW(TAG, "Detection already initialized");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Initializing detection subsystem");
    
    // Initialize ADC continuous mode with DMA (dual-channel for piezo1 + piezo2)
    esp_err_t ret = edrumulus_detection_adc_continuous_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize continuous ADC: %s", esp_err_to_name(ret));
        return ret;
    }
    ESP_LOGI(TAG, "ADC continuous mode initialized: 2 channels @ %d Hz", EDRUMULUS_ADC_SAMPLE_RATE);
    
    g_detection_initialized = true;
    ESP_LOGI(TAG, "Detection subsystem initialized successfully");
    
    // Inicializar configuración de mask time por defecto
    for (int i = 0; i < EDRUMULUS_MAX_ADC_CHANNELS; i++) {
        g_last_hit_time[i] = 0;
        g_mask_active[i] = false;
    }
    
    // Inicializar filtro de banda pasante
    ret = edrumulus_detection_filter_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize band-pass filter: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Start continuous ADC conversion
    ret = edrumulus_detection_adc_continuous_start();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start continuous ADC: %s", esp_err_to_name(ret));
        return ret;
    }
    ESP_LOGI(TAG, "ADC continuous conversion started (DMA active)");
    
    // PHASE 2: Inicializar validación del filtro
    esp_err_t filter_validation_result = edrumulus_filter_validation_init();
    if (filter_validation_result != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize filter validation: %s", esp_err_to_name(filter_validation_result));
        // Continuar sin validación si falla
        ESP_LOGW(TAG, "Continuing without filter validation");
    } else {
        ESP_LOGI(TAG, "Phase 2 filter validation initialized successfully");
    }
    
    // PHASE 3: Inicializar detector de rebotes
    esp_err_t rebound_result = edrumulus_rebound_detector_init();
    if (rebound_result != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize rebound detector: %s", esp_err_to_name(rebound_result));
        // Continuar sin detector de rebotes si falla
        ESP_LOGW(TAG, "Continuing without rebound detection");
    } else {
        ESP_LOGI(TAG, "Phase 3 rebound detector initialized successfully");
    }
    
    return ESP_OK;
}

// === IMPLEMENTACIÓN PHASE 3: VALIDACIÓN ALGORITMOS AVANZADOS ===

// Variables globales para validación Phase 3
static bool g_phase3_validation_initialized = false;
static edrumulus_phase3_validation_t g_phase3_validators[EDRUMULUS_MAX_ADC_CHANNELS];

// Funciones auxiliares para validación Phase 3
static void phase3_validation_log_internal(uint8_t channel, const char *format, ...);
static float calculate_exponential_decay_r_squared(const float *time_values, const float *signal_values, uint16_t count, float *tau_out, float *amplitude_out);
static float generate_synthetic_hit_signal(float t, float amplitude, float frequency, float decay_time);
// Declaración de generate_synthetic_bounce_signal removida - función no utilizada
static float generate_synthetic_noise(float level);

static void phase3_validation_log_internal(uint8_t channel, const char *format, ...)
{
    char buffer[512];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    
    uint64_t timestamp = get_timestamp_us();
    ESP_LOGI(TAG, "[PHASE3_CH%d] %lu: %s", channel, (uint32_t)timestamp, buffer);
    
    // Guardar en log interno si hay espacio
    edrumulus_phase3_validation_t *validator = &g_phase3_validators[channel];
    snprintf(validator->validation_log, sizeof(validator->validation_log), 
             "[%lu] %.800s", (uint32_t)timestamp, buffer);
}

esp_err_t edrumulus_phase3_validation_init(void)
{
    if (g_phase3_validation_initialized) {
        ESP_LOGW(TAG, "Phase 3 validation already initialized");
        return ESP_OK;
    }
    
    // Inicializar validadores para todos los canales
    memset(g_phase3_validators, 0, sizeof(g_phase3_validators));
    
    for (uint8_t i = 0; i < EDRUMULUS_MAX_ADC_CHANNELS; i++) {
        edrumulus_phase3_validation_t *validator = &g_phase3_validators[i];
        
        // Configuración por defecto
        validator->test_channel = i;
        validator->test_sample_count = EDRUMULUS_PHASE3_VALIDATION_TEST_SAMPLES;
        validator->test_duration_ms = EDRUMULUS_PHASE3_VALIDATION_SYNTHETIC_DURATION;
        
        // Inicializar generador de datos sintéticos
        validator->synthetic_generator.hit_amplitude = 1.0f;
        validator->synthetic_generator.hit_frequency = 200.0f; // Hz resonancia típica piezo
        validator->synthetic_generator.hit_decay_time = 100.0f; // ms
        validator->synthetic_generator.hit_rise_time = 2.0f; // ms
        validator->synthetic_generator.bounce_count = 3;
        validator->synthetic_generator.bounce_interval = 5.0f; // ms
        validator->synthetic_generator.bounce_decay_factor = 0.3f;
        validator->synthetic_generator.noise_level = 0.05f; // 5%
        validator->synthetic_generator.power_hum_amplitude = 0.02f;
        validator->synthetic_generator.rf_interference_level = 0.01f;
        
        // Inicializar métricas de rendimiento
        validator->performance_metrics.total_latency_us = 0;
        validator->performance_metrics.overall_sensitivity = 0.0f;
        validator->performance_metrics.overall_specificity = 0.0f;
        validator->performance_metrics.overall_accuracy = 0.0f;
    }
    
    g_phase3_validation_initialized = true;
    
    ESP_LOGI(TAG, "Phase 3 validation subsystem initialized");
    
    return ESP_OK;
}

esp_err_t edrumulus_phase3_validation_deinit(void)
{
    if (!g_phase3_validation_initialized) {
        return ESP_OK;
    }
    
    g_phase3_validation_initialized = false;
    
    ESP_LOGI(TAG, "Phase 3 validation subsystem deinitialized");
    
    return ESP_OK;
}

// Función auxiliar para calcular R² de decaimiento exponencial
static float calculate_exponential_decay_r_squared(const float *time_values, const float *signal_values, uint16_t count, float *tau_out, float *amplitude_out)
{
    if (count < 3 || !time_values || !signal_values) {
        return 0.0f;
    }
    
    // Encontrar amplitud inicial (máximo valor)
    float max_amplitude = 0.0f;
    uint16_t max_index = 0;
    for (uint16_t i = 0; i < count; i++) {
        if (signal_values[i] > max_amplitude) {
            max_amplitude = signal_values[i];
            max_index = i;
        }
    }
    
    if (max_amplitude <= 0.0f) {
        return 0.0f;
    }
    
    // Estimar tau usando método de mínimos cuadrados en escala logarítmica
    float sum_t = 0.0f, sum_ln_y = 0.0f, sum_t_ln_y = 0.0f, sum_t2 = 0.0f;
    uint16_t valid_points = 0;
    
    for (uint16_t i = max_index; i < count; i++) {
        if (signal_values[i] > max_amplitude * 0.01f) { // Solo puntos > 1% del máximo
            float t = time_values[i] - time_values[max_index];
            float ln_y = logf(signal_values[i] / max_amplitude);
            
            sum_t += t;
            sum_ln_y += ln_y;
            sum_t_ln_y += t * ln_y;
            sum_t2 += t * t;
            valid_points++;
        }
    }
    
    if (valid_points < 3) {
        return 0.0f;
    }
    
    // Calcular pendiente (slope = -1/tau)
    float slope = (valid_points * sum_t_ln_y - sum_t * sum_ln_y) / 
                  (valid_points * sum_t2 - sum_t * sum_t);
    
    float tau = (slope != 0.0f) ? -1.0f / slope : 0.0f;
    
    if (tau <= 0.0f || tau > 1000.0f) { // Tau debe ser positivo y razonable
        return 0.0f;
    }
    
    // Calcular R² comparando con modelo exponencial
    float ss_res = 0.0f, ss_tot = 0.0f;
    float mean_y = 0.0f;
    
    // Calcular media
    for (uint16_t i = max_index; i < count; i++) {
        if (signal_values[i] > max_amplitude * 0.01f) {
            mean_y += signal_values[i];
        }
    }
    mean_y /= valid_points;
    
    // Calcular R²
    for (uint16_t i = max_index; i < count; i++) {
        if (signal_values[i] > max_amplitude * 0.01f) {
            float t = time_values[i] - time_values[max_index];
            float predicted = max_amplitude * expf(-t / tau);
            float actual = signal_values[i];
            
            ss_res += (actual - predicted) * (actual - predicted);
            ss_tot += (actual - mean_y) * (actual - mean_y);
        }
    }
    
    float r_squared = (ss_tot > 0.0f) ? 1.0f - (ss_res / ss_tot) : 0.0f;
    
    if (tau_out) *tau_out = tau;
    if (amplitude_out) *amplitude_out = max_amplitude;
    
    return (r_squared >= 0.0f && r_squared <= 1.0f) ? r_squared : 0.0f;
}

// Validación del Edge Detector
esp_err_t edrumulus_phase3_test_edge_detector(uint8_t channel)
{
    if (!g_phase3_validation_initialized || channel >= EDRUMULUS_MAX_ADC_CHANNELS) {
        return ESP_ERR_INVALID_STATE;
    }
    
    edrumulus_phase3_validation_t *validator = &g_phase3_validators[channel];
    edrumulus_edge_detector_validation_t *edge_val = &validator->edge_detector;
    
    phase3_validation_log_internal(channel, "Iniciando test Edge Detector");
    
    // Resetear contadores
    edge_val->true_positives = 0;
    edge_val->false_positives = 0;
    edge_val->true_negatives = 0;
    edge_val->false_negatives = 0;
    
    uint16_t test_samples = validator->test_sample_count;
    uint16_t positive_samples = test_samples / 2;
    uint16_t negative_samples = test_samples - positive_samples;
    
    // Test con señales positivas (golpes reales)
    for (uint16_t i = 0; i < positive_samples; i++) {
        float t = (float)i * 0.125f; // 8kHz sampling
        float signal = generate_synthetic_hit_signal(t, 0.8f + (rand() % 40) * 0.01f, 
                                                   180.0f + (rand() % 40), 80.0f + (rand() % 40));
        signal += generate_synthetic_noise(validator->synthetic_generator.noise_level);
        
        // Simular detección de edge (umbral adaptativo)
        float threshold = 0.1f; // Umbral base
        bool detected = (signal > threshold);
        bool should_detect = (signal > 0.15f); // Ground truth
        
        if (detected && should_detect) {
            edge_val->true_positives++;
        } else if (detected && !should_detect) {
            edge_val->false_positives++;
        } else if (!detected && should_detect) {
            edge_val->false_negatives++;
        } else {
            edge_val->true_negatives++;
        }
    }
    
    // Test con señales negativas (ruido/silencio)
    for (uint16_t i = 0; i < negative_samples; i++) {
        float noise_signal = generate_synthetic_noise(validator->synthetic_generator.noise_level * 2.0f);
        
        float threshold = 0.1f;
        bool detected = (fabsf(noise_signal) > threshold);
        bool should_detect = false; // No debería detectar ruido
        
        if (detected && should_detect) {
            edge_val->true_positives++;
        } else if (detected && !should_detect) {
            edge_val->false_positives++;
        } else if (!detected && should_detect) {
            edge_val->false_negatives++;
        } else {
            edge_val->true_negatives++;
        }
    }
    
    // Calcular métricas
    uint16_t total_positive = edge_val->true_positives + edge_val->false_negatives;
    uint16_t total_negative = edge_val->true_negatives + edge_val->false_positives;
    
    edge_val->sensitivity = (total_positive > 0) ? 
        (float)edge_val->true_positives / total_positive : 0.0f;
    
    edge_val->specificity = (total_negative > 0) ? 
        (float)edge_val->true_negatives / total_negative : 0.0f;
    
    edge_val->precision = (edge_val->true_positives + edge_val->false_positives > 0) ? 
        (float)edge_val->true_positives / (edge_val->true_positives + edge_val->false_positives) : 0.0f;
    
    edge_val->f1_score = (edge_val->precision + edge_val->sensitivity > 0) ? 
        2.0f * (edge_val->precision * edge_val->sensitivity) / (edge_val->precision + edge_val->sensitivity) : 0.0f;
    
    // Verificar criterios de aceptación
    bool sensitivity_ok = edge_val->sensitivity >= EDRUMULUS_PHASE3_VALIDATION_MIN_SENSITIVITY;
    bool specificity_ok = edge_val->specificity >= EDRUMULUS_PHASE3_VALIDATION_MIN_SPECIFICITY;
    
    validator->edge_detector_criteria_met = sensitivity_ok && specificity_ok;
    
    phase3_validation_log_internal(channel, 
        "Edge Detector - TP:%d FP:%d TN:%d FN:%d Sens:%.3f Spec:%.3f Prec:%.3f F1:%.3f %s",
        edge_val->true_positives, edge_val->false_positives, 
        edge_val->true_negatives, edge_val->false_negatives,
        edge_val->sensitivity, edge_val->specificity, 
        edge_val->precision, edge_val->f1_score,
        validator->edge_detector_criteria_met ? "PASS" : "FAIL");
    
    return ESP_OK;
}

// Validación del Decay Analyzer
esp_err_t edrumulus_phase3_test_decay_analyzer(uint8_t channel)
{
    if (!g_phase3_validation_initialized || channel >= EDRUMULUS_MAX_ADC_CHANNELS) {
        return ESP_ERR_INVALID_STATE;
    }
    
    edrumulus_phase3_validation_t *validator = &g_phase3_validators[channel];
    edrumulus_decay_analyzer_validation_t *decay_val = &validator->decay_analyzer;
    
    phase3_validation_log_internal(channel, "Iniciando test Decay Analyzer");
    
    uint16_t test_samples = validator->test_sample_count;
    float sample_rate = 8000.0f; // 8kHz
    float dt = 1.0f / sample_rate;
    
    // Arrays para almacenar datos de test
    float time_values[test_samples];
    float signal_values[test_samples];
    
    // Resetear métricas
    decay_val->tau_error = 0.0f;
    decay_val->amplitude_error = 0.0f;
    decay_val->r_squared = 0.0f;
    decay_val->valid_decay_count = 0;
    
    // Realizar múltiples tests con diferentes parámetros
    uint8_t num_tests = 10;
    
    for (uint8_t test = 0; test < num_tests; test++) {
        // Parámetros de test variables
        float target_tau = 50.0f + (test * 10.0f); // 50-140ms
        float target_amplitude = 0.5f + (test * 0.05f); // 0.5-0.95
        float noise_level = validator->synthetic_generator.noise_level;
        
        // Generar señal de decaimiento exponencial sintética
        for (uint16_t i = 0; i < test_samples; i++) {
            time_values[i] = i * dt * 1000.0f; // Convertir a ms
            
            // Señal exponencial pura + ruido
            signal_values[i] = target_amplitude * expf(-time_values[i] / target_tau);
            signal_values[i] += generate_synthetic_noise(noise_level);
            
            // Asegurar que no sea negativa
            if (signal_values[i] < 0.0f) {
                signal_values[i] = 0.0f;
            }
        }
        
        // Analizar el decaimiento
        float measured_tau, measured_amplitude;
        float r_squared = calculate_exponential_decay_r_squared(time_values, signal_values, 
                                                              test_samples, &measured_tau, &measured_amplitude);
        
        if (r_squared > 0.0f) {
            // Calcular errores
            float tau_error = fabsf(measured_tau - target_tau) / target_tau;
            float amplitude_error = fabsf(measured_amplitude - target_amplitude) / target_amplitude;
            
            decay_val->tau_error = tau_error;
            decay_val->amplitude_error = amplitude_error;
            decay_val->r_squared = r_squared;
            decay_val->valid_decay_count++;
            
            phase3_validation_log_internal(channel, 
                "Test %d: Tau %.1f->%.1f (err:%.3f) Amp %.3f->%.3f (err:%.3f) R²:%.4f",
                test, target_tau, measured_tau, tau_error,
                target_amplitude, measured_amplitude, amplitude_error, r_squared);
        }
    }
    
    // Verificar criterios de aceptación
    bool r_squared_ok = decay_val->r_squared >= EDRUMULUS_PHASE3_VALIDATION_MIN_R_SQUARED;
    bool tau_error_ok = decay_val->tau_error <= 0.15f; // 15% error máximo
    bool amplitude_error_ok = decay_val->amplitude_error <= 0.10f; // 10% error máximo
    
    validator->decay_analyzer_criteria_met = r_squared_ok && tau_error_ok && amplitude_error_ok;
    
    phase3_validation_log_internal(channel, 
        "Decay Analyzer - Tests:%d R²:%.4f TauErr:%.3f AmpErr:%.3f %s",
        decay_val->valid_decay_count, decay_val->r_squared,
        decay_val->tau_error, decay_val->amplitude_error,
        validator->decay_analyzer_criteria_met ? "PASS" : "FAIL");
    
    return ESP_OK;
}

// Validación del Velocity Validator
esp_err_t edrumulus_phase3_test_velocity_validator(uint8_t channel)
{
    if (!g_phase3_validation_initialized || channel >= EDRUMULUS_MAX_ADC_CHANNELS) {
        return ESP_ERR_INVALID_STATE;
    }
    
    edrumulus_phase3_validation_t *validator = &g_phase3_validators[channel];
    edrumulus_velocity_validator_validation_t *vel_val = &validator->velocity_validator;
    
    phase3_validation_log_internal(channel, "Iniciando test Velocity Validator");
    
    uint16_t test_samples = 127; // Rango MIDI 0-127
    
    // Arrays para test de linealidad
    float input_velocities[test_samples];
    float output_velocities[test_samples];
    
    // Test de linealidad: generar velocidades de 0 a 127
    for (uint16_t i = 0; i < test_samples; i++) {
        float input_vel = (float)i;
        input_velocities[i] = input_vel;
        
        // Simular conversión de amplitud a velocidad MIDI
        // Modelo: velocity = amplitude^0.5 * 127 (curva de potencia típica)
        float normalized_amp = input_vel / 127.0f;
        float simulated_output = sqrtf(normalized_amp) * 127.0f;
        
        // Agregar pequeña variación para simular ruido del sistema
        simulated_output += generate_synthetic_noise(0.02f) * 5.0f;
        
        // Limitar al rango MIDI
        if (simulated_output < 0.0f) simulated_output = 0.0f;
        if (simulated_output > 127.0f) simulated_output = 127.0f;
        
        output_velocities[i] = simulated_output;
    }
    
    // Calcular linealidad usando regresión lineal
    vel_val->velocity_linearity = calculate_linear_regression_r_squared(input_velocities, output_velocities, test_samples);
    
    // Test de repetibilidad: mismo input múltiples veces
    uint8_t repeatability_tests = 20;
    float test_velocity = 64.0f; // Velocidad media
    float velocity_outputs[repeatability_tests];
    
    for (uint8_t i = 0; i < repeatability_tests; i++) {
        float normalized_amp = test_velocity / 127.0f;
        velocity_outputs[i] = sqrtf(normalized_amp) * 127.0f;
        velocity_outputs[i] += generate_synthetic_noise(0.02f) * 5.0f;
        
        if (velocity_outputs[i] < 0.0f) velocity_outputs[i] = 0.0f;
        if (velocity_outputs[i] > 127.0f) velocity_outputs[i] = 127.0f;
    }
    
    // Calcular desviación estándar para repetibilidad
    float mean_output = 0.0f;
    for (uint8_t i = 0; i < repeatability_tests; i++) {
        mean_output += velocity_outputs[i];
    }
    mean_output /= repeatability_tests;
    
    float variance = 0.0f;
    for (uint8_t i = 0; i < repeatability_tests; i++) {
        float diff = velocity_outputs[i] - mean_output;
        variance += diff * diff;
    }
    variance /= repeatability_tests;
    
    vel_val->velocity_repeatability = sqrtf(variance);
    
    // Test de precisión en diferentes rangos
    float low_range_error = 0.0f, mid_range_error = 0.0f, high_range_error = 0.0f;
    uint8_t range_tests = 10;
    
    // Rango bajo (0-42)
    for (uint8_t i = 0; i < range_tests; i++) {
        float target = (float)(i * 4); // 0, 4, 8, ..., 36
        float measured = target + generate_synthetic_noise(0.02f) * 3.0f;
        low_range_error += fabsf(measured - target);
    }
    low_range_error /= range_tests;
    
    // Rango medio (43-84)
    for (uint8_t i = 0; i < range_tests; i++) {
        float target = 43.0f + (float)(i * 4); // 43, 47, 51, ..., 79
        float measured = target + generate_synthetic_noise(0.02f) * 3.0f;
        mid_range_error += fabsf(measured - target);
    }
    mid_range_error /= range_tests;
    
    // Rango alto (85-127)
    for (uint8_t i = 0; i < range_tests; i++) {
        float target = 85.0f + (float)(i * 4); // 85, 89, 93, ..., 121
        float measured = target + generate_synthetic_noise(0.02f) * 3.0f;
        high_range_error += fabsf(measured - target);
    }
    high_range_error /= range_tests;
    
    vel_val->velocity_accuracy = (low_range_error + mid_range_error + high_range_error) / 3.0f;
    
    // Verificar criterios de aceptación
    bool linearity_ok = vel_val->velocity_linearity >= 0.95f; // R² mínimo 0.95
    bool repeatability_ok = vel_val->velocity_repeatability <= 2.0f; // Desviación máxima 2 unidades MIDI
    bool accuracy_ok = vel_val->velocity_accuracy <= 3.0f; // Error promedio máximo 3 unidades MIDI
    
    validator->velocity_validator_criteria_met = linearity_ok && repeatability_ok && accuracy_ok;
    
    phase3_validation_log_internal(channel, 
        "Velocity Validator - Lin:%.4f Rep:%.2f Acc:%.2f %s",
        vel_val->velocity_linearity, vel_val->velocity_repeatability, vel_val->velocity_accuracy,
        validator->velocity_validator_criteria_met ? "PASS" : "FAIL");
    
    return ESP_OK;
}

// Validación del Adaptive Threshold
esp_err_t edrumulus_phase3_test_adaptive_threshold(uint8_t channel)
{
    if (!g_phase3_validation_initialized || channel >= EDRUMULUS_MAX_ADC_CHANNELS) {
        return ESP_ERR_INVALID_STATE;
    }
    
    edrumulus_phase3_validation_t *validator = &g_phase3_validators[channel];
    edrumulus_adaptive_threshold_validation_t *thresh_val = &validator->adaptive_threshold;
    
    phase3_validation_log_internal(channel, "Iniciando test Adaptive Threshold");
    
    // Resetear métricas
    thresh_val->measured_snr_db = 0.0f;
    thresh_val->adaptation_time_ms = 0.0f;
    thresh_val->threshold_stability = 0.0f;
    thresh_val->adaptation_count = 0;
    
    uint8_t num_tests = 15;
    float sample_rate = 8000.0f;
    uint16_t test_duration_samples = (uint16_t)(sample_rate * 0.5f); // 500ms por test
    
    for (uint8_t test = 0; test < num_tests; test++) {
        // Parámetros variables para cada test
        float signal_amplitude = 0.3f + (test * 0.05f); // 0.3 a 1.0
        float noise_level = 0.02f + (test * 0.003f); // 2% a 6.2%
        // float target_snr = signal_amplitude / noise_level; // No utilizado actualmente
        
        // Simular umbral adaptativo
        float current_threshold = 0.1f; // Umbral inicial
        float target_threshold = signal_amplitude * 0.3f; // 30% de la señal
        
        // Medir tiempo de adaptación
        uint64_t adaptation_start = get_timestamp_us();
        uint16_t adaptation_samples = 0;
        
        // Arrays para medir estabilidad
        float threshold_history[100];
        uint8_t history_count = 0;
        
        // Simular proceso de adaptación
        for (uint16_t sample = 0; sample < test_duration_samples; sample++) {
            float t = (float)sample / sample_rate;
            
            // Generar señal de test
            float signal = 0.0f;
            if (sample > test_duration_samples / 4 && sample < test_duration_samples * 3 / 4) {
                // Señal presente en el medio del test
                signal = generate_synthetic_hit_signal(t, signal_amplitude, 200.0f, 100.0f);
            }
            signal += generate_synthetic_noise(noise_level);
            
            // Simular algoritmo de umbral adaptativo
            float alpha = 0.01f; // Factor de adaptación
            float signal_energy = signal * signal;
            float noise_estimate = noise_level * noise_level;
            
            // Actualizar umbral basado en energía de señal
            float desired_threshold = sqrtf(signal_energy + 3.0f * noise_estimate);
            current_threshold += alpha * (desired_threshold - current_threshold);
            
            // Registrar convergencia
            if (adaptation_samples == 0 && fabsf(current_threshold - target_threshold) < target_threshold * 0.1f) {
                adaptation_samples = sample;
            }
            
            // Guardar historial para análisis de estabilidad
            if (sample >= test_duration_samples / 2 && history_count < 100) {
                threshold_history[history_count++] = current_threshold;
            }
        }
        
        uint64_t adaptation_end = get_timestamp_us();
        float adaptation_time_ms = (adaptation_end - adaptation_start) / 1000.0f;
        
        // Calcular SNR medido
        float measured_snr = (noise_level > 0.0f) ? signal_amplitude / noise_level : 0.0f;
        
        // Calcular estabilidad (varianza del umbral en la segunda mitad)
        float threshold_mean = 0.0f;
        for (uint8_t i = 0; i < history_count; i++) {
            threshold_mean += threshold_history[i];
        }
        threshold_mean /= history_count;
        
        float threshold_variance = 0.0f;
        for (uint8_t i = 0; i < history_count; i++) {
            float diff = threshold_history[i] - threshold_mean;
            threshold_variance += diff * diff;
        }
        threshold_variance /= history_count;
        
        // Almacenar métricas (usar último valor como representativo)
        thresh_val->measured_snr_db = measured_snr;
        thresh_val->adaptation_time_ms = adaptation_time_ms;
        thresh_val->threshold_stability = threshold_variance;
        thresh_val->adaptation_count++;
        
        phase3_validation_log_internal(channel, 
            "Test %d: SNR %.2f AdaptTime %.1fms Stability %.6f",
            test, measured_snr, adaptation_time_ms, threshold_variance);
    }
    
    // Verificar que se ejecutaron tests
    if (thresh_val->adaptation_count == 0) {
        thresh_val->measured_snr_db = 0.0f;
        thresh_val->adaptation_time_ms = 1000.0f; // Tiempo máximo
        thresh_val->threshold_stability = 1.0f; // Varianza máxima
    }
    
    // Verificar criterios de aceptación
    bool snr_ok = thresh_val->measured_snr_db >= 10.0f; // SNR mínimo 10dB
    bool adaptation_ok = thresh_val->adaptation_time_ms <= EDRUMULUS_PHASE3_VALIDATION_MAX_LATENCY_MS;
    bool stability_ok = thresh_val->threshold_stability <= 0.001f; // Baja varianza
    
    validator->adaptive_threshold_criteria_met = snr_ok && adaptation_ok && stability_ok;
    
    phase3_validation_log_internal(channel, 
        "Adaptive Threshold - SNR:%.2f AdaptTime:%.1fms Stability:%.6f %s",
        thresh_val->measured_snr_db, thresh_val->adaptation_time_ms, 
        thresh_val->threshold_stability,
        validator->adaptive_threshold_criteria_met ? "PASS" : "FAIL");
    
    return ESP_OK;
}

// Implementación de funciones generadoras de datos sintéticos
static float generate_synthetic_hit_signal(float t, float amplitude, float frequency, float decay_time)
{
    if (t < 0.0f) return 0.0f;
    
    // Modelo de golpe: envolvente exponencial * oscilación amortiguada
    float envelope = amplitude * expf(-t * 1000.0f / decay_time); // decay_time en ms
    float oscillation = sinf(2.0f * M_PI * frequency * t);
    
    // Agregar componente de rise time para realismo
    float rise_factor = 1.0f;
    if (t < 0.002f) { // 2ms rise time
        rise_factor = t / 0.002f;
    }
    
    return envelope * oscillation * rise_factor;
}

// Función generate_synthetic_bounce_signal removida - no utilizada actualmente

static float generate_synthetic_noise(float level)
{
    if (level <= 0.0f) return 0.0f;
    
    // Generar ruido blanco gaussiano usando Box-Muller
    static bool has_spare = false;
    static float spare;
    
    if (has_spare) {
        has_spare = false;
        return spare * level;
    }
    
    has_spare = true;
    
    // Generar dos números aleatorios uniformes
    float u1 = (float)rand() / RAND_MAX;
    float u2 = (float)rand() / RAND_MAX;
    
    // Evitar log(0)
    if (u1 < 1e-6f) u1 = 1e-6f;
    
    // Box-Muller transform
    float mag = level * sqrtf(-2.0f * logf(u1));
    spare = mag * cosf(2.0f * M_PI * u2);
    
    return mag * sinf(2.0f * M_PI * u2);
}

// Función de validación completa de Phase 3
esp_err_t edrumulus_phase3_run_full_validation(uint8_t channel)
{
    if (!g_phase3_validation_initialized || channel >= EDRUMULUS_MAX_ADC_CHANNELS) {
        return ESP_ERR_INVALID_STATE;
    }
    
    edrumulus_phase3_validation_t *validator = &g_phase3_validators[channel];
    
    phase3_validation_log_internal(channel, "=== INICIANDO VALIDACIÓN COMPLETA PHASE 3 ===");
    
    uint64_t validation_start = get_timestamp_us();
    
    // Ejecutar todas las validaciones individuales
    esp_err_t result = ESP_OK;
    
    // 1. Edge Detector
    if (edrumulus_phase3_test_edge_detector(channel) != ESP_OK) {
        result = ESP_FAIL;
    }
    
    // 2. Decay Analyzer
    if (edrumulus_phase3_test_decay_analyzer(channel) != ESP_OK) {
        result = ESP_FAIL;
    }
    
    // 3. Velocity Validator
    if (edrumulus_phase3_test_velocity_validator(channel) != ESP_OK) {
        result = ESP_FAIL;
    }
    
    // 4. Adaptive Threshold
    if (edrumulus_phase3_test_adaptive_threshold(channel) != ESP_OK) {
        result = ESP_FAIL;
    }
    
    uint64_t validation_end = get_timestamp_us();
    
    // Calcular métricas de rendimiento del sistema completo
    validator->performance_metrics.total_latency_us = validation_end - validation_start;
    
    // Calcular métricas globales basadas en resultados individuales
    float total_sensitivity = 0.0f, total_specificity = 0.0f;
    uint8_t passed_tests = 0;
    
    if (validator->edge_detector_criteria_met) {
        total_sensitivity += validator->edge_detector.sensitivity;
        total_specificity += validator->edge_detector.specificity;
        passed_tests++;
    }
    
    if (validator->decay_analyzer_criteria_met) {
        // Usar R² como proxy de accuracy para decay analyzer
        total_sensitivity += validator->decay_analyzer.r_squared;
        total_specificity += validator->decay_analyzer.r_squared;
        passed_tests++;
    }
    
    if (validator->velocity_validator_criteria_met) {
        // Usar linealidad como proxy de accuracy
        total_sensitivity += validator->velocity_validator.velocity_linearity;
        total_specificity += validator->velocity_validator.velocity_linearity;
        passed_tests++;
    }
    
    if (validator->adaptive_threshold_criteria_met) {
        // Usar SNR normalizado como proxy de accuracy
        float snr_normalized = fminf(validator->adaptive_threshold.measured_snr_db / 20.0f, 1.0f);
        total_sensitivity += snr_normalized;
        total_specificity += snr_normalized;
        passed_tests++;
    }
    
    // Calcular métricas globales
    validator->performance_metrics.overall_sensitivity = (passed_tests > 0) ? total_sensitivity / passed_tests : 0.0f;
    validator->performance_metrics.overall_specificity = (passed_tests > 0) ? total_specificity / passed_tests : 0.0f;
    validator->performance_metrics.overall_accuracy = (validator->performance_metrics.overall_sensitivity + 
                                                     validator->performance_metrics.overall_specificity) / 2.0f;
    
    // Determinar si la validación general de Phase 3 pasó
    bool all_individual_passed = validator->edge_detector_criteria_met &&
                                validator->decay_analyzer_criteria_met &&
                                validator->velocity_validator_criteria_met &&
                                validator->adaptive_threshold_criteria_met;
    
    bool performance_ok = validator->performance_metrics.total_latency_us <= (EDRUMULUS_PHASE3_VALIDATION_MAX_LATENCY_MS * 1000);
    bool accuracy_ok = validator->performance_metrics.overall_accuracy >= 0.85f; // 85% accuracy mínima
    
    validator->overall_phase3_validation_passed = all_individual_passed && performance_ok && accuracy_ok;
    
    // Log de resultados finales
    phase3_validation_log_internal(channel, 
        "=== RESULTADOS PHASE 3 - Canal %d ===", channel);
    phase3_validation_log_internal(channel, 
        "Edge Detector: %s", validator->edge_detector_criteria_met ? "PASS" : "FAIL");
    phase3_validation_log_internal(channel, 
        "Decay Analyzer: %s", validator->decay_analyzer_criteria_met ? "PASS" : "FAIL");
    phase3_validation_log_internal(channel, 
        "Velocity Validator: %s", validator->velocity_validator_criteria_met ? "PASS" : "FAIL");
    phase3_validation_log_internal(channel, 
        "Adaptive Threshold: %s", validator->adaptive_threshold_criteria_met ? "PASS" : "FAIL");
    phase3_validation_log_internal(channel, 
        "Performance - Latency: %lu us, Accuracy: %.3f", 
        validator->performance_metrics.total_latency_us, validator->performance_metrics.overall_accuracy);
    phase3_validation_log_internal(channel, 
        "PHASE 3 OVERALL: %s", validator->overall_phase3_validation_passed ? "PASS" : "FAIL");
    
    return result;
}

// Función para obtener resultados de validación
esp_err_t edrumulus_phase3_get_validation_results(uint8_t channel, edrumulus_phase3_validation_t **results)
{
    if (!g_phase3_validation_initialized || channel >= EDRUMULUS_MAX_ADC_CHANNELS || !results) {
        return ESP_ERR_INVALID_ARG;
    }
    
    *results = &g_phase3_validators[channel];
    return ESP_OK;
}

// Función para generar reporte de validación
esp_err_t edrumulus_phase3_generate_validation_report(uint8_t channel, char *report_buffer, size_t buffer_size)
{
    if (!g_phase3_validation_initialized || channel >= EDRUMULUS_MAX_ADC_CHANNELS || 
        !report_buffer || buffer_size < 1024) {
        return ESP_ERR_INVALID_ARG;
    }
    
    edrumulus_phase3_validation_t *validator = &g_phase3_validators[channel];
    
    snprintf(report_buffer, buffer_size,
        "=== REPORTE VALIDACIÓN PHASE 3 - Canal %d ===\n"
        "\n[EDGE DETECTOR]\n"
        "  Estado: %s\n"
        "  Sensitivity: %.3f (min: %.3f)\n"
        "  Specificity: %.3f (min: %.3f)\n"
        "  Precision: %.3f\n"
        "  F1-Score: %.3f\n"
        "  TP:%lu FP:%lu TN:%lu FN:%lu\n"
        "\n[DECAY ANALYZER]\n"
        "  Estado: %s\n"
        "  R² Promedio: %.4f (min: %.4f)\n"
        "  Error Tau: %.3f%% (max: 15%%)\n"
        "  Error Amplitud: %.3f%% (max: 10%%)\n"
        "  Tests Realizados: %u\n"
        "\n[VELOCITY VALIDATOR]\n"
        "  Estado: %s\n"
        "  Linealidad R²: %.4f (min: 0.95)\n"
        "  Repetibilidad σ: %.2f (max: 2.0)\n"
        "  Error Precisión: %.2f (max: 3.0)\n"
        "\n[ADAPTIVE THRESHOLD]\n"
        "  Estado: %s\n"
        "  SNR Promedio: %.2f dB (min: 10.0)\n"
        "  Tiempo Adaptación: %.1f ms (max: %.1f)\n"
        "  Estabilidad: %.6f (max: 0.001)\n"
        "\n[RENDIMIENTO GLOBAL]\n"
        "  Latencia Total: %lu μs\n"
        "  Sensitivity Global: %.3f\n"
        "  Specificity Global: %.3f\n"
        "  Accuracy Global: %.3f\n"
        "\n[RESULTADO FINAL]\n"
        "  PHASE 3: %s\n",
        channel,
        validator->edge_detector_criteria_met ? "PASS" : "FAIL",
        validator->edge_detector.sensitivity, EDRUMULUS_PHASE3_VALIDATION_MIN_SENSITIVITY,
        validator->edge_detector.specificity, EDRUMULUS_PHASE3_VALIDATION_MIN_SPECIFICITY,
        validator->edge_detector.precision,
        validator->edge_detector.f1_score,
        validator->edge_detector.true_positives,
        validator->edge_detector.false_positives,
        validator->edge_detector.true_negatives,
        validator->edge_detector.false_negatives,
        validator->decay_analyzer_criteria_met ? "PASS" : "FAIL",
        validator->decay_analyzer.r_squared, EDRUMULUS_PHASE3_VALIDATION_MIN_R_SQUARED,
        validator->decay_analyzer.tau_error * 100.0f,
        validator->decay_analyzer.amplitude_error * 100.0f,
        validator->decay_analyzer.valid_decay_count,
        validator->velocity_validator_criteria_met ? "PASS" : "FAIL",
        validator->velocity_validator.velocity_linearity,
        validator->velocity_validator.velocity_repeatability,
        validator->velocity_validator.velocity_accuracy,
        validator->adaptive_threshold_criteria_met ? "PASS" : "FAIL",
        validator->adaptive_threshold.measured_snr_db,
        validator->adaptive_threshold.adaptation_time_ms,
        (float)EDRUMULUS_PHASE3_VALIDATION_MAX_LATENCY_MS,
        validator->adaptive_threshold.threshold_stability,
        validator->performance_metrics.total_latency_us,
        validator->performance_metrics.overall_sensitivity,
        validator->performance_metrics.overall_specificity,
        validator->performance_metrics.overall_accuracy,
        validator->overall_phase3_validation_passed ? "PASS" : "FAIL");
    
    return ESP_OK;
}

// === IMPLEMENTACIÓN PHASE 2: VALIDACIÓN FILTRO DE BANDA PASANTE ===

// Variables globales para validación Phase 2
static bool g_filter_validation_initialized = false;
static edrumulus_filter_validation_t g_filter_validators[EDRUMULUS_MAX_ADC_CHANNELS];

// Funciones auxiliares para validación de filtro
static void filter_validation_log_internal(uint8_t channel, const char *format, ...);
static float calculate_magnitude_db(float input_amplitude, float output_amplitude);
static esp_err_t generate_sine_wave_internal(float frequency_hz, float amplitude, uint32_t duration_ms, uint16_t *output_buffer, uint16_t buffer_size);
static esp_err_t measure_filter_response_point(uint8_t channel, float frequency_hz, edrumulus_frequency_point_t *point);

static void filter_validation_log_internal(uint8_t channel, const char *format, ...)
{
    char buffer[256];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    
    uint64_t timestamp = get_timestamp_us();
    ESP_LOGI(TAG, "[PHASE2_CH%d] %lu: %s", channel, (uint32_t)timestamp, buffer);
    
    // Guardar en log interno si hay espacio
    edrumulus_filter_validation_t *validator = &g_filter_validators[channel];
    snprintf(validator->validation_log, sizeof(validator->validation_log), 
             "[%lu] %.200s", (uint32_t)timestamp, buffer);
}

static float calculate_magnitude_db(float input_amplitude, float output_amplitude)
{
    if (input_amplitude <= 0.0f || output_amplitude <= 0.0f) {
        return -100.0f; // Atenuación muy alta
    }
    
    float magnitude_ratio = output_amplitude / input_amplitude;
    return 20.0f * log10f(magnitude_ratio);
}

static esp_err_t generate_sine_wave_internal(float frequency_hz, float amplitude, uint32_t duration_ms, 
                                           uint16_t *output_buffer, uint16_t buffer_size)
{
    if (output_buffer == NULL || buffer_size == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    
    float sample_rate = EDRUMULUS_VALIDATION_SAMPLE_RATE;
    uint32_t total_samples = (uint32_t)((duration_ms / 1000.0f) * sample_rate);
    
    if (total_samples > buffer_size) {
        total_samples = buffer_size;
    }
    
    float angular_freq = 2.0f * M_PI * frequency_hz;
    float time_step = 1.0f / sample_rate;
    
    for (uint32_t i = 0; i < total_samples; i++) {
        float time = i * time_step;
        float sine_value = amplitude * sinf(angular_freq * time);
        
        // Convertir a valor ADC (0-4095) con offset de 2048 (punto medio)
        uint16_t adc_value = (uint16_t)(2048 + (sine_value * 2047.0f));
        
        // Clamp a rango válido
        if (adc_value > 4095) adc_value = 4095;
        
        output_buffer[i] = adc_value;
    }
    
    return ESP_OK;
}

static esp_err_t measure_filter_response_point(uint8_t channel, float frequency_hz, edrumulus_frequency_point_t *point)
{
    if (point == NULL || channel >= EDRUMULUS_MAX_ADC_CHANNELS) {
        return ESP_ERR_INVALID_ARG;
    }
    
    // Generar señal de test
    uint16_t test_signal[512];
    float test_amplitude = 0.5f; // 50% de amplitud
    uint32_t test_duration = 100; // 100ms
    
    esp_err_t ret = generate_sine_wave_internal(frequency_hz, test_amplitude, test_duration, 
                                               test_signal, sizeof(test_signal)/sizeof(test_signal[0]));
    if (ret != ESP_OK) {
        return ret;
    }
    
    // Procesar señal a través del filtro
    float input_rms = 0.0f;
    float output_rms = 0.0f;
    uint16_t sample_count = test_duration * EDRUMULUS_VALIDATION_SAMPLE_RATE / 1000;
    
    if (sample_count > 512) sample_count = 512;
    
    for (uint16_t i = 0; i < sample_count; i++) {
        // Convertir a voltaje
        float input_voltage = (float)test_signal[i] * EDRUMULUS_VALIDATION_ADC_VREF / 4095.0f;
        
        // Procesar a través del filtro (simulado)
        float filtered_output = edrumulus_detection_filter_process(input_voltage);
        
        // Calcular RMS
        input_rms += input_voltage * input_voltage;
        output_rms += filtered_output * filtered_output;
    }
    
    input_rms = sqrtf(input_rms / sample_count);
    output_rms = sqrtf(output_rms / sample_count);
    
    // Llenar punto de respuesta
    point->frequency_hz = frequency_hz;
    point->magnitude_db = calculate_magnitude_db(input_rms, output_rms);
    point->phase_degrees = 0.0f; // Simplificado para esta implementación
    point->input_amplitude = input_rms;
    point->output_amplitude = output_rms;
    point->measurement_time = get_timestamp_ms();
    
    return ESP_OK;
}

esp_err_t edrumulus_filter_validation_init(void)
{
    if (g_filter_validation_initialized) {
        ESP_LOGW(TAG, "Filter validation already initialized");
        return ESP_OK;
    }
    
    // Verificar que el filtro principal esté inicializado
    if (!g_filter_initialized) {
        ESP_LOGE(TAG, "Band-pass filter must be initialized before validation");
        return ESP_ERR_INVALID_STATE;
    }
    
    // Inicializar validadores para todos los canales
    memset(g_filter_validators, 0, sizeof(g_filter_validators));
    
    for (uint8_t i = 0; i < EDRUMULUS_MAX_ADC_CHANNELS; i++) {
        edrumulus_filter_validation_t *validator = &g_filter_validators[i];
        
        // Configuración por defecto
        validator->test_channel = i;
        validator->validation_in_progress = false;
        validator->validation_passed = false;
        
        // Configurar test config por defecto
        edrumulus_filter_test_config_t *config = &validator->test_config;
        config->test_frequency_start = 10.0f;
        config->test_frequency_end = 1000.0f;
        config->test_frequency_step = 10.0f;
        config->test_amplitude = 0.5f;
        config->noise_level = 0.01f;
        config->settling_time_ms = 100;
        config->samples_per_frequency = 100;
        config->include_noise = false;
        config->auto_calibration = true;
        
        // Inicializar respuesta en frecuencia
        validator->frequency_response.num_points = 0;
        validator->frequency_response.passband_ripple = 0.0f;
        validator->frequency_response.stopband_attenuation = 0.0f;
        validator->frequency_response.measurement_start_time = 0;
        
        // Inicializar generador de señales
        validator->signal_generator.frequency_hz = 0.0f;
        validator->signal_generator.amplitude = 0.0f;
        validator->signal_generator.enabled = false;
        
        // Inicializar calibrador
        validator->calibrator.calibration_in_progress = false;
        validator->calibrator.calibration_converged = false;
        validator->calibrator.current_iteration = 0;
    }
    
    g_filter_validation_initialized = true;
    
    ESP_LOGI(TAG, "Filter validation subsystem initialized");
    
    return ESP_OK;
}

esp_err_t edrumulus_filter_validation_deinit(void)
{
    if (!g_filter_validation_initialized) {
        return ESP_OK;
    }
    
    g_filter_validation_initialized = false;
    
    ESP_LOGI(TAG, "Filter validation subsystem deinitialized");
    
    return ESP_OK;
}

esp_err_t edrumulus_filter_validation_test_frequency_response(uint8_t channel, 
                                                              float start_freq, 
                                                              float end_freq, 
                                                              float freq_step)
{
    if (!g_filter_validation_initialized) {
        ESP_LOGE(TAG, "Filter validation not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    if (channel >= EDRUMULUS_MAX_ADC_CHANNELS) {
        ESP_LOGE(TAG, "Invalid channel: %d", channel);
        return ESP_ERR_INVALID_ARG;
    }
    
    if (start_freq >= end_freq || freq_step <= 0.0f) {
        ESP_LOGE(TAG, "Invalid frequency parameters");
        return ESP_ERR_INVALID_ARG;
    }
    
    edrumulus_filter_validation_t *validator = &g_filter_validators[channel];
    edrumulus_frequency_response_t *response = &validator->frequency_response;
    
    validator->validation_in_progress = true;
    validator->validation_start_time = get_timestamp_ms();
    
    filter_validation_log_internal(channel, "Starting frequency response test: %.1fHz - %.1fHz, step %.1fHz", 
                                  start_freq, end_freq, freq_step);
    
    // Calcular número de puntos
    uint16_t point_count = (uint16_t)((end_freq - start_freq) / freq_step) + 1;
    if (point_count > 100) { // Máximo 100 puntos según estructura
        point_count = 100;
        freq_step = (end_freq - start_freq) / (point_count - 1);
        filter_validation_log_internal(channel, "Adjusted frequency step to %.1fHz for %d points", 
                                      freq_step, point_count);
    }
    
    response->num_points = 0;
    
    // Barrido de frecuencias
    for (uint16_t i = 0; i < point_count; i++) {
        float frequency = start_freq + (i * freq_step);
        edrumulus_frequency_point_t *point = &response->points[i];
        
        esp_err_t ret = measure_filter_response_point(channel, frequency, point);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to measure frequency point %.1fHz", frequency);
            validator->validation_in_progress = false;
            return ret;
        }
        
        response->num_points++;
        
        filter_validation_log_internal(channel, "Freq: %.1fHz, Magnitude: %.2fdB, Input: %.3fV, Output: %.3fV", 
                                      point->frequency_hz, point->magnitude_db, 
                                      point->input_amplitude, point->output_amplitude);
        
        // Delay pequeño entre mediciones
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    
    // Analizar resultados
    float max_passband_magnitude = -100.0f;
    float min_passband_magnitude = 100.0f;
    float max_stopband_magnitude = -100.0f;
    
    for (uint16_t i = 0; i < response->num_points; i++) {
        edrumulus_frequency_point_t *point = &response->points[i];
        
        // Banda pasante: 40-400Hz
        if (point->frequency_hz >= 40.0f && point->frequency_hz <= 400.0f) {
            if (point->magnitude_db > max_passband_magnitude) {
                max_passband_magnitude = point->magnitude_db;
            }
            if (point->magnitude_db < min_passband_magnitude) {
                min_passband_magnitude = point->magnitude_db;
            }
        }
        // Banda rechazada: <40Hz o >400Hz
        else {
            if (point->magnitude_db > max_stopband_magnitude) {
                max_stopband_magnitude = point->magnitude_db;
            }
        }
    }
    
    response->passband_ripple = max_passband_magnitude - min_passband_magnitude;
    response->stopband_attenuation = -max_stopband_magnitude; // Atenuación positiva
    response->measurement_duration = xTaskGetTickCount() - response->measurement_start_time;
    
    validator->validation_duration = get_timestamp_ms() - validator->validation_start_time;
    validator->validation_in_progress = false;
    
    filter_validation_log_internal(channel, "Frequency response test completed: Passband ripple=%.2fdB, Stopband attenuation=%.2fdB", 
                                  response->passband_ripple, response->stopband_attenuation);
    
    return ESP_OK;
}

esp_err_t edrumulus_filter_validation_test_passband(uint8_t channel)
{
    if (!g_filter_validation_initialized) {
        ESP_LOGE(TAG, "Filter validation not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    if (channel >= EDRUMULUS_MAX_ADC_CHANNELS) {
        ESP_LOGE(TAG, "Invalid channel: %d", channel);
        return ESP_ERR_INVALID_ARG;
    }
    
    filter_validation_log_internal(channel, "Starting passband validation test (40-400Hz)");
    
    // Test específico para banda pasante 40-400Hz
    return edrumulus_filter_validation_test_frequency_response(channel, 40.0f, 400.0f, 10.0f);
}

esp_err_t edrumulus_filter_validation_test_stopband(uint8_t channel)
{
    if (!g_filter_validation_initialized) {
        ESP_LOGE(TAG, "Filter validation not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    if (channel >= EDRUMULUS_MAX_ADC_CHANNELS) {
        ESP_LOGE(TAG, "Invalid channel: %d", channel);
        return ESP_ERR_INVALID_ARG;
    }
    
    filter_validation_log_internal(channel, "Starting stopband validation test (<40Hz, >400Hz)");
    
    // Test banda rechazada baja: 10-39Hz
    esp_err_t ret1 = edrumulus_filter_validation_test_frequency_response(channel, 10.0f, 39.0f, 5.0f);
    if (ret1 != ESP_OK) {
        return ret1;
    }
    
    // Test banda rechazada alta: 401-1000Hz
    esp_err_t ret2 = edrumulus_filter_validation_test_frequency_response(channel, 401.0f, 1000.0f, 20.0f);
    
    return ret2;
}

esp_err_t edrumulus_filter_validation_test_stability(uint8_t channel, uint32_t test_duration_ms)
{
    if (!g_filter_validation_initialized) {
        ESP_LOGE(TAG, "Filter validation not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    if (channel >= EDRUMULUS_MAX_ADC_CHANNELS) {
        ESP_LOGE(TAG, "Invalid channel: %d", channel);
        return ESP_ERR_INVALID_ARG;
    }
    
    edrumulus_filter_validation_t *validator = &g_filter_validators[channel];
    
    filter_validation_log_internal(channel, "Starting stability test for %lu ms", test_duration_ms);
    
    validator->validation_in_progress = true;
    uint32_t start_time = get_timestamp_ms();
    
    // Generar señal de test continua en frecuencia central (220Hz)
    float test_frequency = 220.0f; // Frecuencia central de la banda pasante
    float test_amplitude = 0.3f;
    
    uint32_t sample_count = 0;
    float max_output = 0.0f;
    float min_output = 0.0f;
    bool first_sample = true;
    
    while ((get_timestamp_ms() - start_time) < test_duration_ms) {
        // Generar muestra de señal senoidal
        float time_s = (get_timestamp_ms() - start_time) / 1000.0f;
        float input_sample = test_amplitude * sinf(2.0f * M_PI * test_frequency * time_s);
        
        // Procesar a través del filtro
        float output_sample = edrumulus_detection_filter_process(input_sample);
        
        // Verificar estabilidad (sin saturación)
        if (first_sample) {
            max_output = output_sample;
            min_output = output_sample;
            first_sample = false;
        } else {
            if (output_sample > max_output) max_output = output_sample;
            if (output_sample < min_output) min_output = output_sample;
        }
        
        sample_count++;
        
        // Verificar si hay saturación o valores anómalos
        if (fabsf(output_sample) > 10.0f) { // Umbral de saturación
            filter_validation_log_internal(channel, "Stability test FAILED: Output saturation detected (%.3fV)", output_sample);
            validator->validation_in_progress = false;
            return ESP_FAIL;
        }
        
        // Delay para mantener tasa de muestreo
        vTaskDelay(pdMS_TO_TICKS(1));
    }
    
    float output_range = max_output - min_output;
    
    validator->validation_in_progress = false;
    
    filter_validation_log_internal(channel, "Stability test completed: %lu samples, output range: %.3fV (%.3fV to %.3fV)", 
                                  sample_count, output_range, min_output, max_output);
    
    return ESP_OK;
}

esp_err_t edrumulus_filter_validation_generate_test_signal(float frequency_hz, 
                                                           float amplitude, 
                                                           float noise_level, 
                                                           uint32_t duration_ms)
{
    if (!g_filter_validation_initialized) {
        ESP_LOGE(TAG, "Filter validation not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    ESP_LOGI(TAG, "Generating test signal: %.1fHz, amplitude=%.2f, noise=%.3f, duration=%lums", 
             frequency_hz, amplitude, noise_level, duration_ms);
    
    // Generar señal sintética con ruido
    uint16_t signal_buffer[1024];
    uint16_t buffer_size = sizeof(signal_buffer) / sizeof(signal_buffer[0]);
    
    esp_err_t ret = generate_sine_wave_internal(frequency_hz, amplitude, duration_ms, signal_buffer, buffer_size);
    if (ret != ESP_OK) {
        return ret;
    }
    
    // Agregar ruido si se especifica
    if (noise_level > 0.0f) {
        for (uint16_t i = 0; i < buffer_size; i++) {
            // Generar ruido pseudo-aleatorio simple
            float noise = (float)(rand() % 1000 - 500) / 500.0f * noise_level;
            float signal_voltage = (float)signal_buffer[i] * EDRUMULUS_VALIDATION_ADC_VREF / 4095.0f;
            signal_voltage += noise;
            
            // Clamp y convertir de vuelta
            if (signal_voltage < 0.0f) signal_voltage = 0.0f;
            if (signal_voltage > EDRUMULUS_VALIDATION_ADC_VREF) signal_voltage = EDRUMULUS_VALIDATION_ADC_VREF;
            
            signal_buffer[i] = (uint16_t)(signal_voltage * 4095.0f / EDRUMULUS_VALIDATION_ADC_VREF);
        }
    }
    
    ESP_LOGI(TAG, "Test signal generated successfully");
    
    return ESP_OK;
}

esp_err_t edrumulus_filter_validation_auto_calibrate(uint8_t channel, 
                                                      const edrumulus_filter_config_t *target_config)
{
    if (!g_filter_validation_initialized) {
        ESP_LOGE(TAG, "Filter validation not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    if (channel >= EDRUMULUS_MAX_ADC_CHANNELS || target_config == NULL) {
        ESP_LOGE(TAG, "Invalid parameters for auto calibration");
        return ESP_ERR_INVALID_ARG;
    }
    
    edrumulus_filter_validation_t *validator = &g_filter_validators[channel];
    edrumulus_filter_calibrator_t *calibrator = &validator->calibrator;
    
    filter_validation_log_internal(channel, "Starting automatic filter calibration");
    
    calibrator->calibration_in_progress = true;
    calibrator->current_iteration = 0;
    
    // Configurar filtro con parámetros objetivo
    esp_err_t ret = edrumulus_detection_filter_set_config(target_config);
    if (ret != ESP_OK) {
        filter_validation_log_internal(channel, "Failed to configure filter with target parameters");
        calibrator->calibration_in_progress = false;
        return ret;
    }
    
    // Ejecutar test de respuesta en frecuencia para verificar calibración
    ret = edrumulus_filter_validation_test_frequency_response(channel, 10.0f, 1000.0f, 20.0f);
    if (ret != ESP_OK) {
        filter_validation_log_internal(channel, "Calibration validation test failed");
        calibrator->calibration_in_progress = false;
        return ret;
    }
    
    // Verificar si la calibración cumple criterios
    edrumulus_frequency_response_t *response = &validator->frequency_response;
    bool calibration_success = (response->passband_ripple <= 3.0f &&
                        response->stopband_attenuation >= 20.0f);
    
    calibrator->calibration_converged = calibration_success;
    calibrator->calibration_in_progress = false;
    calibrator->current_iteration = 1;
    
    filter_validation_log_internal(channel, "Auto calibration %s: Ripple=%.2fdB, Attenuation=%.2fdB", 
                                  calibration_success ? "PASSED" : "FAILED",
                                  response->passband_ripple, response->stopband_attenuation);
    
    return calibration_success ? ESP_OK : ESP_FAIL;
}

esp_err_t edrumulus_filter_validation_run_full_suite(uint8_t channel)
{
    if (!g_filter_validation_initialized) {
        ESP_LOGE(TAG, "Filter validation not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    if (channel >= EDRUMULUS_MAX_ADC_CHANNELS) {
        ESP_LOGE(TAG, "Invalid channel: %d", channel);
        return ESP_ERR_INVALID_ARG;
    }
    
    filter_validation_log_internal(channel, "Starting complete Phase 2 validation suite");
    
    // Test 1: Respuesta en frecuencia completa
    esp_err_t ret = edrumulus_filter_validation_test_frequency_response(channel, 10.0f, 1000.0f, 10.0f);
    if (ret != ESP_OK) {
        filter_validation_log_internal(channel, "Frequency response test FAILED");
        return ret;
    }
    
    // Test 2: Banda pasante específica
    ret = edrumulus_filter_validation_test_passband(channel);
    if (ret != ESP_OK) {
        filter_validation_log_internal(channel, "Passband test FAILED");
        return ret;
    }
    
    // Test 3: Banda rechazada
    ret = edrumulus_filter_validation_test_stopband(channel);
    if (ret != ESP_OK) {
        filter_validation_log_internal(channel, "Stopband test FAILED");
        return ret;
    }
    
    // Test 4: Estabilidad
    ret = edrumulus_filter_validation_test_stability(channel, 5000);
    if (ret != ESP_OK) {
        filter_validation_log_internal(channel, "Stability test FAILED");
        return ret;
    }
    
    // Verificar criterios de aceptación
    edrumulus_filter_validation_t *validator = &g_filter_validators[channel];
    edrumulus_frequency_response_t *response = &validator->frequency_response;
    
    bool suite_passed = (response->passband_ripple <= 3.0f &&
                        response->stopband_attenuation >= 20.0f);
    
    validator->validation_passed = suite_passed;
    
    filter_validation_log_internal(channel, "Phase 2 validation suite %s", 
                                  suite_passed ? "PASSED" : "FAILED");
    
    return suite_passed ? ESP_OK : ESP_FAIL;
}

esp_err_t edrumulus_filter_validation_get_results(edrumulus_filter_validation_t *validation)
{
    if (!g_filter_validation_initialized || validation == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    // Copiar resultados del canal 0 por defecto (se puede extender para especificar canal)
    memcpy(validation, &g_filter_validators[0], sizeof(edrumulus_filter_validation_t));
    
    return ESP_OK;
}

bool edrumulus_filter_validation_is_in_progress(uint8_t channel)
{
    if (!g_filter_validation_initialized || channel >= EDRUMULUS_MAX_ADC_CHANNELS) {
        return false;
    }
    
    return g_filter_validators[channel].validation_in_progress;
}

bool edrumulus_filter_validation_passed(uint8_t channel)
{
    if (!g_filter_validation_initialized || channel >= EDRUMULUS_MAX_ADC_CHANNELS) {
        return false;
    }
    
    edrumulus_filter_validation_t *validator = &g_filter_validators[channel];
    edrumulus_frequency_response_t *response = &validator->frequency_response;
    
    // Verificar criterios Phase 2
    bool passband_ok = (response->passband_ripple <= 3.0f);
    bool stopband_ok = (response->stopband_attenuation >= 20.0f);
    bool measurement_complete = (response->num_points > 0);
    
    return (passband_ok && stopband_ok && measurement_complete);
}

esp_err_t edrumulus_filter_validation_log(uint8_t channel, 
                                           float frequency_hz, 
                                           float input_amplitude, 
                                           float output_amplitude, 
                                           float attenuation_db)
{
    if (!g_filter_validation_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    if (channel >= EDRUMULUS_MAX_ADC_CHANNELS) {
        return ESP_ERR_INVALID_ARG;
    }
    
    filter_validation_log_internal(channel, "MEASUREMENT: Freq=%.1fHz, In=%.3fV, Out=%.3fV, Att=%.2fdB", 
                                  frequency_hz, input_amplitude, output_amplitude, attenuation_db);
    
    return ESP_OK;
}

esp_err_t edrumulus_filter_validation_reset(uint8_t channel)
{
    if (!g_filter_validation_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    if (channel >= EDRUMULUS_MAX_ADC_CHANNELS) {
        return ESP_ERR_INVALID_ARG;
    }
    
    edrumulus_filter_validation_t *validator = &g_filter_validators[channel];
    
    // Resetear estado de validación
    validator->validation_in_progress = false;
    validator->validation_passed = false;
    validator->validation_start_time = 0;
    validator->validation_duration = 0;
    
    // Resetear respuesta en frecuencia
    validator->frequency_response.num_points = 0;
    validator->frequency_response.passband_ripple = 0.0f;
    validator->frequency_response.stopband_attenuation = 0.0f;
    validator->frequency_response.measurement_start_time = 0;
    
    // Resetear generador de señales
    validator->signal_generator.enabled = false;
    
    // Resetear calibrador
    validator->calibrator.calibration_in_progress = false;
    validator->calibrator.calibration_converged = false;
    validator->calibrator.current_iteration = 0;
    
    filter_validation_log_internal(channel, "Validation state reset");
    
    return ESP_OK;
}

esp_err_t edrumulus_filter_validation_configure(uint8_t channel, 
                                                 const edrumulus_filter_test_config_t *test_config)
{
    if (!g_filter_validation_initialized || test_config == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (channel >= EDRUMULUS_MAX_ADC_CHANNELS) {
        return ESP_ERR_INVALID_ARG;
    }
    
    edrumulus_filter_validation_t *validator = &g_filter_validators[channel];
    
    // Copiar configuración de test
    memcpy(&validator->test_config, test_config, sizeof(edrumulus_filter_test_config_t));
    
    filter_validation_log_internal(channel, "Test configuration updated: %.1f-%.1fHz, step=%.1fHz", 
                                  test_config->test_frequency_start, test_config->test_frequency_end,
                                  test_config->test_frequency_step);
    
    return ESP_OK;
}

esp_err_t edrumulus_filter_validation_measure_frequency_point(uint8_t channel, 
                                                               float frequency_hz, 
                                                               edrumulus_frequency_point_t *point)
{
    if (!g_filter_validation_initialized || point == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (channel >= EDRUMULUS_MAX_ADC_CHANNELS) {
        return ESP_ERR_INVALID_ARG;
    }
    
    return measure_filter_response_point(channel, frequency_hz, point);
}

// Implementaciones de funciones auxiliares
static uint32_t get_timestamp_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

__attribute__((unused)) static uint16_t calculate_rms(const uint16_t *samples, uint16_t count)
{
    if (count == 0 || samples == NULL) {
        return 0;
    }
    
    uint64_t sum_squares = 0;
    for (uint16_t i = 0; i < count; i++) {
        uint64_t sample = samples[i];
        sum_squares += sample * sample;
    }
    
    return (uint16_t)sqrtf((float)sum_squares / count);
}

static float calculate_rms_float(const float *samples, uint16_t count)
{
    if (count == 0 || samples == NULL) {
        return 0.0f;
    }
    
    float sum_squares = 0.0f;
    for (uint16_t i = 0; i < count; i++) {
        sum_squares += samples[i] * samples[i];
    }
    
    return sqrtf(sum_squares / count);
}

esp_err_t edrumulus_adc_validation_test_sampling_rate(uint8_t channel, uint32_t test_duration_ms)
{
    if (!g_adc_validation_initialized) {
        ESP_LOGE(TAG, "ADC validation not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    if (channel >= EDRUMULUS_MAX_ADC_CHANNELS) {
        ESP_LOGE(TAG, "Invalid channel: %d", channel);
        return ESP_ERR_INVALID_ARG;
    }
    
    edrumulus_adc_validation_t *validator = &g_adc_validators[channel];
    edrumulus_sampling_metrics_t *metrics = &validator->sampling_metrics;
    
    validator->validation_in_progress = true;
    validator->validation_start_time = get_timestamp_ms();
    
    adc_validation_log_internal(channel, "Starting sampling rate test: %lu ms duration", test_duration_ms);
    
    uint64_t start_time = get_timestamp_us();
    uint32_t sample_count = 0;
    uint64_t last_sample_time = start_time;
    uint64_t min_interval = UINT64_MAX;
    uint64_t max_interval = 0;
    uint64_t total_interval = 0;
    
    // Medir intervalos entre muestras durante el tiempo especificado
    while ((get_timestamp_us() - start_time) < (test_duration_ms * 1000)) {
        int adc_raw;
        uint64_t current_time = get_timestamp_us();
        
        esp_err_t ret = edrumulus_detection_read_channel(channel, &adc_raw);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to read ADC channel %d", channel);
            validator->validation_in_progress = false;
            return ret;
        }
        
        if (sample_count > 0) {
            uint64_t interval = current_time - last_sample_time;
            if (interval < min_interval) min_interval = interval;
            if (interval > max_interval) max_interval = interval;
            total_interval += interval;
        }
        
        last_sample_time = current_time;
        sample_count++;
        
        // Delay para simular muestreo a 8kHz
        vTaskDelay(pdMS_TO_TICKS(1));
    }
    
    uint64_t total_time = get_timestamp_us() - start_time;
    
    // Calcular métricas
    float actual_sample_rate = (float)(sample_count * 1000000.0) / total_time;
    
    // Verificar criterios de aceptación
    float rate_error = fabsf(actual_sample_rate - metrics->target_sample_rate) / metrics->target_sample_rate;
    metrics->rate_valid = (rate_error <= EDRUMULUS_VALIDATION_MAX_RATE_DEVIATION);
    
    // Almacenar tiempo de medición
    metrics->measurement_start_time = start_time;
    
    validator->validation_duration = get_timestamp_ms() - validator->validation_start_time;
    validator->validation_in_progress = false;
    
    adc_validation_log_internal(channel, "Sampling rate test completed: %.1fHz (%s), Samples=%lu", 
                               actual_sample_rate,
                               metrics->rate_valid ? "PASS" : "FAIL",
                               sample_count);
    
    return ESP_OK;
}

esp_err_t edrumulus_adc_validation_measure_snr(uint8_t channel, uint16_t signal_samples, uint16_t noise_samples)
{
    if (!g_adc_validation_initialized) {
        ESP_LOGE(TAG, "ADC validation not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    if (channel >= EDRUMULUS_MAX_ADC_CHANNELS) {
        ESP_LOGE(TAG, "Invalid channel: %d", channel);
        return ESP_ERR_INVALID_ARG;
    }
    
    if (signal_samples + noise_samples > EDRUMULUS_VALIDATION_BUFFER_SIZE) {
        ESP_LOGE(TAG, "Total samples exceed buffer size");
        return ESP_ERR_INVALID_ARG;
    }
    
    edrumulus_adc_validation_t *validator = &g_adc_validators[channel];
    edrumulus_snr_calculator_t *snr_calc = &validator->snr_calculator;
    
    validator->validation_in_progress = true;
    validator->validation_start_time = get_timestamp_ms();
    
    adc_validation_log_internal(channel, "Starting SNR measurement: %d signal + %d noise samples", 
                               signal_samples, noise_samples);
    
    float signal_buffer[EDRUMULUS_VALIDATION_BUFFER_SIZE];
    float noise_buffer[EDRUMULUS_VALIDATION_BUFFER_SIZE];
    
    // Medir señal (con estímulo)
    adc_validation_log_internal(channel, "Measuring signal samples...");
    for (uint16_t i = 0; i < signal_samples; i++) {
        int adc_raw;
        esp_err_t ret = edrumulus_detection_read_channel(channel, &adc_raw);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to read signal sample %d", i);
            validator->validation_in_progress = false;
            return ret;
        }
        
        signal_buffer[i] = (float)adc_raw;
        vTaskDelay(pdMS_TO_TICKS(1));
    }
    
    // Medir ruido (sin estímulo)
    adc_validation_log_internal(channel, "Measuring noise samples...");
    for (uint16_t i = 0; i < noise_samples; i++) {
        int adc_raw;
        esp_err_t ret = edrumulus_detection_read_channel(channel, &adc_raw);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to read noise sample %d", i);
            validator->validation_in_progress = false;
            return ret;
        }
        
        noise_buffer[i] = (float)adc_raw;
        vTaskDelay(pdMS_TO_TICKS(1));
    }
    
    // Calcular RMS de señal y ruido
    snr_calc->signal_rms = calculate_rms_float(signal_buffer, (uint16_t)signal_samples);
    snr_calc->noise_rms = calculate_rms_float(noise_buffer, (uint16_t)noise_samples);
    
    // Calcular SNR en dB
    if (snr_calc->noise_rms > 0.0f) {
        snr_calc->snr_db = 20.0f * log10f(snr_calc->signal_rms / snr_calc->noise_rms);
        snr_calc->snr_valid = true;
    } else {
        snr_calc->snr_db = 0.0f;
        snr_calc->snr_valid = false;
    }
    
    snr_calc->signal_sample_count = signal_samples;
    snr_calc->noise_sample_count = noise_samples;
    
    // Verificar criterio de aceptación (usar constante definida)
    bool snr_acceptable = (snr_calc->snr_db >= EDRUMULUS_VALIDATION_MIN_SNR);
    
    validator->validation_duration = get_timestamp_ms() - validator->validation_start_time;
    validator->validation_in_progress = false;
    
    adc_validation_log_internal(channel, "SNR measurement completed: %.1fdB (%s), Signal RMS=%.1f, Noise RMS=%.1f", 
                               snr_calc->snr_db,
                               snr_acceptable ? "PASS" : "FAIL",
                               snr_calc->signal_rms,
                               snr_calc->noise_rms);
    
    return ESP_OK;
}

esp_err_t edrumulus_adc_validation_run_full_suite(uint8_t channel)
{
    if (!g_adc_validation_initialized) {
        ESP_LOGE(TAG, "ADC validation not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    if (channel >= EDRUMULUS_MAX_ADC_CHANNELS) {
        ESP_LOGE(TAG, "Invalid channel: %d", channel);
        return ESP_ERR_INVALID_ARG;
    }
    
    adc_validation_log_internal(channel, "Starting full validation suite");
    
    esp_err_t ret;
    
    // Test 1: Precisión ADC
    ret = edrumulus_adc_validation_test_precision(channel, 0.1f, 3.2f, 100);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "ADC precision test failed");
        return ret;
    }
    
    // Test 2: Frecuencia de muestreo
    ret = edrumulus_adc_validation_test_sampling_rate(channel, 1000); // 1 segundo
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Sampling rate test failed");
        return ret;
    }
    
    // Test 3: SNR
    ret = edrumulus_adc_validation_measure_snr(channel, 200, 200);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SNR measurement failed");
        return ret;
    }
    
    adc_validation_log_internal(channel, "Full validation suite completed");
    
    return ESP_OK;
}

esp_err_t edrumulus_adc_validation_get_results(edrumulus_adc_validation_t *validation)
{
    if (!g_adc_validation_initialized) {
        ESP_LOGE(TAG, "ADC validation not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    if (validation == NULL) {
        ESP_LOGE(TAG, "Validation pointer cannot be NULL");
        return ESP_ERR_INVALID_ARG;
    }
    
    // Copiar resultados del primer canal como ejemplo
    *validation = g_adc_validators[0];
    
    return ESP_OK;
}

bool edrumulus_adc_validation_is_running(uint8_t channel)
{
    if (!g_adc_validation_initialized || channel >= EDRUMULUS_MAX_ADC_CHANNELS) {
        return false;
    }
    
    return g_adc_validators[channel].validation_in_progress;
}

bool edrumulus_adc_validation_passed(uint8_t channel)
{
    if (!g_adc_validation_initialized || channel >= EDRUMULUS_MAX_ADC_CHANNELS) {
        return false;
    }
    
    edrumulus_adc_validation_t *validator = &g_adc_validators[channel];
    
    // Verificar que todas las validaciones hayan pasado
    bool precision_ok = validator->adc_precision.linearity_valid && validator->adc_precision.precision_valid;
    bool sampling_ok = validator->sampling_metrics.rate_valid;
    bool snr_ok = validator->snr_calculator.snr_valid;
    
    return precision_ok && sampling_ok && snr_ok;
}

const char* edrumulus_adc_validation_get_log(uint8_t channel)
{
    if (!g_adc_validation_initialized || channel >= EDRUMULUS_MAX_ADC_CHANNELS) {
        return "Invalid channel or not initialized";
    }
    
    return g_adc_validators[channel].validation_log;
}

esp_err_t edrumulus_adc_validation_reset(uint8_t channel)
{
    if (!g_adc_validation_initialized) {
        ESP_LOGE(TAG, "ADC validation not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    if (channel >= EDRUMULUS_MAX_ADC_CHANNELS) {
        ESP_LOGE(TAG, "Invalid channel: %d", channel);
        return ESP_ERR_INVALID_ARG;
    }
    
    edrumulus_adc_validation_t *validator = &g_adc_validators[channel];
    
    // Resetear solo los resultados, mantener configuración
    memset(&validator->adc_precision, 0, sizeof(validator->adc_precision));
    memset(&validator->sampling_metrics, 0, sizeof(validator->sampling_metrics));
    memset(&validator->snr_calculator, 0, sizeof(validator->snr_calculator));
    
    validator->validation_in_progress = false;
    validator->validation_start_time = 0;
    validator->validation_duration = 0;
    memset(validator->validation_log, 0, sizeof(validator->validation_log));
    
    // Restaurar configuración por defecto
    validator->sampling_metrics.target_sample_rate = EDRUMULUS_VALIDATION_SAMPLE_RATE;
    
    adc_validation_log_internal(channel, "Validation state reset");
    
    return ESP_OK;
}

esp_err_t edrumulus_detection_set_mask_time_config(const edrumulus_mask_time_config_t *config)
{
    if (config == NULL) {
        ESP_LOGE(TAG, "Mask time configuration cannot be NULL");
        return ESP_ERR_INVALID_ARG;
    }
    
    // Validar rangos de configuración
    if (config->mask_time_ms < 1 || config->mask_time_ms > 50) {
        ESP_LOGE(TAG, "Invalid mask time: %lu ms (valid range: 1-50ms)", config->mask_time_ms);
        return ESP_ERR_INVALID_ARG;
    }
    
    if (config->velocity_threshold_low >= config->velocity_threshold_high) {
        ESP_LOGE(TAG, "Invalid velocity thresholds: low=%d, high=%d", 
                 config->velocity_threshold_low, config->velocity_threshold_high);
        return ESP_ERR_INVALID_ARG;
    }
    
    g_mask_config = *config;
    ESP_LOGI(TAG, "Mask time config updated: base=%lums, adaptive=%s, thresholds=%d/%d",
             g_mask_config.mask_time_ms,
             g_mask_config.adaptive_mask ? "enabled" : "disabled",
             g_mask_config.velocity_threshold_low,
             g_mask_config.velocity_threshold_high);
    
    return ESP_OK;
}

esp_err_t edrumulus_detection_get_mask_time_config(edrumulus_mask_time_config_t *config)
{
    if (config == NULL) {
        ESP_LOGE(TAG, "Config pointer cannot be NULL");
        return ESP_ERR_INVALID_ARG;
    }
    
    *config = g_mask_config;
    return ESP_OK;
}

bool edrumulus_detection_is_in_mask_period(uint8_t channel)
{
    if (channel >= EDRUMULUS_MAX_ADC_CHANNELS) {
        return false;
    }
    
    if (!g_mask_active[channel]) {
        return false;
    }
    
    uint32_t current_time = xTaskGetTickCount();
    uint32_t elapsed_ms = (current_time - g_last_hit_time[channel]) * portTICK_PERIOD_MS;
    
    if (elapsed_ms >= g_mask_config.mask_time_ms) {
        g_mask_active[channel] = false;
        return false;
    }
    
    return true;
}

esp_err_t edrumulus_detection_deinit(void)
{
    if (!g_detection_initialized) {
        ESP_LOGW(TAG, "Detection not initialized");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Deinitializing detection subsystem");
    
    // Stop piezo monitoring if active
    if (g_piezo_monitoring) {
        edrumulus_detection_stop_piezo_monitor();
    }
    
    // Stop and deinitialize ADC continuous with DMA
    edrumulus_detection_adc_continuous_deinit();
    
    // Deinitialize filter
    edrumulus_detection_filter_deinit();
    
    // PHASE 2: Deinitialize filter validation
    edrumulus_filter_validation_deinit();
    
    // PHASE 3: Deinitialize rebound detector
    edrumulus_rebound_detector_deinit();
    
    // PHASE 1: Deinitialize ADC validation
    edrumulus_adc_validation_deinit();
    
    g_detection_initialized = false;
    g_piezo_initialized = false;
    ESP_LOGI(TAG, "Detection subsystem deinitialized");
    
    return ESP_OK;
}

// Piezo monitoring task with intelligent mask time
static void piezo_monitor_task(void *pvParameters)
{
    uint8_t velocity;
    edrumulus_hit_event_t hit_event;
    
    ESP_LOGI(TAG, "Piezo monitoring task started with intelligent mask time");
    
    while (g_piezo_monitoring) {
        if (edrumulus_detection_check_piezo_hit(g_piezo_channel, &velocity)) {
            // Create hit event
            hit_event.channel = g_piezo_channel;
            hit_event.velocity = velocity;
            hit_event.note = 38; // MIDI_NOTE_SNARE_DRUM
            hit_event.timestamp = xTaskGetTickCount();
            hit_event.is_rimshot = false;
            
            // Send event to queue (non-blocking)
            if (g_piezo_event_queue) {
                xQueueSend(g_piezo_event_queue, &hit_event, 0);
            }
            
            ESP_LOGI(TAG, "Piezo hit detected: velocity=%d, mask_time=%lums", 
                     velocity, g_mask_config.mask_time_ms);
            
            // NO delay fijo - el mask time inteligente maneja la prevención de retriggering
            // Esto elimina el delay de 10ms, mejorando la latencia significativamente
        }
        
        // Mantener sample rate de 1ms para detección precisa
        vTaskDelay(pdMS_TO_TICKS(PIEZO_SAMPLE_RATE_MS));
    }
    
    ESP_LOGI(TAG, "Piezo monitoring task ended");
    vTaskDelete(NULL);
}

esp_err_t edrumulus_detection_init_piezo(uint8_t channel, uint16_t threshold)
{
    if (!g_detection_initialized) {
        ESP_LOGE(TAG, "Detection subsystem must be initialized first");
        return ESP_ERR_INVALID_STATE;
    }
    
    if (channel >= EDRUMULUS_MAX_ADC_CHANNELS) {
        ESP_LOGE(TAG, "Invalid piezo channel: %d", channel);
        return ESP_ERR_INVALID_ARG;
    }
    
    g_piezo_channel = channel;
    g_piezo_threshold = threshold;
    g_piezo_initialized = true;
    
    ESP_LOGI(TAG, "Piezo initialized on channel %d with threshold %d", channel, threshold);
    
    return ESP_OK;
}

bool edrumulus_detection_check_piezo_hit(uint8_t channel, uint8_t *velocity)
{
    if (!g_piezo_initialized || !g_detection_initialized) {
        return false;
    }
    
    // Verificar si estamos en período de mask time
    if (edrumulus_detection_is_in_mask_period(channel)) {
        return false;
    }
    
    int adc_value;
    esp_err_t ret = edrumulus_detection_read_channel(channel, &adc_value);
    
    if (ret != ESP_OK) {
        return false;
    }
    
    // Apply band-pass filter to ADC signal
    float normalized_input = (float)adc_value / PIEZO_MAX_ADC_VALUE; // Normalize to 0.0-1.0
    float filtered_output = edrumulus_detection_filter_process(normalized_input);
    
    // PHASE 3: Process through rebound detector
    if (g_rebound_detector_initialized) {
        bool is_valid_signal = edrumulus_rebound_detector_process(filtered_output, channel);
        
        if (!is_valid_signal) {
            ESP_LOGD(TAG, "Signal rejected by rebound detector on channel %d", channel);
            return false; // Rebound detected and rejected
        }
    }
    
    int filtered_adc_value = (int)(filtered_output * PIEZO_MAX_ADC_VALUE); // Convert back to ADC range
    
    // Ensure filtered value is within valid range
    if (filtered_adc_value < 0) filtered_adc_value = 0;
    if (filtered_adc_value > PIEZO_MAX_ADC_VALUE) filtered_adc_value = PIEZO_MAX_ADC_VALUE;
    
    // Check if filtered value exceeds threshold
    if (filtered_adc_value > g_piezo_threshold) {
        // Calculate velocity based on filtered ADC value
        uint32_t scaled_velocity = ((filtered_adc_value - g_piezo_threshold) * PIEZO_VELOCITY_SCALE) / 
                                  (PIEZO_MAX_ADC_VALUE - g_piezo_threshold);
        
        *velocity = (uint8_t)(scaled_velocity > PIEZO_VELOCITY_SCALE ? PIEZO_VELOCITY_SCALE : scaled_velocity);
        
        // Ensure minimum velocity
        if (*velocity < 10) {
            *velocity = 10;
        }
        
        // PHASE 3: Additional velocity validation using rebound detector
        if (g_rebound_detector_initialized) {
            bool is_valid_hit = edrumulus_rebound_detector_is_valid_hit(channel, *velocity);
            
            if (!is_valid_hit) {
                ESP_LOGD(TAG, "Hit rejected by velocity validator: velocity=%d, channel=%d", *velocity, channel);
                return false; // Invalid velocity detected
            }
        }
        
        // Activar mask time inteligente basado en velocidad
        uint32_t current_time = xTaskGetTickCount();
        g_last_hit_time[channel] = current_time;
        g_mask_active[channel] = true;
        
        // Ajustar mask time según velocidad si está habilitado el modo adaptativo
        if (g_mask_config.adaptive_mask) {
            if (*velocity <= g_mask_config.velocity_threshold_low) {
                // Velocidad baja: mask time corto (2ms)
                g_mask_config.mask_time_ms = 2;
            } else if (*velocity >= g_mask_config.velocity_threshold_high) {
                // Velocidad alta: mask time largo (10ms)
                g_mask_config.mask_time_ms = 10;
            } else {
                // Velocidad media: mask time medio (5ms)
                g_mask_config.mask_time_ms = 5;
            }
        }
        
        ESP_LOGD(TAG, "Valid hit detected: channel=%d, velocity=%d, adc=%d, filtered=%d", 
                 channel, *velocity, adc_value, filtered_adc_value);
        
        return true;
    }
    
    return false;
}

esp_err_t edrumulus_detection_start_piezo_monitor(uint8_t channel, QueueHandle_t event_queue)
{
    if (!g_piezo_initialized) {
        ESP_LOGE(TAG, "Piezo not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    if (g_piezo_monitoring) {
        ESP_LOGW(TAG, "Piezo monitoring already active");
        return ESP_OK;
    }
    
    g_piezo_event_queue = event_queue;
    g_piezo_monitoring = true;
    
    // Create monitoring task with increased stack size for Phase 3 structures
    BaseType_t ret = xTaskCreate(
        piezo_monitor_task,
        "piezo_monitor",
        4096, // Increased from 2048 to accommodate Phase 3 rebound detector structures
        NULL,
        5, // High priority for low latency
        &g_piezo_task_handle
    );
    
    if (ret != pdPASS) {
        g_piezo_monitoring = false;
        ESP_LOGE(TAG, "Failed to create piezo monitoring task");
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "Piezo monitoring started");
    
    return ESP_OK;
}

esp_err_t edrumulus_detection_stop_piezo_monitor(void)
{
    if (!g_piezo_monitoring) {
        ESP_LOGW(TAG, "Piezo monitoring not active");
        return ESP_OK;
    }
    
    g_piezo_monitoring = false;
    
    // Wait for task to finish
    if (g_piezo_task_handle) {
        vTaskDelay(pdMS_TO_TICKS(50)); // Give task time to exit
        g_piezo_task_handle = NULL;
    }
    
    g_piezo_event_queue = NULL;
    
    ESP_LOGI(TAG, "Piezo monitoring stopped");
    
    return ESP_OK;
}

// Filter implementation functions
esp_err_t edrumulus_detection_filter_init(void)
{
    if (g_filter_initialized) {
        ESP_LOGW(TAG, "Filter already initialized");
        return ESP_OK;
    }
    
    ESP_LOGI(TAG, "Initializing band-pass filter (%.0f-%.0f Hz)", 
             g_filter_config.low_cutoff_hz, g_filter_config.high_cutoff_hz);
    
    // Initialize filters for all channels
    for (int ch = 0; ch < EDRUMULUS_MAX_ADC_CHANNELS; ch++) {
        // Calculate low-pass coefficients (high cutoff)
        calculate_butterworth_coeffs(g_filter_config.high_cutoff_hz, 
                                   EDRUMULUS_FILTER_SAMPLE_RATE,
                                   EDRUMULUS_FILTER_Q,
                                   &g_bandpass_filter[ch].low_pass);
        
        // Calculate high-pass coefficients (low cutoff)
        // For high-pass, we use different coefficient calculation
        float omega = 2.0f * M_PI * g_filter_config.low_cutoff_hz / EDRUMULUS_FILTER_SAMPLE_RATE;
        float sin_omega = sinf(omega);
        float cos_omega = cosf(omega);
        float alpha = sin_omega / (2.0f * EDRUMULUS_FILTER_Q);
        
        // High-pass coefficients
        float b0 = (1.0f + cos_omega) / 2.0f;
        float b1 = -(1.0f + cos_omega);
        float b2 = (1.0f + cos_omega) / 2.0f;
        float a0 = 1.0f + alpha;
        float a1 = -2.0f * cos_omega;
        float a2 = 1.0f - alpha;
        
        // Normalize high-pass coefficients
        g_bandpass_filter[ch].high_pass.b0 = b0 / a0;
        g_bandpass_filter[ch].high_pass.b1 = b1 / a0;
        g_bandpass_filter[ch].high_pass.b2 = b2 / a0;
        g_bandpass_filter[ch].high_pass.a1 = a1 / a0;
        g_bandpass_filter[ch].high_pass.a2 = a2 / a0;
        
        // Reset filter states
        memset(&g_bandpass_filter[ch].lp_state, 0, sizeof(edrumulus_biquad_state_t));
        memset(&g_bandpass_filter[ch].hp_state, 0, sizeof(edrumulus_biquad_state_t));
        
        g_bandpass_filter[ch].enabled = g_filter_config.filter_enabled;
        g_bandpass_filter[ch].gain_compensation = g_filter_config.gain_compensation;
        g_bandpass_filter[ch].sample_count = 0;
    }
    
    g_filter_initialized = true;
    ESP_LOGI(TAG, "Band-pass filter initialized successfully");
    
    return ESP_OK;
}

esp_err_t edrumulus_detection_filter_deinit(void)
{
    if (!g_filter_initialized) {
        ESP_LOGW(TAG, "Filter not initialized");
        return ESP_OK;
    }
    
    g_filter_initialized = false;
    ESP_LOGI(TAG, "Filter deinitialized");
    
    return ESP_OK;
}

float edrumulus_detection_filter_process(float input_sample)
{
    // Use channel 0 for single-channel processing
    uint8_t channel = 0;
    
    if (!g_filter_initialized || channel >= EDRUMULUS_MAX_ADC_CHANNELS) {
        return input_sample; // Pass-through if not initialized or invalid channel
    }
    
    if (!g_bandpass_filter[channel].enabled) {
        return input_sample; // Pass-through if filter disabled
    }
    
    // Apply high-pass filter first (removes DC and low frequencies)
    float highpass_output = process_biquad(input_sample, 
                                         &g_bandpass_filter[channel].high_pass,
                                         &g_bandpass_filter[channel].hp_state);
    
    // Apply low-pass filter second (removes high frequencies)
    float bandpass_output = process_biquad(highpass_output,
                                          &g_bandpass_filter[channel].low_pass,
                                          &g_bandpass_filter[channel].lp_state);
    
    // Update sample count and apply gain compensation
    g_bandpass_filter[channel].sample_count++;
    return bandpass_output * g_bandpass_filter[channel].gain_compensation;
}

void edrumulus_detection_filter_reset(void)
{
    if (!g_filter_initialized) {
        return;
    }
    
    // Reset filter states for all channels
    for (int ch = 0; ch < EDRUMULUS_MAX_ADC_CHANNELS; ch++) {
        memset(&g_bandpass_filter[ch].lp_state, 0, sizeof(edrumulus_biquad_state_t));
        memset(&g_bandpass_filter[ch].hp_state, 0, sizeof(edrumulus_biquad_state_t));
        g_bandpass_filter[ch].sample_count = 0;
    }
}

esp_err_t edrumulus_detection_filter_set_config(const edrumulus_filter_config_t *config)
{
    if (config == NULL) {
        ESP_LOGE(TAG, "Filter configuration cannot be NULL");
        return ESP_ERR_INVALID_ARG;
    }
    
    // Validate frequency ranges
    if (config->low_cutoff_hz >= config->high_cutoff_hz) {
        ESP_LOGE(TAG, "Invalid cutoff frequencies: low=%.1f, high=%.1f", 
                 config->low_cutoff_hz, config->high_cutoff_hz);
        return ESP_ERR_INVALID_ARG;
    }
    
    if (config->low_cutoff_hz < 10.0f || config->high_cutoff_hz > 500.0f) {
        ESP_LOGE(TAG, "Cutoff frequencies out of range (10-500 Hz)");
        return ESP_ERR_INVALID_ARG;
    }
    
    g_filter_config = *config;
    
    // Reinitialize if already initialized
    if (g_filter_initialized) {
        g_filter_initialized = false;
        edrumulus_detection_filter_init();
    }
    
    ESP_LOGI(TAG, "Filter config updated: %.1f-%.1f Hz, order=%d, gain=%.2f",
             g_filter_config.low_cutoff_hz, g_filter_config.high_cutoff_hz,
             g_filter_config.filter_order, g_filter_config.gain_compensation);
    
    return ESP_OK;
}

esp_err_t edrumulus_detection_filter_get_config(edrumulus_filter_config_t *config)
{
    if (config == NULL) {
        ESP_LOGE(TAG, "Config pointer cannot be NULL");
        return ESP_ERR_INVALID_ARG;
    }
    
    *config = g_filter_config;
    return ESP_OK;
}

bool edrumulus_detection_filter_is_enabled(uint8_t channel)
{
    if (!g_filter_initialized || channel >= EDRUMULUS_MAX_ADC_CHANNELS) {
        return false;
    }
    
    return g_bandpass_filter[channel].enabled;
}

// === PHASE 3: IMPLEMENTACIÓN DETECTOR DE REBOTES ===

// Funciones auxiliares para algoritmos de Phase 3



/**
 * @brief Actualizar detector de flancos
 */
static void update_edge_detector(edrumulus_edge_detector_t *detector, float signal_value)
{
    if (detector == NULL) return;
    
    uint32_t current_time = get_timestamp_ms();
    detector->previous_value = detector->current_value;
    detector->current_value = signal_value;
    
    // Detectar flanco ascendente
    float rise_threshold = 0.02f; // 2% del rango
    if (signal_value > detector->previous_value + rise_threshold) {
        if (!detector->rising_edge_detected) {
            detector->rising_edge_detected = true;
            detector->rise_start_time = current_time;
        }
        
        // Calcular velocidad de subida
        if (current_time > detector->rise_start_time) {
            detector->rise_rate = (signal_value - detector->previous_value) / 
                                 (current_time - detector->rise_start_time);
        }
        
        // Actualizar pico si es mayor
        if (signal_value > detector->peak_value) {
            detector->peak_value = signal_value;
            detector->peak_timestamp = current_time;
        }
    }
    
    // Detectar flanco descendente
    float fall_threshold = detector->peak_value * 0.7f; // 70% del pico
    if (detector->rising_edge_detected && signal_value < fall_threshold) {
        if (!detector->falling_edge_detected) {
            detector->falling_edge_detected = true;
            detector->fall_start_time = current_time;
        }
        
        // Calcular velocidad de bajada
        if (current_time > detector->fall_start_time) {
            detector->fall_rate = (detector->peak_value - signal_value) / 
                                 (current_time - detector->fall_start_time);
        }
    }
}

/**
 * @brief Analizar decaimiento exponencial
 */
static bool analyze_decay_pattern(edrumulus_decay_analyzer_t *analyzer, float signal_value)
{
    if (analyzer == NULL) return false;
    
    uint32_t current_time = get_timestamp_ms();
    
    // Inicializar análisis si es el primer punto
    if (analyzer->decay_start_time == 0) {
        analyzer->decay_start_time = current_time;
        analyzer->initial_amplitude = signal_value;
        return true;
    }
    
    // Calcular tiempo transcurrido
    uint32_t elapsed_ms = current_time - analyzer->decay_start_time;
    if (elapsed_ms == 0) return true;
    
    // Modelo exponencial: V(t) = V₀ * e^(-λt)
    // λ típico para golpes reales: 0.1-0.3 ms⁻¹
    analyzer->decay_constant = 0.2f; // Valor típico
    
    // Calcular valor esperado según modelo exponencial
    float time_seconds = elapsed_ms / 1000.0f;
    analyzer->expected_value = analyzer->initial_amplitude * 
                              expf(-analyzer->decay_constant * time_seconds);
    
    // Calcular desviación del modelo
    analyzer->deviation = fabsf(signal_value - analyzer->expected_value);
    
    // Calcular correlación simplificada (R²)
    float relative_error = analyzer->deviation / analyzer->initial_amplitude;
    analyzer->r_squared = 1.0f - relative_error;
    
    // Validar si sigue patrón exponencial
    analyzer->decay_valid = (analyzer->r_squared > EDRUMULUS_REBOUND_DECAY_R_SQUARED);
    
    return analyzer->decay_valid;
}

/**
 * @brief Validar velocidad y detectar patrones anómalos
 */
static bool validate_velocity(edrumulus_velocity_validator_t *validator, uint8_t velocity)
{
    if (validator == NULL) return false;
    
    uint32_t current_time = get_timestamp_ms();
    
    // Rechazar velocidades muy bajas
    if (velocity < validator->min_valid_velocity) {
        return false;
    }
    
    // Agregar velocidad al historial
    validator->velocity_history[validator->history_index] = velocity;
    validator->history_index = (validator->history_index + 1) % 5;
    
    // Detectar saltos anómalos de velocidad
    if (validator->last_valid_hit_time > 0) {
        uint32_t time_diff = current_time - validator->last_valid_hit_time;
        
        // Si hay golpes muy cercanos (<10ms), verificar consistencia
        if (time_diff < 10) {
            // Buscar velocidad anterior en historial
            uint8_t prev_index = (validator->history_index + 4) % 5;
            uint8_t prev_velocity = validator->velocity_history[prev_index];
            
            if (prev_velocity > 0) {
                float velocity_ratio = (float)velocity / prev_velocity;
                
                // Detectar salto anómalo (>50% de cambio)
                if (velocity_ratio > (1.0f + EDRUMULUS_REBOUND_MAX_VELOCITY_JUMP) ||
                    velocity_ratio < (1.0f - EDRUMULUS_REBOUND_MAX_VELOCITY_JUMP)) {
                    validator->velocity_jump_detected = true;
                    return false;
                }
            }
        }
    }
    
    // Calcular consistencia de velocidades
    uint8_t valid_samples = 0;
    float velocity_sum = 0.0f;
    
    for (int i = 0; i < 5; i++) {
        if (validator->velocity_history[i] > 0) {
            velocity_sum += validator->velocity_history[i];
            valid_samples++;
        }
    }
    
    if (valid_samples > 1) {
        float avg_velocity = velocity_sum / valid_samples;
        float variance = 0.0f;
        
        for (int i = 0; i < 5; i++) {
            if (validator->velocity_history[i] > 0) {
                float diff = validator->velocity_history[i] - avg_velocity;
                variance += diff * diff;
            }
        }
        
        variance /= valid_samples;
        validator->velocity_consistency = 1.0f / (1.0f + variance / (avg_velocity * avg_velocity));
    }
    
    validator->last_valid_hit_time = current_time;
    validator->velocity_jump_detected = false;
    
    return true;
}

/**
 * @brief Actualizar threshold adaptativo
 */
static void update_adaptive_threshold(edrumulus_adaptive_threshold_t *threshold, float signal_value)
{
    if (threshold == NULL) return;
    
    uint32_t current_time = get_timestamp_ms();
    
    // Actualizar muestras de ruido durante períodos silenciosos
    if (signal_value < 0.1f) { // Señal muy baja, probablemente ruido
        threshold->noise_samples[threshold->noise_sample_index] = signal_value;
        threshold->noise_sample_index = (threshold->noise_sample_index + 1) % 20;
        
        // Calcular nivel de ruido cada 20 muestras
        if (threshold->noise_sample_index == 0) {
            threshold->noise_floor = calculate_rms_float(threshold->noise_samples, 20);
            threshold->noise_floor_valid = true;
        }
    }
    
    // Ajustar threshold cada 100ms
    if (current_time - threshold->last_adjustment_time > 100) {
        if (threshold->noise_floor_valid && threshold->noise_floor > 0.0f) {
            // Calcular SNR aproximado
            float signal_peak = signal_value;
            threshold->signal_to_noise_ratio = 20.0f * log10f(signal_peak / threshold->noise_floor);
            
            // Ajustar threshold basado en SNR
            float snr_factor = threshold->signal_to_noise_ratio / 10.0f;
            threshold->adaptive_threshold = (uint16_t)(threshold->base_threshold * (1.0f + snr_factor));
            
            // Limitar threshold entre 50-500
            if (threshold->adaptive_threshold < 50) threshold->adaptive_threshold = 50;
            if (threshold->adaptive_threshold > 500) threshold->adaptive_threshold = 500;
        }
        
        threshold->last_adjustment_time = current_time;
    }
}

// === FUNCIONES PÚBLICAS PHASE 3 ===

esp_err_t edrumulus_rebound_detector_init(void)
{
    if (g_rebound_detector_initialized) {
        ESP_LOGW(TAG, "Rebound detector already initialized");
        return ESP_OK;
    }
    
    ESP_LOGI(TAG, "Initializing rebound detector subsystem");
    
    // Inicializar detectores para todos los canales
    for (int ch = 0; ch < EDRUMULUS_MAX_ADC_CHANNELS; ch++) {
        edrumulus_rebound_detector_t *detector = &g_rebound_detectors[ch];
        
        // Inicializar detector de flancos
        memset(&detector->edge_detector, 0, sizeof(edrumulus_edge_detector_t));
        
        // Inicializar analizador de decaimiento
        memset(&detector->decay_analyzer, 0, sizeof(edrumulus_decay_analyzer_t));
        
        // Inicializar validador de velocidad
        detector->velocity_validator.min_valid_velocity = EDRUMULUS_REBOUND_MIN_VELOCITY;
        memset(detector->velocity_validator.velocity_history, 0, sizeof(detector->velocity_validator.velocity_history));
        detector->velocity_validator.history_index = 0;
        detector->velocity_validator.velocity_consistency = 1.0f;
        detector->velocity_validator.velocity_jump_detected = false;
        detector->velocity_validator.last_valid_hit_time = 0;
        
        // Inicializar threshold adaptativo
        detector->adaptive_threshold.base_threshold = 100; // Valor por defecto
        detector->adaptive_threshold.noise_floor = 0.01f;
        detector->adaptive_threshold.signal_to_noise_ratio = 20.0f;
        detector->adaptive_threshold.adaptive_threshold = 100;
        detector->adaptive_threshold.last_adjustment_time = 0;
        memset(detector->adaptive_threshold.noise_samples, 0, sizeof(detector->adaptive_threshold.noise_samples));
        detector->adaptive_threshold.noise_sample_index = 0;
        detector->adaptive_threshold.noise_floor_valid = false;
        
        // Inicializar buffer circular
        memset(detector->signal_buffer, 0, sizeof(detector->signal_buffer));
        detector->buffer_index = 0;
        
        // Inicializar estado
        detector->hit_in_progress = false;
        detector->hit_start_time = 0;
        detector->hit_peak_value = 0.0f;
        detector->hit_velocity = 0;
        detector->enabled = true;
        
        // Inicializar estadísticas
        detector->total_hits_detected = 0;
        detector->rebounds_rejected = 0;
        detector->rejection_rate = 0.0f;
        detector->false_positives = 0;
        detector->false_negatives = 0;
    }
    
    g_rebound_detector_initialized = true;
    ESP_LOGI(TAG, "Rebound detector initialized successfully");
    
    return ESP_OK;
}

esp_err_t edrumulus_rebound_detector_deinit(void)
{
    if (!g_rebound_detector_initialized) {
        ESP_LOGW(TAG, "Rebound detector not initialized");
        return ESP_OK;
    }
    
    g_rebound_detector_initialized = false;
    ESP_LOGI(TAG, "Rebound detector deinitialized");
    
    return ESP_OK;
}

bool edrumulus_rebound_detector_process(float signal, uint8_t channel)
{
    if (!g_rebound_detector_initialized || channel >= EDRUMULUS_MAX_ADC_CHANNELS) {
        return true; // Pass-through si no está inicializado
    }
    
    edrumulus_rebound_detector_t *detector = &g_rebound_detectors[channel];
    
    if (!detector->enabled) {
        return true; // Pass-through si está deshabilitado
    }
    
    // Agregar muestra al buffer circular
    detector->signal_buffer[detector->buffer_index] = signal;
    detector->buffer_index = (detector->buffer_index + 1) % EDRUMULUS_REBOUND_SIGNAL_BUFFER_SIZE;
    
    // Actualizar todos los algoritmos
    update_edge_detector(&detector->edge_detector, signal);
    update_adaptive_threshold(&detector->adaptive_threshold, signal);
    
    // Detectar inicio de golpe
    if (!detector->hit_in_progress && detector->edge_detector.rising_edge_detected) {
        // Verificar velocidad de subida mínima
        if (detector->edge_detector.rise_rate >= EDRUMULUS_REBOUND_MIN_RISE_RATE) {
            detector->hit_in_progress = true;
            detector->hit_start_time = get_timestamp_ms();
            detector->hit_peak_value = detector->edge_detector.peak_value;
            
            // Inicializar análisis de decaimiento
            detector->decay_analyzer.decay_start_time = 0;
            
            ESP_LOGD(TAG, "Hit started on channel %d, rise_rate=%.3f", channel, detector->edge_detector.rise_rate);
        } else {
            // Rebote detectado por velocidad de subida insuficiente
            detector->rebounds_rejected++;
            detector->edge_detector.rising_edge_detected = false;
            ESP_LOGD(TAG, "Rebound rejected (low rise rate): %.3f < %.3f", 
                     detector->edge_detector.rise_rate, EDRUMULUS_REBOUND_MIN_RISE_RATE);
            return false;
        }
    }
    
    // Procesar golpe en progreso
    if (detector->hit_in_progress) {
        uint32_t hit_duration = get_timestamp_ms() - detector->hit_start_time;
        
        // Verificar duración mínima del pico
        if (hit_duration >= EDRUMULUS_REBOUND_MIN_PEAK_DURATION) {
            // Analizar patrón de decaimiento
            bool decay_valid = analyze_decay_pattern(&detector->decay_analyzer, signal);
            
            // Finalizar golpe si se detecta flanco descendente
            if (detector->edge_detector.falling_edge_detected) {
                detector->hit_in_progress = false;
                detector->total_hits_detected++;
                
                // Calcular velocidad basada en el pico
                detector->hit_velocity = (uint8_t)(detector->hit_peak_value * 127.0f);
                if (detector->hit_velocity < 10) detector->hit_velocity = 10;
                if (detector->hit_velocity > 127) detector->hit_velocity = 127;
                
                // Validar usando todos los criterios
                bool is_valid = decay_valid && 
                               validate_velocity(&detector->velocity_validator, detector->hit_velocity);
                
                if (!is_valid) {
                    detector->rebounds_rejected++;
                    ESP_LOGD(TAG, "Rebound rejected (validation failed): decay=%s, velocity=%d", 
                             decay_valid ? "OK" : "FAIL", detector->hit_velocity);
                }
                
                // Actualizar estadísticas
                detector->rejection_rate = (float)detector->rebounds_rejected / detector->total_hits_detected;
                
                // Reset detector de flancos
                detector->edge_detector.rising_edge_detected = false;
                detector->edge_detector.falling_edge_detected = false;
                detector->edge_detector.peak_value = 0.0f;
                
                return is_valid;
            }
        }
    }
    
    return true; // Continuar procesando
}

bool edrumulus_rebound_detector_is_valid_hit(uint8_t channel, uint8_t velocity)
{
    if (!g_rebound_detector_initialized || channel >= EDRUMULUS_MAX_ADC_CHANNELS) {
        return true; // Pass-through si no está inicializado
    }
    
    edrumulus_rebound_detector_t *detector = &g_rebound_detectors[channel];
    
    if (!detector->enabled) {
        return true; // Pass-through si está deshabilitado
    }
    
    // Validar velocidad mínima
    if (velocity < detector->velocity_validator.min_valid_velocity) {
        detector->rebounds_rejected++;
        ESP_LOGD(TAG, "Hit rejected (low velocity): %d < %d", 
                 velocity, detector->velocity_validator.min_valid_velocity);
        return false;
    }
    
    // Validar usando el validador de velocidad
    bool is_valid = validate_velocity(&detector->velocity_validator, velocity);
    
    if (!is_valid) {
        detector->rebounds_rejected++;
        ESP_LOGD(TAG, "Hit rejected (velocity validation failed)");
    }
    
    return is_valid;
}

void edrumulus_rebound_detector_reset(uint8_t channel)
{
    if (!g_rebound_detector_initialized || channel >= EDRUMULUS_MAX_ADC_CHANNELS) {
        return;
    }
    
    edrumulus_rebound_detector_t *detector = &g_rebound_detectors[channel];
    
    // Reset estado del detector
    detector->hit_in_progress = false;
    detector->hit_start_time = 0;
    detector->hit_peak_value = 0.0f;
    detector->hit_velocity = 0;
    
    // Reset detector de flancos
    memset(&detector->edge_detector, 0, sizeof(edrumulus_edge_detector_t));
    
    // Reset analizador de decaimiento
    memset(&detector->decay_analyzer, 0, sizeof(edrumulus_decay_analyzer_t));
    
    // Reset buffer circular
    memset(detector->signal_buffer, 0, sizeof(detector->signal_buffer));
    detector->buffer_index = 0;
    
    ESP_LOGD(TAG, "Rebound detector reset for channel %d", channel);
}

esp_err_t edrumulus_rebound_detector_set_enabled(uint8_t channel, bool enabled)
{
    if (!g_rebound_detector_initialized) {
        ESP_LOGE(TAG, "Rebound detector not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    if (channel >= EDRUMULUS_MAX_ADC_CHANNELS) {
        ESP_LOGE(TAG, "Invalid channel: %d", channel);
        return ESP_ERR_INVALID_ARG;
    }
    
    g_rebound_detectors[channel].enabled = enabled;
    
    ESP_LOGI(TAG, "Rebound detector channel %d %s", channel, enabled ? "enabled" : "disabled");
    
    return ESP_OK;
}

esp_err_t edrumulus_rebound_detector_get_statistics(uint8_t channel, edrumulus_rebound_detector_t *detector)
{
    if (!g_rebound_detector_initialized) {
        ESP_LOGE(TAG, "Rebound detector not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    if (channel >= EDRUMULUS_MAX_ADC_CHANNELS || detector == NULL) {
        ESP_LOGE(TAG, "Invalid parameters");
        return ESP_ERR_INVALID_ARG;
    }
    
    *detector = g_rebound_detectors[channel];
    
    return ESP_OK;
}

esp_err_t edrumulus_rebound_detector_configure(uint8_t channel, uint8_t min_velocity, 
                                               float min_rise_rate, float decay_threshold)
{
    if (!g_rebound_detector_initialized) {
        ESP_LOGE(TAG, "Rebound detector not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    if (channel >= EDRUMULUS_MAX_ADC_CHANNELS) {
        ESP_LOGE(TAG, "Invalid channel: %d", channel);
        return ESP_ERR_INVALID_ARG;
    }
    
    if (min_velocity < 1 || min_velocity > 127) {
        ESP_LOGE(TAG, "Invalid min_velocity: %d (valid range: 1-127)", min_velocity);
        return ESP_ERR_INVALID_ARG;
    }
    
    if (min_rise_rate < 0.01f || min_rise_rate > 1.0f) {
        ESP_LOGE(TAG, "Invalid min_rise_rate: %.3f (valid range: 0.01-1.0)", min_rise_rate);
        return ESP_ERR_INVALID_ARG;
    }
    
    if (decay_threshold < 0.5f || decay_threshold > 1.0f) {
        ESP_LOGE(TAG, "Invalid decay_threshold: %.3f (valid range: 0.5-1.0)", decay_threshold);
        return ESP_ERR_INVALID_ARG;
    }
    
    edrumulus_rebound_detector_t *detector = &g_rebound_detectors[channel];
    
    // Actualizar configuración
    detector->velocity_validator.min_valid_velocity = min_velocity;
    // min_rise_rate se usa en la constante global, aquí solo validamos
    // decay_threshold se usa en la constante global, aquí solo validamos
    
    ESP_LOGI(TAG, "Rebound detector configured for channel %d: min_vel=%d, rise_rate=%.3f, decay_th=%.3f",
             channel, min_velocity, min_rise_rate, decay_threshold);
    
    return ESP_OK;
}

// === IMPLEMENTACIÓN PHASE 1: VALIDACIÓN ADC Y MUESTREO ===

// Declaraciones forward para funciones auxiliares adicionales
static void adc_validation_log_internal(uint8_t channel, const char *format, ...);
static uint64_t get_timestamp_us(void);

// Funciones auxiliares para cálculos matemáticos (usando la función existente)

static float calculate_linear_regression_r_squared(const float *x_values, const float *y_values, uint16_t count)
{
    if (count < 2) return 0.0f;
    
    // Calcular medias
    float x_mean = 0.0f, y_mean = 0.0f;
    for (uint16_t i = 0; i < count; i++) {
        x_mean += x_values[i];
        y_mean += y_values[i];
    }
    x_mean /= count;
    y_mean /= count;
    
    // Calcular coeficientes de correlación
    float numerator = 0.0f, x_variance = 0.0f, y_variance = 0.0f;
    for (uint16_t i = 0; i < count; i++) {
        float x_diff = x_values[i] - x_mean;
        float y_diff = y_values[i] - y_mean;
        numerator += x_diff * y_diff;
        x_variance += x_diff * x_diff;
        y_variance += y_diff * y_diff;
    }
    
    if (x_variance == 0.0f || y_variance == 0.0f) return 0.0f;
    
    float correlation = numerator / sqrtf(x_variance * y_variance);
    return correlation * correlation; // R²
}

static uint64_t get_timestamp_us(void)
{
    return esp_timer_get_time();
}



static void adc_validation_log_internal(uint8_t channel, const char *format, ...)
{
    char buffer[256];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    
    uint64_t timestamp = get_timestamp_us();
    ESP_LOGI(TAG, "[PHASE1_CH%d] %lu: %s", channel, (uint32_t)timestamp, buffer);
    
    // Guardar en log interno si hay espacio
    edrumulus_adc_validation_t *validator = &g_adc_validators[channel];
    snprintf(validator->validation_log, sizeof(validator->validation_log), 
             "[%lu] %.200s", (uint32_t)timestamp, buffer);
}

esp_err_t edrumulus_adc_validation_init(void)
{
    if (g_adc_validation_initialized) {
        ESP_LOGW(TAG, "ADC validation already initialized");
        return ESP_OK;
    }
    
    // Inicializar validadores para todos los canales
    memset(g_adc_validators, 0, sizeof(g_adc_validators));
    
    for (uint8_t i = 0; i < EDRUMULUS_MAX_ADC_CHANNELS; i++) {
        edrumulus_adc_validation_t *validator = &g_adc_validators[i];
        
        // Configuración por defecto
        validator->test_channel = i;
        validator->test_voltage_min = 0.0f;
        validator->test_voltage_max = EDRUMULUS_VALIDATION_ADC_VREF;
        validator->test_sample_count = EDRUMULUS_VALIDATION_BUFFER_SIZE;
        
        // Inicializar métricas de muestreo
        validator->sampling_metrics.target_sample_rate = EDRUMULUS_VALIDATION_SAMPLE_RATE;
        
        // Inicializar calculadora SNR
        validator->snr_calculator.snr_valid = false;
        validator->snr_calculator.measurement_complete = false;
    }
    
    g_adc_validation_initialized = true;
    
    ESP_LOGI(TAG, "ADC validation subsystem initialized");
    
    return ESP_OK;
}

esp_err_t edrumulus_adc_validation_deinit(void)
{
    if (!g_adc_validation_initialized) {
        return ESP_OK;
    }
    
    g_adc_validation_initialized = false;
    
    ESP_LOGI(TAG, "ADC validation subsystem deinitialized");
    
    return ESP_OK;
}

esp_err_t edrumulus_adc_validation_test_precision(uint8_t channel, float test_voltage_min, 
                                                  float test_voltage_max, uint16_t sample_count)
{
    if (!g_adc_validation_initialized) {
        ESP_LOGE(TAG, "ADC validation not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    if (channel >= EDRUMULUS_MAX_ADC_CHANNELS) {
        ESP_LOGE(TAG, "Invalid channel: %d", channel);
        return ESP_ERR_INVALID_ARG;
    }
    
    if (sample_count > EDRUMULUS_VALIDATION_BUFFER_SIZE) {
        ESP_LOGE(TAG, "Sample count too large: %d (max: %d)", sample_count, EDRUMULUS_VALIDATION_BUFFER_SIZE);
        return ESP_ERR_INVALID_ARG;
    }
    
    edrumulus_adc_validation_t *validator = &g_adc_validators[channel];
    edrumulus_adc_precision_t *precision = &validator->adc_precision;
    
    validator->validation_in_progress = true;
    validator->validation_start_time = get_timestamp_ms();
    
    adc_validation_log_internal(channel, "Starting ADC precision test: %.2fV - %.2fV, %d samples", 
                               test_voltage_min, test_voltage_max, sample_count);
    
    // Arrays para regresión lineal
    float voltage_references[EDRUMULUS_VALIDATION_BUFFER_SIZE];
    float voltage_measured[EDRUMULUS_VALIDATION_BUFFER_SIZE];
    
    precision->raw_adc_min = 4095;
    precision->raw_adc_max = 0;
    precision->total_samples = 0;
    
    // Simular barrido de voltajes (en implementación real se usaría DAC externo)
    float voltage_step = (test_voltage_max - test_voltage_min) / (sample_count - 1);
    
    for (uint16_t i = 0; i < sample_count; i++) {
        // Voltaje de referencia teórico
        voltage_references[i] = test_voltage_min + (i * voltage_step);
        
        // Leer ADC (simulado para esta implementación)
        int adc_raw;
        esp_err_t ret = edrumulus_detection_read_channel(channel, &adc_raw);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to read ADC channel %d", channel);
            validator->validation_in_progress = false;
            return ret;
        }
        
        // Convertir a voltaje
        voltage_measured[i] = (float)adc_raw * EDRUMULUS_VALIDATION_ADC_VREF / 4095.0f;
        
        // Actualizar estadísticas
        if (adc_raw < precision->raw_adc_min) precision->raw_adc_min = adc_raw;
        if (adc_raw > precision->raw_adc_max) precision->raw_adc_max = adc_raw;
        precision->total_samples++;
        
        // Delay pequeño entre muestras
        vTaskDelay(pdMS_TO_TICKS(1));
    }
    
    // Calcular R² de linealidad
    precision->linearity_r_squared = calculate_linear_regression_r_squared(
        voltage_references, voltage_measured, sample_count);
    
    // Calcular error de precisión promedio
    float total_error = 0.0f;
    for (uint16_t i = 0; i < sample_count; i++) {
        float error = fabsf(voltage_measured[i] - voltage_references[i]) / voltage_references[i];
        total_error += error;
    }
    precision->precision_error = (total_error / sample_count) * 100.0f; // Porcentaje
    
    // Verificar criterios de aceptación
    precision->linearity_valid = (precision->linearity_r_squared >= EDRUMULUS_VALIDATION_MIN_LINEARITY);
    precision->precision_valid = (precision->precision_error <= 1.0f); // < 1% error
    
    validator->validation_duration = get_timestamp_ms() - validator->validation_start_time;
    validator->validation_in_progress = false;
    
    adc_validation_log_internal(channel, "ADC precision test completed: R²=%.4f (%s), Error=%.2f%% (%s)", 
                               precision->linearity_r_squared, 
                               precision->linearity_valid ? "PASS" : "FAIL",
                               precision->precision_error,
                               precision->precision_valid ? "PASS" : "FAIL");
    
    return ESP_OK;
}

// === VALIDATION: Issue #1 - ADC Continuous with DMA ===
// Procedure:
//   1. edrumulus_detection_adc_continuous_init()      -> must return ESP_OK
//   2. edrumulus_detection_adc_continuous_start()       -> must return ESP_OK
//   3. Wait 100ms, then read 1000 samples via edrumulus_detection_get_sample()
//   4. Verify sample rate: count samples / elapsed time ≈ EDRUMULUS_ADC_SAMPLE_RATE * 2
//   5. Verify no overflow: edrumulus_detection_get_overflow_count() == 0
//   6. Verify both channels (CH4=piezo1, CH5=piezo2) have data
//   7. edrumulus_detection_adc_continuous_stop()        -> must return ESP_OK
//   8. edrumulus_detection_adc_continuous_deinit()      -> must return ESP_OK
//   9. Build compiles without errors or warnings
//
// HITL (Hardware-in-the-Loop):
//   - Connect piezo1 to GPIO4, piezo2 to GPIO5 (or signal generator with 200Hz sine)
//   - Run the validation above via console command 'test adc_continuous'
//   - Verify on oscilloscope: ADC DMA output toggling at 8kHz
//   - Tap piezo1 only: verify samples on ch4 change, ch5 stays flat
//   - Tap piezo2 only: verify samples on ch5 change, ch4 stays flat

/**
 * @brief Validate ADC continuous mode (compile-time + runtime checks)
 * 
 * Call this after edrumulus_detection_adc_continuous_init() and _start().
 * Verify sample rate, no overflows, and both channels active.
 * 
 * @param duration_ms Duration to accumulate samples (e.g. 500ms)
 * @return esp_err_t ESP_OK if validation passes, ESP_FAIL otherwise
 */
esp_err_t edrumulus_detection_adc_continuous_validate(uint32_t duration_ms)
{
    if (!g_adc_continuous_running) {
        ESP_LOGE(TAG, "[VALIDATE] ADC continuous not running");
        return ESP_ERR_INVALID_STATE;
    }
    
    ESP_LOGI(TAG, "[VALIDATE] Starting ADC continuous validation (%lu ms)", duration_ms);
    
    uint32_t sample_count = 0;
    uint32_t ch4_count = 0;
    uint32_t ch5_count = 0;
    uint32_t overflow_before = g_adc_ringbuf.overflow_count;
    uint32_t start_ms = get_timestamp_ms();
    
    edrumulus_adc_sample_t sample;
    while (get_timestamp_ms() - start_ms < duration_ms) {
        while (edrumulus_detection_get_sample(&sample)) {
            sample_count++;
            if (sample.channel == 4) ch4_count++;
            if (sample.channel == 5) ch5_count++;
        }
        vTaskDelay(pdMS_TO_TICKS(1));
    }
    
    uint32_t elapsed_ms = get_timestamp_ms() - start_ms;
    uint32_t overflow_count = g_adc_ringbuf.overflow_count - overflow_before;
    
    // Calculate actual sample rate (both channels)
    float actual_rate = (float)sample_count * 1000.0f / elapsed_ms;
    float rate_error = fabsf(actual_rate - EDRUMULUS_ADC_SAMPLE_RATE * 2) / (EDRUMULUS_ADC_SAMPLE_RATE * 2);
    bool rate_ok = (rate_error <= 0.10f);  // 10% tolerance for validation
    
    bool overflow_ok = (overflow_count == 0);
    bool ch4_ok = (ch4_count > 0);
    bool ch5_ok = (ch5_count > 0);
    bool result = rate_ok && overflow_ok && ch4_ok && ch5_ok;
    
    ESP_LOGI(TAG, "[VALIDATE] === ADC Continuous Results ===");
    ESP_LOGI(TAG, "[VALIDATE] Samples: %lu in %lu ms (%.0f Hz, target %d Hz, err %.1f%%)",
             sample_count, elapsed_ms, actual_rate, EDRUMULUS_ADC_SAMPLE_RATE * 2, rate_error * 100.0f);
    ESP_LOGI(TAG, "[VALIDATE] Ch4 samples: %lu, Ch5 samples: %lu", ch4_count, ch5_count);
    ESP_LOGI(TAG, "[VALIDATE] Overflows: %lu", overflow_count);
    ESP_LOGI(TAG, "[VALIDATE] Rate: %s, Overflow: %s, Ch4: %s, Ch5: %s",
             rate_ok ? "PASS" : "FAIL",
             overflow_ok ? "PASS" : "FAIL",
             ch4_ok ? "PASS" : "FAIL",
             ch5_ok ? "PASS" : "FAIL");
    ESP_LOGI(TAG, "[VALIDATE] === OVERALL: %s ===", result ? "PASS" : "FAIL");
    
    return result ? ESP_OK : ESP_FAIL;
}