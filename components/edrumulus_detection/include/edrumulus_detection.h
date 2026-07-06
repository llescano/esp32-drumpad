/**
 * @file edrumulus_detection.h
 * @brief ESP32 E-Drum Trigger System - Detection Component
 * 
 * Signal processing and drum hit detection algorithms
 */

#ifndef EDRUMULUS_DETECTION_H
#define EDRUMULUS_DETECTION_H

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "edrumulus_config.h"

#ifdef __cplusplus
extern "C" {
#endif

// ADC Configuration for ESP32-S3 (continuous/DMA mode)
#define EDRUMULUS_MAX_ADC_CHANNELS      6
#define EDRUMULUS_ADC_SAMPLE_RATE       8000    // Target sample rate (Hz)
#define EDRUMULUS_ADC_RESOLUTION        12      // 12-bit
#define EDRUMULUS_ADC_CONT_FRAME_SIZE   256     // Samples per DMA frame
#define EDRUMULUS_ADC_RINGBUF_SIZE      512     // Ring buffer entries (2x frame)

// ADC channel mapping for dual-piezo pad
#define EDRUMULUS_ADC_CH_PIEZO1         ADC_CHANNEL_4   // GPIO4
#define EDRUMULUS_ADC_CH_PIEZO2         ADC_CHANNEL_5   // GPIO5

/**
 * @brief ADC sample structure (one per channel per conversion)
 */
typedef struct {
    uint8_t  channel;       ///< ADC channel (0-5)
    uint16_t raw_value;     ///< Raw 12-bit ADC value (0-4095)
    uint32_t timestamp_us;  ///< Timestamp in microseconds
} edrumulus_adc_sample_t;

/**
 * @brief Ring buffer for ADC continuous samples (lock-free, single producer/consumer)
 */
typedef struct {
    edrumulus_adc_sample_t buffer[EDRUMULUS_ADC_RINGBUF_SIZE];  ///< Circular buffer
    volatile uint32_t head;     ///< Write index (producer, ADC DMA callback)
    volatile uint32_t tail;     ///< Read index (consumer, DSP task)
    uint32_t overflow_count;    ///< Lost samples due to overflow
    bool initialized;           ///< Buffer ready
} edrumulus_adc_ringbuf_t;

/**
 * @brief Detection configuration structure
 */
typedef struct {
    uint32_t sample_rate_hz;         ///< ADC sampling rate
    uint8_t num_channels;            ///< Number of active channels
    QueueHandle_t event_queue;       ///< Queue for detection events
    bool enable_crosstalk_cancel;    ///< Enable crosstalk cancellation
} edrumulus_detection_config_t;

/**
 * @brief Drum hit event structure
 */
typedef struct {
    uint8_t channel;                 ///< ADC channel
    uint8_t velocity;                ///< Hit velocity (0-127)
    uint8_t note;                    ///< MIDI note number
    uint32_t timestamp;              ///< Hit timestamp
    bool is_rimshot;                 ///< Rimshot detection
} edrumulus_hit_event_t;

// edrumulus_pad_config_t is defined in edrumulus_config.h

/**
 * @brief Mask time configuration structure for intelligent retrigger prevention
 * 
 * Esta estructura reemplaza el delay fijo de 500ms con algoritmos inteligentes
 * de mask time que se adaptan según la velocidad del golpe detectado.
 */
typedef struct {
    uint32_t mask_time_ms;           ///< Tiempo de máscara base en milisegundos (2-10ms típico)
    bool adaptive_mask;              ///< Habilitar máscara adaptativa basada en velocidad
    uint8_t velocity_threshold_low;  ///< Umbral bajo de velocidad para mask time corto
    uint8_t velocity_threshold_high; ///< Umbral alto de velocidad para mask time largo
} edrumulus_mask_time_config_t;

/**
 * @brief Initialize detection subsystem
 * 
 * @param config Detection configuration
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
esp_err_t edrumulus_detection_init(const edrumulus_detection_config_t *config);

/**
 * @brief Read ADC channel value (one-shot, legacy compatibility)
 * 
 * When continuous mode is active, reads latest value from ring buffer.
 * Falls back to one-shot read if continuous mode is not running.
 * 
 * @param channel ADC channel (0-5)
 * @param value Pointer to store the read value
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
esp_err_t edrumulus_detection_read_channel(uint8_t channel, int *value);

/**
 * @brief Initialize ADC continuous mode with DMA
 * 
 * Configures adc_continuous driver with dual-channel pattern (piezo1, piezo2)
 * and DMA frame size for 8kHz sampling. Sets up ring buffer for Core 0 -> Core 1
 * communication.
 * 
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
esp_err_t edrumulus_detection_adc_continuous_init(void);

/**
 * @brief Start ADC continuous conversion with DMA
 * 
 * Starts the continuous ADC conversion. Samples arrive in ring buffer.
 * Can be called after edrumulus_detection_adc_continuous_init().
 * 
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
esp_err_t edrumulus_detection_adc_continuous_start(void);

/**
 * @brief Stop ADC continuous conversion
 * 
 * Stops continuous ADC conversion. Ring buffer retains last samples.
 * 
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
esp_err_t edrumulus_detection_adc_continuous_stop(void);

/**
 * @brief Deinitialize ADC continuous mode
 * 
 * Stops conversion and frees ADC continuous handle.
 * 
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
esp_err_t edrumulus_detection_adc_continuous_deinit(void);

/**
 * @brief Pop next sample from ADC ring buffer (non-blocking)
 * 
 * Called from DSP task (Core 1) to consume ADC samples.
 * 
 * @param[out] sample Pointer to store the sample
 * @return true if sample available, false if buffer empty
 */
bool edrumulus_detection_get_sample(edrumulus_adc_sample_t *sample);

/**
 * @brief Get ring buffer fill level
 * 
 * @param[out] count Pointer to store number of samples in buffer
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
esp_err_t edrumulus_detection_get_buffer_level(uint32_t *count);

/**
 * @brief Get total overflow count (lost samples)
 * 
 * @return uint32_t Number of samples lost due to buffer overflow
 */
uint32_t edrumulus_detection_get_overflow_count(void);

/**
 * @brief Configure pad settings
 * 
 * @param pad_id Pad ID (0-7)
 * @param config Pad configuration
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
esp_err_t edrumulus_detection_configure_pad(uint8_t pad_id, const edrumulus_pad_config_t *config);

/**
 * @brief Start detection processing
 * 
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
esp_err_t edrumulus_detection_start(void);

/**
 * @brief Stop detection processing
 * 
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
esp_err_t edrumulus_detection_stop(void);

/**
 * @brief Deinitialize detection subsystem
 * 
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
esp_err_t edrumulus_detection_deinit(void);

/**
 * @brief Configure mask time settings for intelligent retrigger prevention
 * 
 * @param config Configuración de mask time que reemplaza el delay fijo de 500ms
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
esp_err_t edrumulus_detection_set_mask_time_config(const edrumulus_mask_time_config_t *config);

/**
 * @brief Get current mask time configuration
 * 
 * @param config Pointer to store current mask time configuration
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
esp_err_t edrumulus_detection_get_mask_time_config(edrumulus_mask_time_config_t *config);

/**
 * @brief Check if a channel is currently in mask period (retrigger prevention)
 * 
 * @param channel ADC channel to check
 * @return bool true if in mask period, false if ready for new detection
 */
bool edrumulus_detection_is_in_mask_period(uint8_t channel);

// === FILTRO DE BANDA PASANTE ===
// Estructuras y constantes para filtrado digital IIR (40-400 Hz)

// Parámetros de diseño del filtro
#define EDRUMULUS_FILTER_SAMPLE_RATE    1000    // Hz (1ms sampling)
#define EDRUMULUS_FILTER_LOW_CUTOFF     40      // Hz - elimina ruido DC y vibraciones lentas
#define EDRUMULUS_FILTER_HIGH_CUTOFF    400     // Hz - elimina armónicos altos y ruido
#define EDRUMULUS_FILTER_ORDER          4       // 2 biquads en cascada
#define EDRUMULUS_FILTER_Q              0.707f  // Factor de calidad Butterworth

/**
 * @brief Tipos de filtro disponibles
 */
typedef enum {
    EDRUMULUS_FILTER_LOW_PASS,
    EDRUMULUS_FILTER_HIGH_PASS,
    EDRUMULUS_FILTER_BAND_PASS
} edrumulus_filter_type_t;

/**
 * @brief Coeficientes de filtro biquad (2do orden)
 */
typedef struct {
    float b0, b1, b2;  ///< Coeficientes del numerador
    float a1, a2;      ///< Coeficientes del denominador (a0 normalizado a 1)
} edrumulus_biquad_coeffs_t;

/**
 * @brief Estado del filtro biquad (muestras anteriores)
 */
typedef struct {
    float x1, x2;      ///< Muestras de entrada anteriores
    float y1, y2;      ///< Muestras de salida anteriores
} edrumulus_biquad_state_t;

/**
 * @brief Configuración completa del filtro de banda pasante
 */
typedef struct {
    edrumulus_biquad_coeffs_t high_pass;  ///< Filtro pasa altos (40 Hz)
    edrumulus_biquad_coeffs_t low_pass;   ///< Filtro pasa bajos (400 Hz)
    edrumulus_biquad_state_t hp_state;    ///< Estado del filtro pasa altos
    edrumulus_biquad_state_t lp_state;    ///< Estado del filtro pasa bajos
    bool enabled;                         ///< Habilitación del filtro
    float gain_compensation;              ///< Compensación de ganancia
    uint32_t sample_count;                ///< Contador de muestras procesadas
} edrumulus_bandpass_filter_t;

/**
 * @brief Configuración del filtro (para NVS y ajustes dinámicos)
 */
typedef struct {
    bool filter_enabled;        ///< Habilitar/deshabilitar filtro
    float low_cutoff_hz;        ///< Frecuencia de corte baja (Hz)
    float high_cutoff_hz;       ///< Frecuencia de corte alta (Hz)
    float gain_compensation;    ///< Factor de compensación de ganancia
    uint8_t filter_order;       ///< Orden del filtro (2 o 4)
} edrumulus_filter_config_t;

// === PHASE 1: VALIDACIÓN ADC Y MUESTREO ===
// Estructuras para validación y calibración del sistema de adquisición

// Parámetros de validación Phase 1
#define EDRUMULUS_VALIDATION_ADC_RESOLUTION     12      // bits
#define EDRUMULUS_VALIDATION_ADC_VREF           3.3f    // V
#define EDRUMULUS_VALIDATION_SAMPLE_RATE        8000    // Hz
#define EDRUMULUS_VALIDATION_MIN_SNR            60.0f   // dB
#define EDRUMULUS_VALIDATION_MAX_RATE_DEVIATION 0.001f  // 0.1%
#define EDRUMULUS_VALIDATION_MIN_LINEARITY      0.99f   // R²
#define EDRUMULUS_VALIDATION_BUFFER_SIZE        1000    // muestras para análisis

/**
 * @brief Métricas de precisión del ADC
 */
typedef struct {
    float voltage_reference;        ///< Voltaje de referencia (3.3V)
    float measured_voltage;         ///< Voltaje medido por ADC
    float precision_error;          ///< Error de precisión (%)
    float linearity_r_squared;      ///< Coeficiente de linealidad R²
    uint16_t raw_adc_min;          ///< Valor ADC mínimo medido
    uint16_t raw_adc_max;          ///< Valor ADC máximo medido
    uint32_t total_samples;        ///< Total de muestras analizadas
    bool precision_valid;          ///< Precisión dentro de especificaciones
    bool linearity_valid;          ///< Linealidad dentro de especificaciones
} edrumulus_adc_precision_t;

/**
 * @brief Métricas de frecuencia de muestreo
 */
typedef struct {
    uint32_t target_sample_rate;    ///< Frecuencia objetivo (8000 Hz)
    uint32_t measured_sample_rate;  ///< Frecuencia medida
    float rate_deviation;           ///< Desviación de frecuencia (%)
    uint64_t last_sample_time;      ///< Timestamp última muestra (μs)
    uint64_t sample_interval;       ///< Intervalo entre muestras (μs)
    uint32_t sample_count;          ///< Contador de muestras
    uint64_t measurement_start_time; ///< Inicio de medición (μs)
    bool rate_valid;                ///< Frecuencia dentro de especificaciones
} edrumulus_sampling_metrics_t;

/**
 * @brief Calculadora de SNR (Signal-to-Noise Ratio)
 */
typedef struct {
    float signal_rms;               ///< RMS de la señal
    float noise_rms;                ///< RMS del ruido
    float snr_db;                   ///< SNR en decibelios
    float signal_samples[EDRUMULUS_VALIDATION_BUFFER_SIZE]; ///< Buffer de señal
    float noise_samples[EDRUMULUS_VALIDATION_BUFFER_SIZE];  ///< Buffer de ruido
    uint16_t signal_sample_count;   ///< Contador de muestras de señal
    uint16_t noise_sample_count;    ///< Contador de muestras de ruido
    bool snr_valid;                 ///< SNR dentro de especificaciones
    bool measurement_complete;      ///< Medición completada
} edrumulus_snr_calculator_t;

/**
 * @brief Validador completo ADC y muestreo (Phase 1)
 */
typedef struct {
    edrumulus_adc_precision_t adc_precision;        ///< Métricas de precisión ADC
    edrumulus_sampling_metrics_t sampling_metrics;  ///< Métricas de muestreo
    edrumulus_snr_calculator_t snr_calculator;      ///< Calculadora de SNR
    
    // Estado de validación
    bool validation_enabled;        ///< Validación habilitada
    bool validation_in_progress;    ///< Validación en progreso
    bool validation_passed;         ///< Validación exitosa
    uint32_t validation_start_time; ///< Inicio de validación (ms)
    uint32_t validation_duration;   ///< Duración de validación (ms)
    
    // Configuración de test
    uint8_t test_channel;           ///< Canal ADC bajo test
    float test_voltage_min;         ///< Voltaje mínimo de test
    float test_voltage_max;         ///< Voltaje máximo de test
    uint16_t test_sample_count;     ///< Número de muestras de test
    
    // Resultados de validación
    char validation_log[256];       ///< Log de validación
    uint32_t validation_timestamp;  ///< Timestamp de validación
} edrumulus_adc_validation_t;

// === PHASE 2: VALIDACIÓN FILTRO DE BANDA PASANTE ===
// Estructuras para validación y calibración del filtro IIR 40-400Hz

// Parámetros de validación Phase 2
#define EDRUMULUS_FILTER_VALIDATION_MIN_FREQ        10      // Hz - frecuencia mínima de test
#define EDRUMULUS_FILTER_VALIDATION_MAX_FREQ        1000    // Hz - frecuencia máxima de test
#define EDRUMULUS_FILTER_VALIDATION_FREQ_STEPS      10      // Hz - pasos de frecuencia
#define EDRUMULUS_FILTER_VALIDATION_PASSBAND_RIPPLE 3.0f    // dB - ripple máximo en banda pasante
#define EDRUMULUS_FILTER_VALIDATION_STOPBAND_ATTEN  20.0f   // dB - atenuación mínima en banda rechazada
#define EDRUMULUS_FILTER_VALIDATION_TEST_DURATION   1000    // ms - duración de cada test
#define EDRUMULUS_FILTER_VALIDATION_AMPLITUDE       1.0f    // V - amplitud de señal de test
#define EDRUMULUS_FILTER_VALIDATION_SAMPLES         100     // muestras por frecuencia

/**
 * @brief Punto de respuesta en frecuencia
 */
typedef struct {
    float frequency_hz;             ///< Frecuencia de test (Hz)
    float input_amplitude;          ///< Amplitud de entrada (V)
    float output_amplitude;         ///< Amplitud de salida (V)
    float magnitude_db;             ///< Magnitud en dB (20*log10(out/in))
    float phase_degrees;            ///< Fase en grados
    bool measurement_valid;         ///< Medición válida
    uint32_t measurement_time;      ///< Timestamp de medición (ms)
} edrumulus_frequency_point_t;

/**
 * @brief Respuesta completa en frecuencia del filtro
 */
typedef struct {
    edrumulus_frequency_point_t points[100];  ///< Puntos de respuesta (10Hz-1kHz)
    uint16_t num_points;            ///< Número de puntos medidos
    float passband_ripple;          ///< Ripple en banda pasante (dB)
    float stopband_attenuation;     ///< Atenuación en banda rechazada (dB)
    float passband_min_freq;        ///< Frecuencia mínima banda pasante (Hz)
    float passband_max_freq;        ///< Frecuencia máxima banda pasante (Hz)
    bool passband_valid;            ///< Banda pasante cumple especificaciones
    bool stopband_valid;            ///< Banda rechazada cumple especificaciones
    uint32_t measurement_start_time; ///< Inicio de medición (ms)
    uint32_t measurement_duration;   ///< Duración total de medición (ms)
} edrumulus_frequency_response_t;

/**
 * @brief Generador de señales sintéticas para test
 */
typedef struct {
    float frequency_hz;             ///< Frecuencia de generación (Hz)
    float amplitude;                ///< Amplitud de señal (0.0-1.0)
    float phase_offset;             ///< Offset de fase (radianes)
    float noise_level;              ///< Nivel de ruido añadido (0.0-1.0)
    uint32_t sample_count;          ///< Contador de muestras generadas
    float current_time;             ///< Tiempo actual de generación (s)
    bool enabled;                   ///< Generador habilitado
} edrumulus_signal_generator_t;

/**
 * @brief Configuración de test del filtro
 */
typedef struct {
    float test_frequency_start;     ///< Frecuencia inicial de test (Hz)
    float test_frequency_end;       ///< Frecuencia final de test (Hz)
    float test_frequency_step;      ///< Paso de frecuencia (Hz)
    float test_amplitude;           ///< Amplitud de test (V)
    uint16_t samples_per_frequency; ///< Muestras por frecuencia
    uint32_t settling_time_ms;      ///< Tiempo de estabilización (ms)
    bool include_noise;             ///< Incluir ruido en test
    float noise_level;              ///< Nivel de ruido (0.0-1.0)
    bool auto_calibration;          ///< Calibración automática habilitada
} edrumulus_filter_test_config_t;

/**
 * @brief Calibrador automático del filtro
 */
typedef struct {
    edrumulus_filter_config_t target_config;    ///< Configuración objetivo
    edrumulus_filter_config_t current_config;   ///< Configuración actual
    edrumulus_frequency_response_t response;    ///< Respuesta medida
    
    // Parámetros de optimización
    float optimization_tolerance;   ///< Tolerancia de optimización
    uint8_t max_iterations;         ///< Máximo número de iteraciones
    uint8_t current_iteration;      ///< Iteración actual
    bool calibration_converged;     ///< Calibración convergió
    
    // Métricas de calidad
    float passband_error;           ///< Error en banda pasante (dB)
    float stopband_error;           ///< Error en banda rechazada (dB)
    float total_error;              ///< Error total del filtro
    
    // Estado de calibración
    bool calibration_in_progress;   ///< Calibración en progreso
    bool calibration_successful;    ///< Calibración exitosa
    uint32_t calibration_start_time; ///< Inicio de calibración (ms)
    uint32_t calibration_duration;   ///< Duración de calibración (ms)
} edrumulus_filter_calibrator_t;

/**
 * @brief Validador completo del filtro (Phase 2)
 */
typedef struct {
    edrumulus_frequency_response_t frequency_response;  ///< Respuesta en frecuencia
    edrumulus_signal_generator_t signal_generator;      ///< Generador de señales
    edrumulus_filter_test_config_t test_config;         ///< Configuración de test
    edrumulus_filter_calibrator_t calibrator;           ///< Calibrador automático
    
    // Estado de validación
    bool validation_enabled;        ///< Validación habilitada
    bool validation_in_progress;    ///< Validación en progreso
    bool validation_passed;         ///< Validación exitosa
    uint32_t validation_start_time; ///< Inicio de validación (ms)
    uint32_t validation_duration;   ///< Duración de validación (ms)
    
    // Configuración de test
    uint8_t test_channel;           ///< Canal ADC bajo test
    float test_signal_amplitude;    ///< Amplitud de señal de test
    uint16_t test_frequency_count;  ///< Número de frecuencias de test
    
    // Resultados de validación
    char validation_log[512];       ///< Log de validación extendido
    uint32_t validation_timestamp;  ///< Timestamp de validación
    
    // Métricas de aceptación
    bool passband_criteria_met;     ///< Criterios banda pasante cumplidos
    bool stopband_criteria_met;     ///< Criterios banda rechazada cumplidos
    bool stability_criteria_met;    ///< Criterios estabilidad cumplidos
    bool overall_validation_passed; ///< Validación general exitosa
} edrumulus_filter_validation_t;

// === PHASE 3: VALIDACIÓN ALGORITMOS AVANZADOS DE DETECCIÓN ===
// Estructuras para validación y calibración de algoritmos Phase 3

// Parámetros de validación Phase 3
#define EDRUMULUS_PHASE3_VALIDATION_MIN_SENSITIVITY     0.95f   // 95% mínimo sensitivity
#define EDRUMULUS_PHASE3_VALIDATION_MIN_SPECIFICITY     0.90f   // 90% mínimo specificity
#define EDRUMULUS_PHASE3_VALIDATION_MIN_R_SQUARED       0.85f   // R² mínimo para decay
#define EDRUMULUS_PHASE3_VALIDATION_MAX_LATENCY_MS      10      // Latencia máxima (ms)
#define EDRUMULUS_PHASE3_VALIDATION_MIN_VELOCITY_ACCURACY 0.90f // Precisión velocidad
#define EDRUMULUS_PHASE3_VALIDATION_TEST_SAMPLES        100     // Muestras por test
#define EDRUMULUS_PHASE3_VALIDATION_SYNTHETIC_DURATION  500     // ms duración señal sintética

/**
 * @brief Métricas de rendimiento del edge detector
 */
typedef struct {
    uint32_t true_positives;        ///< Golpes reales detectados correctamente
    uint32_t false_positives;       ///< Rebotes detectados como golpes
    uint32_t true_negatives;        ///< Rebotes rechazados correctamente
    uint32_t false_negatives;       ///< Golpes reales perdidos
    float sensitivity;              ///< TP/(TP+FN) - capacidad detectar golpes
    float specificity;              ///< TN/(TN+FP) - capacidad rechazar rebotes
    float precision;                ///< TP/(TP+FP) - precisión detecciones
    float f1_score;                 ///< 2*(precision*sensitivity)/(precision+sensitivity)
    float rise_rate_threshold;      ///< Threshold rise rate actual (V/ms)
    float fall_rate_threshold;      ///< Threshold fall rate actual (V/ms)
    bool criteria_met;              ///< Criterios de aceptación cumplidos
    uint32_t test_duration_ms;      ///< Duración del test (ms)
    uint32_t validation_timestamp;  ///< Timestamp de validación
} edrumulus_edge_detector_validation_t;

/**
 * @brief Métricas de validación del decay analyzer
 */
typedef struct {
    float tau_measured;             ///< Constante tau medida (ms)
    float tau_expected;             ///< Constante tau esperada (ms)
    float tau_error;                ///< Error en tau (% desviación)
    float amplitude_measured;       ///< Amplitud inicial medida (V)
    float amplitude_expected;       ///< Amplitud inicial esperada (V)
    float amplitude_error;          ///< Error en amplitud (% desviación)
    float r_squared;                ///< Coeficiente correlación R²
    float goodness_of_fit;          ///< Calidad del ajuste exponencial
    uint16_t valid_decay_count;     ///< Número de decaimientos válidos
    uint16_t invalid_decay_count;   ///< Número de decaimientos inválidos
    float decay_validation_rate;    ///< Tasa de validación (0.0-1.0)
    bool criteria_met;              ///< Criterios de aceptación cumplidos
    uint32_t test_duration_ms;      ///< Duración del test (ms)
    uint32_t validation_timestamp;  ///< Timestamp de validación
} edrumulus_decay_analyzer_validation_t;

/**
 * @brief Métricas de validación del velocity validator
 */
typedef struct {
    float velocity_linearity;       ///< Linealidad mapeo velocidad (R²)
    uint8_t min_velocity_threshold; ///< Threshold mínimo velocidad
    uint8_t max_velocity_range;     ///< Rango máximo velocidad (127)
    float velocity_accuracy;        ///< Precisión velocidad vs referencia
    float velocity_repeatability;   ///< Repetibilidad (σ < 5%)
    uint16_t valid_velocity_count;  ///< Velocidades válidas detectadas
    uint16_t invalid_velocity_count; ///< Velocidades inválidas rechazadas
    float velocity_rejection_rate;  ///< Tasa de rechazo velocidades bajas
    float velocity_consistency;     ///< Consistencia entre golpes similares
    bool criteria_met;              ///< Criterios de aceptación cumplidos
    uint32_t test_duration_ms;      ///< Duración del test (ms)
    uint32_t validation_timestamp;  ///< Timestamp de validación
} edrumulus_velocity_validator_validation_t;

/**
 * @brief Métricas de validación del adaptive threshold
 */
typedef struct {
    float target_snr_db;            ///< SNR objetivo (20dB mínimo)
    float measured_snr_db;          ///< SNR medido actual
    float snr_error;                ///< Error en SNR (dB)
    uint16_t base_threshold;        ///< Threshold base configurado
    uint16_t adaptive_threshold;    ///< Threshold adaptativo calculado
    float adaptation_time_ms;       ///< Tiempo de adaptación (ms)
    float threshold_stability;      ///< Estabilidad threshold (±10%)
    uint32_t adaptation_count;      ///< Número de adaptaciones
    float noise_floor_rms;          ///< Nivel de ruido medido (RMS)
    bool snr_criteria_met;          ///< Criterios SNR cumplidos
    bool adaptation_criteria_met;   ///< Criterios adaptación cumplidos
    bool stability_criteria_met;    ///< Criterios estabilidad cumplidos
    uint32_t test_duration_ms;      ///< Duración del test (ms)
    uint32_t validation_timestamp;  ///< Timestamp de validación
} edrumulus_adaptive_threshold_validation_t;

/**
 * @brief Generador de datos sintéticos para validación
 */
typedef struct {
    // Parámetros del modelo de golpe real
    float hit_amplitude;            ///< Amplitud del golpe (V)
    float hit_frequency;            ///< Frecuencia resonante (Hz)
    float hit_decay_time;           ///< Tiempo de decaimiento (ms)
    float hit_rise_time;            ///< Tiempo de subida (ms)
    
    // Parámetros del modelo de rebote mecánico
    uint8_t bounce_count;           ///< Número de rebotes
    float bounce_interval;          ///< Intervalo entre rebotes (ms)
    float bounce_decay_factor;      ///< Factor de decaimiento rebotes
    
    // Parámetros de ruido ambiente
    float noise_level;              ///< Nivel de ruido (0.0-1.0)
    float power_hum_amplitude;      ///< Amplitud ruido 50Hz
    float rf_interference_level;    ///< Nivel interferencia RF
    
    // Estado del generador
    float current_time;             ///< Tiempo actual generación (s)
    uint32_t sample_count;          ///< Contador de muestras generadas
    bool generation_active;         ///< Generación activa
    
    // Buffer de señal generada
    float signal_buffer[EDRUMULUS_PHASE3_VALIDATION_TEST_SAMPLES];
    uint16_t buffer_index;          ///< Índice actual en buffer
    uint16_t buffer_size;           ///< Tamaño actual del buffer
} edrumulus_synthetic_data_generator_t;

/**
 * @brief Métricas de rendimiento del sistema completo
 */
typedef struct {
    // Métricas de latencia
    uint64_t physical_hit_time;     ///< Tiempo golpe físico (μs)
    uint64_t adc_sample_time;       ///< Tiempo primera muestra ADC (μs)
    uint64_t detection_time;        ///< Tiempo decisión algoritmo (μs)
    uint64_t midi_output_time;      ///< Tiempo envío MIDI (μs)
    uint32_t total_latency_us;      ///< Latencia total (μs)
    uint32_t adc_latency_us;        ///< Latencia ADC (μs)
    uint32_t processing_latency_us; ///< Latencia procesamiento (μs)
    uint32_t output_latency_us;     ///< Latencia salida (μs)
    
    // Métricas de detección global
    uint32_t total_hits_detected;   ///< Total hits detectados
    uint32_t total_rebounds_rejected; ///< Total rebotes rechazados
    uint32_t total_false_positives; ///< Total falsos positivos
    uint32_t total_false_negatives; ///< Total falsos negativos
    float overall_sensitivity;      ///< Sensitivity global del sistema
    float overall_specificity;      ///< Specificity global del sistema
    float overall_accuracy;         ///< Precisión global del sistema
    
    // Métricas de estabilidad
    float temperature_drift;        ///< Drift por temperatura (% por °C)
    float voltage_stability;        ///< Estabilidad voltaje (% variación)
    float long_term_drift;          ///< Drift largo plazo (% por hora)
    
    // Estado de validación
    bool latency_criteria_met;      ///< Criterios latencia cumplidos
    bool detection_criteria_met;    ///< Criterios detección cumplidos
    bool stability_criteria_met;    ///< Criterios estabilidad cumplidos
    bool overall_validation_passed; ///< Validación general exitosa
    uint32_t validation_timestamp;  ///< Timestamp de validación
} edrumulus_performance_metrics_t;

/**
 * @brief Validador completo Phase 3 (algoritmos avanzados)
 */
typedef struct {
    edrumulus_edge_detector_validation_t edge_detector;         ///< Validación edge detector
    edrumulus_decay_analyzer_validation_t decay_analyzer;       ///< Validación decay analyzer
    edrumulus_velocity_validator_validation_t velocity_validator; ///< Validación velocity validator
    edrumulus_adaptive_threshold_validation_t adaptive_threshold; ///< Validación adaptive threshold
    edrumulus_synthetic_data_generator_t synthetic_generator;   ///< Generador datos sintéticos
    edrumulus_performance_metrics_t performance_metrics;       ///< Métricas rendimiento sistema
    
    // Estado de validación Phase 3
    bool validation_enabled;        ///< Validación habilitada
    bool validation_in_progress;    ///< Validación en progreso
    bool validation_passed;         ///< Validación exitosa
    uint32_t validation_start_time; ///< Inicio de validación (ms)
    uint32_t validation_duration;   ///< Duración de validación (ms)
    
    // Configuración de test
    uint8_t test_channel;           ///< Canal ADC bajo test
    uint16_t test_sample_count;     ///< Número de muestras de test
    uint32_t test_duration_ms;      ///< Duración total de test
    
    // Resultados de validación
    char validation_log[1024];      ///< Log de validación extendido
    uint32_t validation_timestamp;  ///< Timestamp de validación
    
    // Criterios de aceptación Phase 3
    bool edge_detector_criteria_met;    ///< Edge detector cumple criterios
    bool decay_analyzer_criteria_met;   ///< Decay analyzer cumple criterios
    bool velocity_validator_criteria_met; ///< Velocity validator cumple criterios
    bool adaptive_threshold_criteria_met; ///< Adaptive threshold cumple criterios
    bool performance_criteria_met;      ///< Performance cumple criterios
    bool overall_phase3_validation_passed; ///< Validación Phase 3 exitosa
} edrumulus_phase3_validation_t;

// === PHASE 3: DETECCIÓN AVANZADA DE PICOS Y CANCELACIÓN DE REBOTES ===
// Estructuras para eliminar rebotes mecánicos del piezo

// Parámetros de configuración para Phase 3
#define EDRUMULUS_REBOUND_MIN_RISE_RATE     0.05f   // V/ms - velocidad mínima de subida
#define EDRUMULUS_REBOUND_MIN_PEAK_DURATION 2       // ms - duración mínima del pico
#define EDRUMULUS_REBOUND_MIN_VELOCITY      15      // MIDI velocity mínima válida
#define EDRUMULUS_REBOUND_DECAY_R_SQUARED   0.85f   // Correlación mínima para decaimiento
#define EDRUMULUS_REBOUND_MAX_VELOCITY_JUMP 0.5f    // 50% máximo salto de velocidad
#define EDRUMULUS_REBOUND_SIGNAL_BUFFER_SIZE 50     // 50ms de historia a 1kHz

/**
 * @brief Detector de flancos ascendentes/descendentes
 */
typedef struct {
    float current_value;        ///< Valor actual de la señal
    float previous_value;       ///< Valor anterior de la señal
    float peak_value;           ///< Valor máximo detectado
    uint32_t peak_timestamp;    ///< Timestamp del pico (ms)
    bool rising_edge_detected;  ///< Flanco ascendente detectado
    bool falling_edge_detected; ///< Flanco descendente detectado
    float rise_rate;            ///< Velocidad de subida (V/ms)
    float fall_rate;            ///< Velocidad de bajada (V/ms)
    uint32_t rise_start_time;   ///< Inicio del flanco ascendente
    uint32_t fall_start_time;   ///< Inicio del flanco descendente
} edrumulus_edge_detector_t;

/**
 * @brief Analizador de decaimiento exponencial
 */
typedef struct {
    float decay_constant;       ///< Constante de decaimiento (λ)
    float initial_amplitude;    ///< Amplitud inicial del golpe
    uint32_t decay_start_time;  ///< Inicio del análisis de decaimiento
    float expected_value;       ///< Valor esperado según modelo exponencial
    float deviation;            ///< Desviación del modelo ideal
    float r_squared;            ///< Coeficiente de correlación R²
    bool decay_valid;           ///< Decaimiento válido (sigue modelo exponencial)
} edrumulus_decay_analyzer_t;

/**
 * @brief Validador de velocidad inteligente
 */
typedef struct {
    uint8_t min_valid_velocity;     ///< Velocidad mínima válida (15)
    uint8_t velocity_history[5];    ///< Historial de velocidades recientes
    uint8_t history_index;          ///< Índice actual en el historial
    float velocity_consistency;     ///< Consistencia de velocidades (0.0-1.0)
    bool velocity_jump_detected;    ///< Salto anómalo de velocidad detectado
    uint32_t last_valid_hit_time;   ///< Timestamp del último golpe válido
} edrumulus_velocity_validator_t;

/**
 * @brief Threshold adaptativo basado en SNR
 */
typedef struct {
    uint16_t base_threshold;        ///< Threshold base configurado
    float noise_floor;              ///< Nivel de ruido medido (RMS)
    float signal_to_noise_ratio;    ///< SNR actual calculado
    uint16_t adaptive_threshold;    ///< Threshold calculado dinámicamente
    uint32_t last_adjustment_time;  ///< Última vez que se ajustó
    float noise_samples[20];        ///< Muestras para cálculo de ruido
    uint8_t noise_sample_index;     ///< Índice en buffer de ruido
    bool noise_floor_valid;         ///< Nivel de ruido válido calculado
} edrumulus_adaptive_threshold_t;

/**
 * @brief Detector principal de rebotes mecánicos
 */
typedef struct {
    edrumulus_edge_detector_t edge_detector;           ///< Detector de flancos
    edrumulus_decay_analyzer_t decay_analyzer;         ///< Analizador de decaimiento
    edrumulus_velocity_validator_t velocity_validator; ///< Validador de velocidad
    edrumulus_adaptive_threshold_t adaptive_threshold; ///< Threshold adaptativo
    
    // Buffer circular para análisis temporal
    float signal_buffer[EDRUMULUS_REBOUND_SIGNAL_BUFFER_SIZE]; ///< 50ms de historia
    uint8_t buffer_index;               ///< Índice actual en buffer circular
    
    // Estado del detector
    bool hit_in_progress;               ///< Golpe en proceso de análisis
    uint32_t hit_start_time;            ///< Inicio del golpe actual
    float hit_peak_value;               ///< Valor máximo del golpe actual
    uint8_t hit_velocity;               ///< Velocidad calculada del golpe
    bool enabled;                       ///< Detector habilitado
    
    // Estadísticas de rendimiento
    uint32_t total_hits_detected;       ///< Total de hits detectados
    uint32_t rebounds_rejected;         ///< Rebotes rechazados
    float rejection_rate;               ///< Tasa de rechazo (0.0-1.0)
    uint32_t false_positives;           ///< Falsos positivos detectados
    uint32_t false_negatives;           ///< Falsos negativos estimados
} edrumulus_rebound_detector_t;

/**
 * @brief Initialize piezo sensor for latency measurement
 * 
 * @param channel ADC channel for piezo (typically 0 for GPIO4)
 * @param threshold Detection threshold for immediate trigger
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
esp_err_t edrumulus_detection_init_piezo(uint8_t channel, uint16_t threshold);

/**
 * @brief Check piezo for immediate hit detection with intelligent mask time
 * 
 * Esta función reemplaza el sistema anterior con delay fijo de 500ms.
 * Ahora utiliza mask time inteligente (2-10ms) para prevenir retriggering
 * sin sacrificar latencia ni perder golpes legítimos.
 * 
 * @param channel ADC channel for piezo
 * @param velocity Pointer to store detected velocity (0-127)
 * @return bool true if hit detected, false otherwise
 */
bool edrumulus_detection_check_piezo_hit(uint8_t channel, uint8_t *velocity);

/**
 * @brief Start continuous piezo monitoring task with intelligent detection
 * 
 * Inicia el monitoreo continuo con algoritmos de mask time inteligente.
 * Elimina el delay fijo de 500ms, mejorando la latencia de ~500ms a <3ms.
 * 
 * @param channel ADC channel for piezo
 * @param event_queue Queue to send hit events
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
esp_err_t edrumulus_detection_start_piezo_monitor(uint8_t channel, QueueHandle_t event_queue);

/**
 * @brief Stop piezo monitoring task
 * 
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
esp_err_t edrumulus_detection_stop_piezo_monitor(void);

// === FUNCIONES PÚBLICAS DEL FILTRO DE BANDA PASANTE ===

/**
 * @brief Initialize band-pass filter subsystem
 * 
 * Inicializa el filtro IIR de banda pasante (40-400 Hz) para eliminar
 * detecciones espurias causadas por vibraciones residuales del piezo.
 * 
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
esp_err_t edrumulus_detection_filter_init(void);

/**
 * @brief Deinitialize band-pass filter subsystem
 * 
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
esp_err_t edrumulus_detection_filter_deinit(void);

/**
 * @brief Process a single sample through the band-pass filter
 * 
 * Aplica filtrado IIR en cascada (pasa altos 40Hz + pasa bajos 400Hz)
 * para eliminar ruido DC, vibraciones lentas y armónicos altos.
 * 
 * @param input_sample Muestra de entrada normalizada (0.0 - 1.0)
 * @return float Muestra filtrada (0.0 - 1.0)
 */
float edrumulus_detection_filter_process(float input_sample);

/**
 * @brief Reset filter state (clear history)
 * 
 * Reinicia el estado interno del filtro, eliminando muestras anteriores.
 * Útil para evitar transitorios al cambiar configuraciones.
 */
void edrumulus_detection_filter_reset(void);

/**
 * @brief Set filter configuration
 * 
 * @param config Nueva configuración del filtro
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
esp_err_t edrumulus_detection_filter_set_config(const edrumulus_filter_config_t* config);

/**
 * @brief Get current filter configuration
 * 
 * @param config Pointer to store current filter configuration
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
esp_err_t edrumulus_detection_filter_get_config(edrumulus_filter_config_t* config);

/**
 * @brief Check if filter is enabled for a specific channel
 * @param channel ADC channel number (0-7)
 * @return true if filter is enabled, false otherwise
 */
bool edrumulus_detection_filter_is_enabled(uint8_t channel);

// === FUNCIONES PÚBLICAS PHASE 3: DETECTOR DE REBOTES ===

/**
 * @brief Initialize rebound detector subsystem
 * 
 * Inicializa el detector de rebotes mecánicos con algoritmos avanzados
 * de análisis de flancos, decaimiento exponencial y validación de velocidad.
 * 
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
esp_err_t edrumulus_rebound_detector_init(void);

/**
 * @brief Deinitialize rebound detector subsystem
 * 
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
esp_err_t edrumulus_rebound_detector_deinit(void);

/**
 * @brief Process signal sample through rebound detection algorithms
 * 
 * Analiza una muestra de señal usando detección de flancos, análisis de
 * decaimiento exponencial y validación de velocidad para determinar si
 * corresponde a un golpe real o un rebote mecánico.
 * 
 * @param signal Muestra de señal filtrada (0.0 - 1.0)
 * @param channel ADC channel number (0-7)
 * @return bool true si es golpe válido, false si es rebote
 */
bool edrumulus_rebound_detector_process(float signal, uint8_t channel);

/**
 * @brief Check if detected hit is valid (not a rebound)
 * 
 * Combina resultados de todos los algoritmos de análisis para determinar
 * la validez del golpe detectado.
 * 
 * @param channel ADC channel number (0-7)
 * @param velocity Velocidad detectada del golpe
 * @return bool true si el golpe es válido, false si es rebote
 */
bool edrumulus_rebound_detector_is_valid_hit(uint8_t channel, uint8_t velocity);

/**
 * @brief Reset rebound detector state for specific channel
 * 
 * Reinicia el estado del detector para un canal específico,
 * útil después de períodos de inactividad o cambios de configuración.
 * 
 * @param channel ADC channel number (0-7)
 */
void edrumulus_rebound_detector_reset(uint8_t channel);

/**
 * @brief Enable/disable rebound detector for specific channel
 * 
 * @param channel ADC channel number (0-7)
 * @param enabled true para habilitar, false para deshabilitar
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
esp_err_t edrumulus_rebound_detector_set_enabled(uint8_t channel, bool enabled);

/**
 * @brief Get rebound detector statistics
 * 
 * Obtiene estadísticas de rendimiento del detector de rebotes,
 * incluyendo tasa de rechazo y efectividad del algoritmo.
 * 
 * @param channel ADC channel number (0-7)
 * @param detector Pointer to store detector statistics
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
esp_err_t edrumulus_rebound_detector_get_statistics(uint8_t channel, edrumulus_rebound_detector_t *detector);

/**
 * @brief Configure rebound detector parameters
 * 
 * Permite ajustar parámetros del detector como velocidad mínima,
 * threshold de correlación, etc.
 * 
 * @param channel ADC channel number (0-7)
 * @param min_velocity Velocidad mínima válida (1-127)
 * @param min_rise_rate Velocidad mínima de subida (V/ms)
 * @param decay_threshold Threshold de correlación R² (0.0-1.0)
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
esp_err_t edrumulus_rebound_detector_configure(uint8_t channel, uint8_t min_velocity, 
                                               float min_rise_rate, float decay_threshold);

// === FUNCIONES PÚBLICAS PHASE 1: VALIDACIÓN ADC Y MUESTREO ===

/**
 * @brief Initialize ADC validation subsystem
 * 
 * Inicializa el sistema de validación Phase 1 para verificar precisión
 * del ADC, frecuencia de muestreo y calcular SNR según protocolo documentado.
 * 
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
esp_err_t edrumulus_adc_validation_init(void);

/**
 * @brief Deinitialize ADC validation subsystem
 * 
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
esp_err_t edrumulus_adc_validation_deinit(void);

/**
 * @brief Start ADC precision validation test
 * 
 * Ejecuta test de precisión del ADC midiendo linealidad R² > 0.99
 * en el rango completo 0-3.3V según criterios de aceptación.
 * 
 * @param channel ADC channel to test (0-7)
 * @param test_voltage_min Minimum test voltage (V)
 * @param test_voltage_max Maximum test voltage (V)
 * @param sample_count Number of samples for test
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
esp_err_t edrumulus_adc_validation_test_precision(uint8_t channel, float test_voltage_min, 
                                                  float test_voltage_max, uint16_t sample_count);

/**
 * @brief Start sampling rate validation test
 * 
 * Valida frecuencia de muestreo verificando 8kHz con desviación < 0.1%
 * según especificaciones del protocolo de validación.
 * 
 * @param channel ADC channel to test (0-7)
 * @param measurement_duration_ms Duration of measurement (ms)
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
esp_err_t edrumulus_adc_validation_test_sampling_rate(uint8_t channel, uint32_t measurement_duration_ms);

/**
 * @brief Start SNR measurement test
 * 
 * Mide relación señal/ruido verificando SNR > 60dB según criterios
 * de aceptación del sistema de validación.
 * 
 * @param channel ADC channel to test (0-7)
 * @param signal_frequency Test signal frequency (Hz)
 * @param measurement_duration_ms Duration of measurement (ms)
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
esp_err_t edrumulus_adc_validation_test_snr(uint8_t channel, float signal_frequency, 
                                            uint32_t measurement_duration_ms);

/**
 * @brief Run complete Phase 1 validation suite
 * 
 * Ejecuta suite completo de validación Phase 1 incluyendo precisión ADC,
 * frecuencia de muestreo y SNR con logs de validación automáticos.
 * 
 * @param channel ADC channel to test (0-7)
 * @return esp_err_t ESP_OK if all tests pass, error code otherwise
 */
esp_err_t edrumulus_adc_validation_run_full_suite(uint8_t channel);

/**
 * @brief Get ADC validation results
 * 
 * Obtiene resultados completos de validación Phase 1 incluyendo
 * métricas de aceptación y logs de diagnóstico.
 * 
 * @param validation Pointer to store validation results
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
esp_err_t edrumulus_adc_validation_get_results(edrumulus_adc_validation_t *validation);

/**
 * @brief Check if validation is currently in progress
 * 
 * @param channel ADC channel to check (0-7)
 * @return bool true if validation in progress, false otherwise
 */
bool edrumulus_adc_validation_is_in_progress(uint8_t channel);

/**
 * @brief Check if validation passed all criteria
 * 
 * Verifica si la validación Phase 1 cumplió todos los criterios:
 * - Precisión ADC: Error < 1%, R² > 0.99
 * - Frecuencia: Desviación < 0.1% de 8kHz
 * - SNR: > 60dB
 * 
 * @param channel ADC channel to check (0-7)
 * @return bool true if all criteria passed, false otherwise
 */
bool edrumulus_adc_validation_passed(uint8_t channel);

/**
 * @brief Generate validation log entry
 * 
 * Genera entrada de log de validación con timestamp preciso y
 * métricas según formato del protocolo documentado.
 * 
 * @param channel ADC channel (0-7)
 * @param log_message Log message to generate
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
esp_err_t edrumulus_adc_validation_log(uint8_t channel, const char *log_message);

/**
 * @brief Reset validation state for specific channel
 * 
 * Reinicia estado de validación para permitir nueva ejecución
 * de tests en el canal especificado.
 * 
 * @param channel ADC channel to reset (0-7)
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
esp_err_t edrumulus_adc_validation_reset(uint8_t channel);

// === FUNCIONES PÚBLICAS PHASE 2: VALIDACIÓN FILTRO DE BANDA PASANTE ===

/**
 * @brief Initialize filter validation subsystem
 * 
 * Inicializa el sistema de validación Phase 2 para verificar respuesta
 * en frecuencia del filtro, atenuación en bandas y calibración automática.
 * 
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
esp_err_t edrumulus_filter_validation_init(void);

/**
 * @brief Deinitialize filter validation subsystem
 * 
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
esp_err_t edrumulus_filter_validation_deinit(void);

/**
 * @brief Start frequency response analysis test
 * 
 * Ejecuta barrido de frecuencias 10Hz-1kHz midiendo atenuación
 * en banda pasante (<3dB) y banda rechazada (>20dB) según protocolo.
 * 
 * @param channel ADC channel to test (0-7)
 * @param start_freq Start frequency for sweep (Hz)
 * @param end_freq End frequency for sweep (Hz)
 * @param freq_step Frequency step size (Hz)
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
esp_err_t edrumulus_filter_validation_test_frequency_response(uint8_t channel, 
                                                              float start_freq, 
                                                              float end_freq, 
                                                              float freq_step);

/**
 * @brief Start passband validation test
 * 
 * Valida banda pasante 40-400Hz verificando atenuación <3dB
 * y ripple <1dB según criterios de aceptación del protocolo.
 * 
 * @param channel ADC channel to test (0-7)
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
esp_err_t edrumulus_filter_validation_test_passband(uint8_t channel);

/**
 * @brief Start stopband validation test
 * 
 * Valida banda rechazada (<40Hz, >400Hz) verificando atenuación >20dB
 * según especificaciones del filtro de banda pasante.
 * 
 * @param channel ADC channel to test (0-7)
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
esp_err_t edrumulus_filter_validation_test_stopband(uint8_t channel);

/**
 * @brief Start filter stability test
 * 
 * Verifica estabilidad del filtro IIR sin oscilaciones o saturación
 * durante operación continua con señales de test.
 * 
 * @param channel ADC channel to test (0-7)
 * @param test_duration_ms Duration of stability test (ms)
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
esp_err_t edrumulus_filter_validation_test_stability(uint8_t channel, uint32_t test_duration_ms);

/**
 * @brief Generate synthetic test signal
 * 
 * Genera señal sintética (sine wave + ruido) para validación automática
 * del filtro con frecuencias y amplitudes controladas.
 * 
 * @param frequency_hz Signal frequency (Hz)
 * @param amplitude Signal amplitude (0.0-1.0)
 * @param noise_level Noise level (0.0-1.0)
 * @param duration_ms Signal duration (ms)
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
esp_err_t edrumulus_filter_validation_generate_test_signal(float frequency_hz, 
                                                           float amplitude, 
                                                           float noise_level, 
                                                           uint32_t duration_ms);

/**
 * @brief Start automatic filter calibration
 * 
 * Ejecuta calibración automática del filtro ajustando parámetros
 * para optimizar respuesta en frecuencia según especificaciones.
 * 
 * @param channel ADC channel to calibrate (0-7)
 * @param target_config Target filter configuration
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
esp_err_t edrumulus_filter_validation_auto_calibrate(uint8_t channel, 
                                                      const edrumulus_filter_config_t *target_config);

/**
 * @brief Run complete Phase 2 validation suite
 * 
 * Ejecuta suite completo de validación Phase 2 incluyendo respuesta
 * en frecuencia, banda pasante, banda rechazada y estabilidad.
 * 
 * @param channel ADC channel to test (0-7)
 * @return esp_err_t ESP_OK if all tests pass, error code otherwise
 */
esp_err_t edrumulus_filter_validation_run_full_suite(uint8_t channel);

/**
 * @brief Get filter validation results
 * 
 * Obtiene resultados completos de validación Phase 2 incluyendo
 * respuesta en frecuencia, métricas de aceptación y logs detallados.
 * 
 * @param validation Pointer to store validation results
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
esp_err_t edrumulus_filter_validation_get_results(edrumulus_filter_validation_t *validation);

/**
 * @brief Check if filter validation is currently in progress
 * 
 * @param channel ADC channel to check (0-7)
 * @return bool true if validation in progress, false otherwise
 */
bool edrumulus_filter_validation_is_in_progress(uint8_t channel);

/**
 * @brief Check if filter validation passed all criteria
 * 
 * Verifica si la validación Phase 2 cumplió todos los criterios:
 * - Banda pasante: Atenuación <3dB (40-400Hz)
 * - Banda rechazada: Atenuación >20dB (<40Hz, >400Hz)
 * - Estabilidad: Sin oscilaciones o saturación
 * 
 * @param channel ADC channel to check (0-7)
 * @return bool true if all criteria passed, false otherwise
 */
bool edrumulus_filter_validation_passed(uint8_t channel);

/**
 * @brief Generate filter validation log entry
 * 
 * Genera entrada de log de validación con timestamp preciso y
 * métricas según formato del protocolo documentado.
 * 
 * @param channel ADC channel (0-7)
 * @param frequency_hz Test frequency (Hz)
 * @param input_amplitude Input signal amplitude (V)
 * @param output_amplitude Output signal amplitude (V)
 * @param attenuation_db Measured attenuation (dB)
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
esp_err_t edrumulus_filter_validation_log(uint8_t channel, 
                                           float frequency_hz, 
                                           float input_amplitude, 
                                           float output_amplitude, 
                                           float attenuation_db);

/**
 * @brief Reset filter validation state for specific channel
 * 
 * Reinicia estado de validación para permitir nueva ejecución
 * de tests en el canal especificado.
 * 
 * @param channel ADC channel to reset (0-7)
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
esp_err_t edrumulus_filter_validation_reset(uint8_t channel);

/**
 * @brief Configure filter validation test parameters
 * 
 * Configura parámetros de test como rango de frecuencias,
 * amplitud de señal, nivel de ruido y criterios de aceptación.
 * 
 * @param channel ADC channel (0-7)
 * @param test_config Test configuration parameters
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
esp_err_t edrumulus_filter_validation_configure(uint8_t channel, 
                                                 const edrumulus_filter_test_config_t *test_config);

/**
 * @brief Get frequency response measurement
 * 
 * Obtiene medición de respuesta en frecuencia para una frecuencia
 * específica, incluyendo magnitud y fase.
 * 
 * @param channel ADC channel (0-7)
 * @param frequency_hz Test frequency (Hz)
 * @param point Pointer to store frequency response point
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
esp_err_t edrumulus_filter_validation_measure_frequency_point(uint8_t channel, 
                                                               float frequency_hz, 
                                                               edrumulus_frequency_point_t *point);

// === FUNCIONES PÚBLICAS PHASE 3: VALIDACIÓN ALGORITMOS AVANZADOS ===

/**
 * @brief Initialize Phase 3 validation subsystem
 * 
 * Inicializa el sistema de validación Phase 3 para algoritmos avanzados
 * incluyendo edge detector, decay analyzer, velocity validator y adaptive threshold.
 * 
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
esp_err_t edrumulus_phase3_validation_init(void);

/**
 * @brief Deinitialize Phase 3 validation subsystem
 * 
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
esp_err_t edrumulus_phase3_validation_deinit(void);

/**
 * @brief Test edge detector algorithm validation
 * 
 * Ejecuta validación del algoritmo de detección de flancos midiendo
 * sensitivity, specificity, precision y F1-score con datos sintéticos.
 * 
 * @param channel ADC channel to test (0-7)
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
esp_err_t edrumulus_phase3_test_edge_detector(uint8_t channel);

/**
 * @brief Test decay analyzer algorithm validation
 * 
 * Ejecuta validación del analizador de decaimiento exponencial midiendo
 * goodness of fit (R²), precisión de tau y amplitud con señales sintéticas.
 * 
 * @param channel ADC channel to test (0-7)
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
esp_err_t edrumulus_phase3_test_decay_analyzer(uint8_t channel);

/**
 * @brief Test velocity validator algorithm validation
 * 
 * Ejecuta validación del validador de velocidad midiendo linealidad,
 * repetibilidad y precisión en todo el rango dinámico MIDI (0-127).
 * 
 * @param channel ADC channel to test (0-7)
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
esp_err_t edrumulus_phase3_test_velocity_validator(uint8_t channel);

/**
 * @brief Test adaptive threshold algorithm validation
 * 
 * Ejecuta validación del umbral adaptativo midiendo SNR target,
 * tiempo de adaptación y estabilidad según protocolo documentado.
 * 
 * @param channel ADC channel to test (0-7)
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
esp_err_t edrumulus_phase3_test_adaptive_threshold(uint8_t channel);

/**
 * @brief Run complete Phase 3 validation suite
 * 
 * Ejecuta suite completo de validación Phase 3 incluyendo todos los
 * algoritmos avanzados y métricas de rendimiento del sistema completo.
 * 
 * @param channel ADC channel to test (0-7)
 * @return esp_err_t ESP_OK if all tests pass, error code otherwise
 */
esp_err_t edrumulus_phase3_run_full_validation(uint8_t channel);

/**
 * @brief Get Phase 3 validation results
 * 
 * Obtiene resultados completos de validación Phase 3 incluyendo
 * métricas de cada algoritmo y rendimiento global del sistema.
 * 
 * @param channel ADC channel (0-7)
 * @param results Pointer to store validation results
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
esp_err_t edrumulus_phase3_get_validation_results(uint8_t channel, edrumulus_phase3_validation_t **results);

/**
 * @brief Generate Phase 3 validation report
 * 
 * Genera reporte detallado de validación Phase 3 con métricas,
 * criterios de aceptación y resultado final en formato legible.
 * 
 * @param channel ADC channel (0-7)
 * @param report_buffer Buffer to store report text
 * @param buffer_size Size of report buffer (minimum 1024 bytes)
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
esp_err_t edrumulus_phase3_generate_validation_report(uint8_t channel, char *report_buffer, size_t buffer_size);

/**
 * @brief Check if Phase 3 validation passed all criteria
 * 
 * Verifica si la validación Phase 3 cumplió todos los criterios:
 * - Edge Detector: Sensitivity ≥0.95, Specificity ≥0.90
 * - Decay Analyzer: R² ≥0.85, Error Tau ≤15%, Error Amplitud ≤10%
 * - Velocity Validator: Linealidad R² ≥0.95, Repetibilidad σ ≤2.0, Precisión ≤3.0
 * - Adaptive Threshold: SNR ≥10dB, Tiempo Adaptación ≤50ms, Estabilidad ≤0.001
 * - Rendimiento: Latencia ≤50ms, Accuracy ≥85%
 * 
 * @param channel ADC channel to check (0-7)
 * @return bool true if all criteria passed, false otherwise
 */
bool edrumulus_phase3_validation_passed(uint8_t channel);

/**
 * @brief Reset Phase 3 validation state for specific channel
 * 
 * Reinicia estado de validación Phase 3 para permitir nueva ejecución
 * de tests en el canal especificado.
 * 
 * @param channel ADC channel to reset (0-7)
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
esp_err_t edrumulus_phase3_validation_reset(uint8_t channel);

/**
 * @brief Configure Phase 3 validation test parameters
 * 
 * Configura parámetros de test como número de muestras, duración,
 * niveles de ruido y criterios de aceptación personalizados.
 * 
 * @param channel ADC channel (0-7)
 * @param test_samples Number of test samples per algorithm
 * @param test_duration_ms Test duration in milliseconds
 * @param noise_level Synthetic noise level (0.0-1.0)
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
esp_err_t edrumulus_phase3_validation_configure(uint8_t channel, uint16_t test_samples, 
                                                 uint32_t test_duration_ms, float noise_level);

/**
 * @brief Check if Phase 3 validation is currently in progress
 * 
 * @param channel ADC channel to check (0-7)
 * @return bool true if validation in progress, false otherwise
 */
bool edrumulus_phase3_validation_is_in_progress(uint8_t channel);

/**
 * @brief Get Phase 3 validation progress percentage
 * 
 * Obtiene porcentaje de progreso de la validación Phase 3 en curso
 * para mostrar al usuario durante tests largos.
 * 
 * @param channel ADC channel (0-7)
 * @return uint8_t Progress percentage (0-100)
 */
uint8_t edrumulus_phase3_validation_get_progress(uint8_t channel);

#ifdef __cplusplus
}
#endif

#endif // EDRUMULUS_DETECTION_H