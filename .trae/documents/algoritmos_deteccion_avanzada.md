# Algoritmos de Detección Avanzada - Especificación Técnica

## 1. Arquitectura del Sistema de Detección

### 1.1 Flujo de Procesamiento de Señal
```
ADC Sample → FIFO Buffer → Filtro Pasa-Banda → Detección Umbral → 
    ↓
Ventana Pre-Scan ← Cancelación Retriggering ← Detección de Pico → 
    ↓
Cálculo Velocity → Envío MIDI
```

### 1.2 Componentes del Sistema
- **ADC Sampler**: Captura continua a 8kHz
- **Signal Processor**: Filtrado y acondicionamiento
- **Peak Detector**: Identificación de picos principales
- **Velocity Calculator**: Cálculo de intensidad MIDI
- **Retrigger Canceller**: Supresión de ecos

## 2. Implementación del Filtro Pasa-Banda

### 2.1 Diseño del Filtro IIR
**Especificaciones**:
- Tipo: Butterworth de 2do orden
- Frecuencia de corte inferior: 40Hz
- Frecuencia de corte superior: 400Hz
- Frecuencia de muestreo: 8kHz
- Ripple: <0.1dB en banda de paso

### 2.2 Cálculo de Coeficientes
```c
// Coeficientes para filtro pasa-banda 40Hz-400Hz @ 8kHz
#define BP_FILTER_B0  0.0318
#define BP_FILTER_B1  0.0000
#define BP_FILTER_B2 -0.0318
#define BP_FILTER_A1 -1.7347
#define BP_FILTER_A2  0.9364

typedef struct {
    float b0, b1, b2;  // Coeficientes del numerador
    float a1, a2;      // Coeficientes del denominador
    float x1, x2;      // Muestras anteriores de entrada
    float y1, y2;      // Muestras anteriores de salida
    float gain;        // Ganancia de normalización
} iir_bandpass_t;

// Inicialización del filtro
void init_bandpass_filter(iir_bandpass_t *filter) {
    filter->b0 = BP_FILTER_B0;
    filter->b1 = BP_FILTER_B1;
    filter->b2 = BP_FILTER_B2;
    filter->a1 = BP_FILTER_A1;
    filter->a2 = BP_FILTER_A2;
    filter->x1 = filter->x2 = 0.0f;
    filter->y1 = filter->y2 = 0.0f;
    filter->gain = 3.14f; // Compensación de ganancia
}

// Procesamiento de muestra
float process_bandpass_filter(iir_bandpass_t *filter, float input) {
    // Ecuación de diferencias: y[n] = b0*x[n] + b1*x[n-1] + b2*x[n-2] - a1*y[n-1] - a2*y[n-2]
    float output = filter->b0 * input + 
                   filter->b1 * filter->x1 + 
                   filter->b2 * filter->x2 - 
                   filter->a1 * filter->y1 - 
                   filter->a2 * filter->y2;
    
    // Actualizar historia
    filter->x2 = filter->x1;
    filter->x1 = input;
    filter->y2 = filter->y1;
    filter->y1 = output;
    
    return output * filter->gain;
}
```

## 3. Sistema de Detección de Picos

### 3.1 Ventanas de Procesamiento
```c
#define SAMPLE_RATE_HZ        8000
#define PRE_SCAN_SAMPLES      (SAMPLE_RATE_HZ * 5 / 1000)   // 5ms = 40 samples
#define SCAN_WINDOW_SAMPLES   (SAMPLE_RATE_HZ * 10 / 1000)  // 10ms = 80 samples
#define MASK_TIME_SAMPLES     (SAMPLE_RATE_HZ * 20 / 1000)  // 20ms = 160 samples

typedef enum {
    DETECTION_IDLE,
    DETECTION_PRE_SCAN,
    DETECTION_SCANNING,
    DETECTION_MASKED
} detection_state_t;

typedef struct {
    detection_state_t state;
    uint32_t state_start_sample;
    float peak_value;
    uint32_t peak_sample;
    float threshold;
    bool peak_found;
} peak_detector_t;
```

### 3.2 Algoritmo de Detección
```c
bool detect_peak(peak_detector_t *detector, float filtered_sample, uint32_t sample_count) {
    switch (detector->state) {
        case DETECTION_IDLE:
            // Verificar si la muestra supera el umbral
            if (filtered_sample > detector->threshold) {
                detector->state = DETECTION_PRE_SCAN;
                detector->state_start_sample = sample_count;
                detector->peak_value = filtered_sample;
                detector->peak_sample = sample_count;
                detector->peak_found = false;
                return false; // Continuar con pre-scan
            }
            break;
            
        case DETECTION_PRE_SCAN: {
            // Buscar pico en ventana de pre-scan
            uint32_t elapsed = sample_count - detector->state_start_sample;
            
            if (filtered_sample > detector->peak_value) {
                detector->peak_value = filtered_sample;
                detector->peak_sample = sample_count;
            }
            
            if (elapsed >= PRE_SCAN_SAMPLES) {
                detector->state = DETECTION_SCANNING;
                detector->state_start_sample = sample_count;
            }
            break;
        }
        
        case DETECTION_SCANNING: {
            // Continuar búsqueda en ventana de scan
            uint32_t elapsed = sample_count - detector->state_start_sample;
            
            if (filtered_sample > detector->peak_value) {
                detector->peak_value = filtered_sample;
                detector->peak_sample = sample_count;
            }
            
            if (elapsed >= SCAN_WINDOW_SAMPLES) {
                // Fin de ventana de scan - validar pico
                if (detector->peak_value > detector->threshold * 1.2f) {
                    detector->peak_found = true;
                    detector->state = DETECTION_MASKED;
                    detector->state_start_sample = sample_count;
                    return true; // Pico válido detectado
                } else {
                    // Pico no válido - volver a idle
                    detector->state = DETECTION_IDLE;
                }
            }
            break;
        }
        
        case DETECTION_MASKED: {
            // Período de máscara - ignorar nuevas detecciones
            uint32_t elapsed = sample_count - detector->state_start_sample;
            
            if (elapsed >= MASK_TIME_SAMPLES) {
                detector->state = DETECTION_IDLE;
            }
            break;
        }
    }
    
    return false;
}
```

## 4. Cancelación de Retriggering

### 4.1 Modelo de Decaimiento Exponencial
```c
#define MAX_DECAY_SAMPLES  (SAMPLE_RATE_HZ * 200 / 1000)  // 200ms máximo

typedef struct {
    bool active;
    float initial_amplitude;
    float decay_constant;     // τ en segundos
    uint32_t start_sample;
    float position_factor;    // Ajuste por posición del golpe
} retrigger_canceller_t;

// Inicializar cancelación después de detectar pico
void start_retrigger_cancellation(retrigger_canceller_t *canceller, 
                                 float peak_amplitude, 
                                 uint32_t current_sample) {
    canceller->active = true;
    canceller->initial_amplitude = peak_amplitude;
    canceller->decay_constant = 0.05f; // 50ms de constante de tiempo
    canceller->start_sample = current_sample;
    canceller->position_factor = 1.0f; // Ajustar según posición detectada
}

// Calcular valor de cancelación
float calculate_cancellation_value(retrigger_canceller_t *canceller, 
                                  uint32_t current_sample) {
    if (!canceller->active) {
        return 0.0f;
    }
    
    uint32_t elapsed_samples = current_sample - canceller->start_sample;
    
    if (elapsed_samples > MAX_DECAY_SAMPLES) {
        canceller->active = false;
        return 0.0f;
    }
    
    // Calcular decaimiento exponencial: A * e^(-t/τ)
    float elapsed_time = (float)elapsed_samples / SAMPLE_RATE_HZ;
    float decay_factor = expf(-elapsed_time / canceller->decay_constant);
    
    return canceller->initial_amplitude * decay_factor * canceller->position_factor;
}
```

## 5. Cálculo de Velocity MIDI

### 5.1 Algoritmo de Intensidad Mejorado
```c
#define VELOCITY_MIN  1
#define VELOCITY_MAX  127
#define ENERGY_WINDOW_SAMPLES  (SAMPLE_RATE_HZ * 5 / 1000)  // 5ms

typedef struct {
    float peak_amplitude;
    float total_energy;
    float position_metric;
    uint8_t velocity_curve_type;
} velocity_calculator_t;

// Calcular energía total en ventana alrededor del pico
float calculate_total_energy(float *samples, uint32_t peak_index, uint32_t buffer_size) {
    float energy = 0.0f;
    uint32_t start_idx = (peak_index >= ENERGY_WINDOW_SAMPLES/2) ? 
                        peak_index - ENERGY_WINDOW_SAMPLES/2 : 0;
    uint32_t end_idx = (peak_index + ENERGY_WINDOW_SAMPLES/2 < buffer_size) ? 
                      peak_index + ENERGY_WINDOW_SAMPLES/2 : buffer_size - 1;
    
    for (uint32_t i = start_idx; i <= end_idx; i++) {
        energy += samples[i] * samples[i]; // Energía = suma de cuadrados
    }
    
    return sqrtf(energy / (end_idx - start_idx + 1)); // RMS
}

// Aplicar curva de respuesta logarítmica
uint8_t calculate_midi_velocity(velocity_calculator_t *calc) {
    // Combinar amplitud de pico y energía total
    float combined_intensity = 0.7f * calc->peak_amplitude + 0.3f * calc->total_energy;
    
    // Normalizar a rango 0-1
    float normalized = combined_intensity / 4095.0f; // Asumiendo ADC de 12 bits
    
    // Aplicar curva logarítmica para respuesta más natural
    float log_response;
    switch (calc->velocity_curve_type) {
        case 0: // Lineal
            log_response = normalized;
            break;
        case 1: // Logarítmica suave
            log_response = logf(1.0f + normalized * 9.0f) / logf(10.0f);
            break;
        case 2: // Logarítmica fuerte
            log_response = logf(1.0f + normalized * 99.0f) / logf(100.0f);
            break;
        default:
            log_response = normalized;
    }
    
    // Compensar por posición (centro vs. borde)
    log_response *= (0.8f + 0.2f * calc->position_metric);
    
    // Convertir a rango MIDI
    uint8_t velocity = (uint8_t)(log_response * (VELOCITY_MAX - VELOCITY_MIN) + VELOCITY_MIN);
    
    // Asegurar rango válido
    if (velocity < VELOCITY_MIN) velocity = VELOCITY_MIN;
    if (velocity > VELOCITY_MAX) velocity = VELOCITY_MAX;
    
    return velocity;
}
```

## 6. Integración del Sistema Completo

### 6.1 Estructura Principal de Detección
```c
typedef struct {
    // Componentes del sistema
    iir_bandpass_t bandpass_filter;
    peak_detector_t peak_detector;
    retrigger_canceller_t retrigger_canceller;
    velocity_calculator_t velocity_calc;
    
    // Buffers de datos
    float raw_samples[FIFO_SIZE];
    float filtered_samples[FIFO_SIZE];
    uint32_t sample_index;
    
    // Estado del sistema
    bool initialized;
    uint32_t total_samples_processed;
    
    // Configuración
    float base_threshold;
    float adaptive_threshold;
    uint8_t sensitivity_level;
} advanced_detection_system_t;

// Función principal de procesamiento
bool process_adc_sample(advanced_detection_system_t *system, int16_t adc_value, uint8_t *velocity) {
    if (!system->initialized) {
        return false;
    }
    
    // 1. Convertir ADC a float y almacenar
    float raw_sample = (float)adc_value;
    uint32_t buffer_idx = system->sample_index % FIFO_SIZE;
    system->raw_samples[buffer_idx] = raw_sample;
    
    // 2. Aplicar filtro pasa-banda
    float filtered = process_bandpass_filter(&system->bandpass_filter, raw_sample);
    system->filtered_samples[buffer_idx] = filtered;
    
    // 3. Aplicar cancelación de retriggering si está activa
    float cancellation = calculate_cancellation_value(&system->retrigger_canceller, 
                                                      system->total_samples_processed);
    filtered -= cancellation;
    
    // 4. Actualizar umbral adaptativo
    update_adaptive_threshold(system, filtered);
    
    // 5. Detectar pico
    bool peak_detected = detect_peak(&system->peak_detector, filtered, 
                                   system->total_samples_processed);
    
    if (peak_detected) {
        // 6. Calcular velocity
        system->velocity_calc.peak_amplitude = system->peak_detector.peak_value;
        system->velocity_calc.total_energy = calculate_total_energy(
            system->filtered_samples, 
            system->peak_detector.peak_sample % FIFO_SIZE, 
            FIFO_SIZE
        );
        
        *velocity = calculate_midi_velocity(&system->velocity_calc);
        
        // 7. Iniciar cancelación de retriggering
        start_retrigger_cancellation(&system->retrigger_canceller, 
                                    system->peak_detector.peak_value,
                                    system->total_samples_processed);
        
        return true;
    }
    
    // Actualizar contadores
    system->sample_index++;
    system->total_samples_processed++;
    
    return false;
}
```

### 6.2 Umbral Adaptativo
```c
#define NOISE_FLOOR_SAMPLES  (SAMPLE_RATE_HZ * 100 / 1000)  // 100ms para ruido de fondo

void update_adaptive_threshold(advanced_detection_system_t *system, float filtered_sample) {
    static float noise_floor_buffer[NOISE_FLOOR_SAMPLES];
    static uint32_t noise_index = 0;
    static float noise_floor_sum = 0.0f;
    static bool noise_buffer_full = false;
    
    // Solo actualizar ruido de fondo cuando no hay detección activa
    if (system->peak_detector.state == DETECTION_IDLE) {
        // Actualizar buffer circular de ruido de fondo
        if (noise_buffer_full) {
            noise_floor_sum -= noise_floor_buffer[noise_index];
        }
        
        noise_floor_buffer[noise_index] = fabsf(filtered_sample);
        noise_floor_sum += noise_floor_buffer[noise_index];
        
        noise_index = (noise_index + 1) % NOISE_FLOOR_SAMPLES;
        if (noise_index == 0) {
            noise_buffer_full = true;
        }
        
        // Calcular nuevo umbral adaptativo
        if (noise_buffer_full) {
            float avg_noise = noise_floor_sum / NOISE_FLOOR_SAMPLES;
            system->adaptive_threshold = system->base_threshold + 
                                       (avg_noise * 3.0f); // 3 sigma sobre ruido
            
            // Actualizar umbral en detector
            system->peak_detector.threshold = system->adaptive_threshold;
        }
    }
}
```

## 7. Parámetros de Configuración

### 7.1 Configuración por Defecto
```c
#define DEFAULT_BASE_THRESHOLD     100.0f
#define DEFAULT_SENSITIVITY        64     // Rango 0-127
#define DEFAULT_VELOCITY_CURVE     1      // Logarítmica suave
#define DEFAULT_DECAY_CONSTANT     0.05f  // 50ms
#define DEFAULT_POSITION_FACTOR    1.0f

// Inicialización del sistema
void init_advanced_detection_system(advanced_detection_system_t *system) {
    // Inicializar filtro
    init_bandpass_filter(&system->bandpass_filter);
    
    // Configurar detector de picos
    system->peak_detector.state = DETECTION_IDLE;
    system->peak_detector.threshold = DEFAULT_BASE_THRESHOLD;
    system->peak_detector.peak_found = false;
    
    // Configurar cancelación de retriggering
    system->retrigger_canceller.active = false;
    system->retrigger_canceller.decay_constant = DEFAULT_DECAY_CONSTANT;
    
    // Configurar calculadora de velocity
    system->velocity_calc.velocity_curve_type = DEFAULT_VELOCITY_CURVE;
    system->velocity_calc.position_metric = DEFAULT_POSITION_FACTOR;
    
    // Inicializar buffers
    memset(system->raw_samples, 0, sizeof(system->raw_samples));
    memset(system->filtered_samples, 0, sizeof(system->filtered_samples));
    
    // Estado inicial
    system->sample_index = 0;
    system->total_samples_processed = 0;
    system->base_threshold = DEFAULT_BASE_THRESHOLD;
    system->adaptive_threshold = DEFAULT_BASE_THRESHOLD;
    system->sensitivity_level = DEFAULT_SENSITIVITY;
    system->initialized = true;
}
```

## 8. Optimizaciones de Rendimiento

### 8.1 Consideraciones de Memoria
- **FIFO Size**: 400 samples (50ms @ 8kHz) = 1.6KB por buffer
- **Total RAM**: ~5KB para sistema completo
- **Stack Usage**: <1KB por task

### 8.2 Optimizaciones de CPU
- Usar aritmética de punto fijo donde sea posible
- Precalcular constantes de filtro
- Optimizar loops críticos
- Usar DMA para ADC cuando esté disponible

### 8.3 Latencia Total Estimada
- **ADC Sampling**: <0.1ms
- **Filtro IIR**: <0.1ms
- **Detección de Pico**: <0.5ms
- **Cálculo Velocity**: <0.2ms
- **Total**: <1ms (excluyendo ventanas de scan)

---

**Nota**: Esta implementación proporciona la base técnica para los algoritmos avanzados. La integración debe realizarse gradualmente según el roadmap establecido.