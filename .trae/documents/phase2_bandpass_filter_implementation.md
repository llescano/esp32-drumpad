# 🔧 Fase 2: Implementación de Filtro de Banda Pasante - Especificaciones Técnicas

## 📋 Resumen de la Fase 2

**Objetivo Principal**: Eliminar detecciones espurias mediante filtrado digital de banda pasante (40-400 Hz)

**Problema a Resolver**: 
- Hit real detectado: velocity=118
- Detecciones espurias: múltiples hits de velocity=10 cada ~7ms
- Causa: Vibraciones residuales del piezo después del impacto inicial

**Solución**: Filtro IIR de banda pasante en cascada (pasa altos + pasa bajos)

## 🎯 Especificaciones del Filtro

### Parámetros de Diseño
```c
#define EDRUMULUS_FILTER_SAMPLE_RATE    1000    // Hz (1ms sampling)
#define EDRUMULUS_FILTER_LOW_CUTOFF     40      // Hz - elimina ruido DC y vibraciones lentas
#define EDRUMULUS_FILTER_HIGH_CUTOFF    400     // Hz - elimina armónicos altos y ruido
#define EDRUMULUS_FILTER_ORDER          4       // 2 biquads en cascada
#define EDRUMULUS_FILTER_Q              0.707f  // Factor de calidad Butterworth
```

### Respuesta en Frecuencia Objetivo
- **Banda de paso**: 40-400 Hz (-3dB)
- **Atenuación**: >40dB fuera de la banda
- **Ripple**: <0.5dB en banda de paso
- **Latencia de grupo**: <0.5ms

## 🔧 Estructuras de Datos

### Archivo: edrumulus_detection.h (Adiciones)
```c
// Tipos de filtro
typedef enum {
    EDRUMULUS_FILTER_LOW_PASS,
    EDRUMULUS_FILTER_HIGH_PASS,
    EDRUMULUS_FILTER_BAND_PASS
} edrumulus_filter_type_t;

// Coeficientes de filtro biquad
typedef struct {
    float b0, b1, b2;  // Coeficientes del numerador
    float a1, a2;      // Coeficientes del denominador (a0 normalizado a 1)
} edrumulus_biquad_coeffs_t;

// Estado del filtro biquad
typedef struct {
    float x1, x2;      // Muestras de entrada anteriores
    float y1, y2;      // Muestras de salida anteriores
} edrumulus_biquad_state_t;

// Configuración completa del filtro de banda pasante
typedef struct {
    edrumulus_biquad_coeffs_t high_pass;  // Filtro pasa altos (40 Hz)
    edrumulus_biquad_coeffs_t low_pass;   // Filtro pasa bajos (400 Hz)
    edrumulus_biquad_state_t hp_state;    // Estado del filtro pasa altos
    edrumulus_biquad_state_t lp_state;    // Estado del filtro pasa bajos
    bool enabled;                         // Habilitación del filtro
    float gain_compensation;              // Compensación de ganancia
    uint32_t sample_count;                // Contador de muestras procesadas
} edrumulus_bandpass_filter_t;

// Configuración del filtro (para NVS)
typedef struct {
    bool filter_enabled;
    float low_cutoff_hz;
    float high_cutoff_hz;
    float gain_compensation;
    uint8_t filter_order;
} edrumulus_filter_config_t;

// Funciones públicas del filtro
esp_err_t edrumulus_detection_filter_init(void);
esp_err_t edrumulus_detection_filter_deinit(void);
float edrumulus_detection_filter_process(float input_sample);
void edrumulus_detection_filter_reset(void);
esp_err_t edrumulus_detection_filter_set_config(const edrumulus_filter_config_t* config);
esp_err_t edrumulus_detection_filter_get_config(edrumulus_filter_config_t* config);
bool edrumulus_detection_filter_is_enabled(void);
```

## 🧮 Algoritmos de Implementación

### Cálculo de Coeficientes Butterworth
```c
/**
 * Calcula coeficientes para filtro Butterworth de 2do orden
 * @param coeffs Estructura para almacenar coeficientes
 * @param cutoff_freq Frecuencia de corte en Hz
 * @param sample_rate Frecuencia de muestreo en Hz
 * @param filter_type Tipo de filtro (pasa altos/bajos)
 */
static void calculate_butterworth_coeffs(edrumulus_biquad_coeffs_t* coeffs,
                                        float cutoff_freq,
                                        float sample_rate,
                                        edrumulus_filter_type_t filter_type) {
    // Frecuencia angular normalizada
    float omega = 2.0f * M_PI * cutoff_freq / sample_rate;
    float cos_omega = cosf(omega);
    float sin_omega = sinf(omega);
    
    // Factor de calidad para respuesta Butterworth
    float alpha = sin_omega / (2.0f * EDRUMULUS_FILTER_Q);
    
    // Cálculo de coeficientes según tipo de filtro
    if (filter_type == EDRUMULUS_FILTER_LOW_PASS) {
        // Filtro pasa bajos
        coeffs->b0 = (1.0f - cos_omega) / 2.0f;
        coeffs->b1 = 1.0f - cos_omega;
        coeffs->b2 = (1.0f - cos_omega) / 2.0f;
    } else if (filter_type == EDRUMULUS_FILTER_HIGH_PASS) {
        // Filtro pasa altos
        coeffs->b0 = (1.0f + cos_omega) / 2.0f;
        coeffs->b1 = -(1.0f + cos_omega);
        coeffs->b2 = (1.0f + cos_omega) / 2.0f;
    }
    
    // Coeficientes del denominador (comunes)
    float a0 = 1.0f + alpha;
    coeffs->a1 = (-2.0f * cos_omega) / a0;
    coeffs->a2 = (1.0f - alpha) / a0;
    
    // Normalizar coeficientes del numerador
    coeffs->b0 /= a0;
    coeffs->b1 /= a0;
    coeffs->b2 /= a0;
}
```

### Procesamiento de Muestra Individual
```c
/**
 * Procesa una muestra a través del filtro biquad
 * @param coeffs Coeficientes del filtro
 * @param state Estado del filtro (muestras anteriores)
 * @param input Muestra de entrada
 * @return Muestra filtrada
 */
static float process_biquad(const edrumulus_biquad_coeffs_t* coeffs,
                           edrumulus_biquad_state_t* state,
                           float input) {
    // Ecuación de diferencias del filtro biquad
    float output = coeffs->b0 * input +
                   coeffs->b1 * state->x1 +
                   coeffs->b2 * state->x2 -
                   coeffs->a1 * state->y1 -
                   coeffs->a2 * state->y2;
    
    // Actualizar estado (shift de muestras)
    state->x2 = state->x1;
    state->x1 = input;
    state->y2 = state->y1;
    state->y1 = output;
    
    return output;
}
```

### Filtro de Banda Pasante Completo
```c
/**
 * Procesa una muestra a través del filtro de banda pasante completo
 * @param filter Estructura del filtro de banda pasante
 * @param input Muestra de entrada (0.0 - 1.0)
 * @return Muestra filtrada
 */
float edrumulus_detection_filter_process(float input_sample) {
    if (!g_bandpass_filter.enabled) {
        return input_sample;
    }
    
    // Paso 1: Filtro pasa altos (elimina DC y bajas frecuencias)
    float hp_output = process_biquad(&g_bandpass_filter.high_pass,
                                    &g_bandpass_filter.hp_state,
                                    input_sample);
    
    // Paso 2: Filtro pasa bajos (elimina altas frecuencias)
    float lp_output = process_biquad(&g_bandpass_filter.low_pass,
                                    &g_bandpass_filter.lp_state,
                                    hp_output);
    
    // Aplicar compensación de ganancia
    float filtered_output = lp_output * g_bandpass_filter.gain_compensation;
    
    // Incrementar contador de muestras
    g_bandpass_filter.sample_count++;
    
    // Saturación suave para evitar clipping
    if (filtered_output > 1.0f) filtered_output = 1.0f;
    if (filtered_output < 0.0f) filtered_output = 0.0f;
    
    return filtered_output;
}
```

## 📝 Plan de Implementación Detallado

### Paso 1: Modificar edrumulus_detection.h
```c
// Agregar al final del archivo, antes de #endif

// === FILTRO DE BANDA PASANTE ===
// Estructuras y funciones para filtrado digital

// [Insertar todas las estructuras definidas arriba]

// Funciones públicas del filtro
esp_err_t edrumulus_detection_filter_init(void);
esp_err_t edrumulus_detection_filter_deinit(void);
float edrumulus_detection_filter_process(float input_sample);
void edrumulus_detection_filter_reset(void);
esp_err_t edrumulus_detection_filter_set_config(const edrumulus_filter_config_t* config);
esp_err_t edrumulus_detection_filter_get_config(edrumulus_filter_config_t* config);
bool edrumulus_detection_filter_is_enabled(void);
```

### Paso 2: Implementar en edrumulus_detection.c
```c
// === VARIABLES GLOBALES DEL FILTRO ===
static edrumulus_bandpass_filter_t g_bandpass_filter = {0};
static edrumulus_filter_config_t g_filter_config = {
    .filter_enabled = true,
    .low_cutoff_hz = EDRUMULUS_FILTER_LOW_CUTOFF,
    .high_cutoff_hz = EDRUMULUS_FILTER_HIGH_CUTOFF,
    .gain_compensation = 2.0f,  // Compensar atenuación del filtro
    .filter_order = EDRUMULUS_FILTER_ORDER
};

// === FUNCIONES PRIVADAS ===
// [Insertar calculate_butterworth_coeffs y process_biquad]

// === FUNCIONES PÚBLICAS ===
esp_err_t edrumulus_detection_filter_init(void) {
    ESP_LOGI(TAG, "Inicializando filtro de banda pasante %d-%d Hz",
             (int)g_filter_config.low_cutoff_hz,
             (int)g_filter_config.high_cutoff_hz);
    
    // Calcular coeficientes del filtro pasa altos
    calculate_butterworth_coeffs(&g_bandpass_filter.high_pass,
                                g_filter_config.low_cutoff_hz,
                                EDRUMULUS_FILTER_SAMPLE_RATE,
                                EDRUMULUS_FILTER_HIGH_PASS);
    
    // Calcular coeficientes del filtro pasa bajos
    calculate_butterworth_coeffs(&g_bandpass_filter.low_pass,
                                g_filter_config.high_cutoff_hz,
                                EDRUMULUS_FILTER_SAMPLE_RATE,
                                EDRUMULUS_FILTER_LOW_PASS);
    
    // Inicializar estado
    memset(&g_bandpass_filter.hp_state, 0, sizeof(g_bandpass_filter.hp_state));
    memset(&g_bandpass_filter.lp_state, 0, sizeof(g_bandpass_filter.lp_state));
    
    // Configurar parámetros
    g_bandpass_filter.enabled = g_filter_config.filter_enabled;
    g_bandpass_filter.gain_compensation = g_filter_config.gain_compensation;
    g_bandpass_filter.sample_count = 0;
    
    ESP_LOGI(TAG, "Filtro inicializado exitosamente");
    return ESP_OK;
}

void edrumulus_detection_filter_reset(void) {
    memset(&g_bandpass_filter.hp_state, 0, sizeof(g_bandpass_filter.hp_state));
    memset(&g_bandpass_filter.lp_state, 0, sizeof(g_bandpass_filter.lp_state));
    g_bandpass_filter.sample_count = 0;
    ESP_LOGI(TAG, "Estado del filtro reiniciado");
}

// [Insertar edrumulus_detection_filter_process]

bool edrumulus_detection_filter_is_enabled(void) {
    return g_bandpass_filter.enabled;
}
```

### Paso 3: Modificar piezo_monitor_task()
```c
void piezo_monitor_task(void *pvParameters) {
    ESP_LOGI(TAG, "Iniciando tarea de monitoreo de piezo con filtro");
    
    while (1) {
        // Leer valor ADC crudo
        uint32_t raw_adc = edrumulus_input_read_adc();
        
        // Convertir a muestra normalizada (0.0 - 1.0)
        float normalized_sample = (float)raw_adc / (float)ADC_MAX_VALUE;
        
        // Aplicar filtro de banda pasante
        float filtered_sample = edrumulus_detection_filter_process(normalized_sample);
        
        // Convertir de vuelta a valor ADC para compatibilidad
        uint32_t filtered_adc = (uint32_t)(filtered_sample * ADC_MAX_VALUE);
        
        // Verificar si hay un hit usando valor filtrado
        if (edrumulus_detection_check_hit(filtered_adc)) {
            // Calcular velocidad basada en valor filtrado
            uint8_t velocity = edrumulus_detection_calculate_velocity(filtered_adc);
            
            // Verificar período de mask time
            if (!edrumulus_detection_is_in_mask_period()) {
                // Crear evento de hit
                edrumulus_hit_event_t hit_event = {
                    .pad_id = 0,
                    .velocity = velocity,
                    .timestamp = xTaskGetTickCount()
                };
                
                // Enviar evento a la cola
                if (xQueueSend(hit_event_queue, &hit_event, 0) == pdTRUE) {
                    ESP_LOGI(TAG, "Hit detectado: velocity=%d (filtrado), ADC=%lu->%lu",
                             velocity, raw_adc, filtered_adc);
                    
                    // Activar mask time adaptativo
                    edrumulus_detection_start_mask_time(velocity);
                } else {
                    ESP_LOGW(TAG, "Cola de eventos llena, hit descartado");
                }
            } else {
                ESP_LOGD(TAG, "Hit ignorado por mask time: velocity=%d", velocity);
            }
        }
        
        // Delay de 1ms para mantener frecuencia de muestreo
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}
```

### Paso 4: Modificar edrumulus_detection_init()
```c
esp_err_t edrumulus_detection_init(void) {
    ESP_LOGI(TAG, "Inicializando subsistema de detección con filtro");
    
    // Inicializar ADC
    esp_err_t ret = edrumulus_detection_adc_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Error inicializando ADC: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Inicializar filtro de banda pasante
    ret = edrumulus_detection_filter_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Error inicializando filtro: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Inicializar configuración de mask time
    edrumulus_mask_time_config_t mask_config = {
        .mask_time_ms = 10,
        .adaptive_mask = true,
        .velocity_threshold_low = 20,
        .velocity_threshold_high = 80
    };
    edrumulus_detection_set_mask_time_config(&mask_config);
    
    // Crear cola de eventos
    hit_event_queue = xQueueCreate(10, sizeof(edrumulus_hit_event_t));
    if (hit_event_queue == NULL) {
        ESP_LOGE(TAG, "Error creando cola de eventos");
        return ESP_ERR_NO_MEM;
    }
    
    ESP_LOGI(TAG, "Subsistema de detección inicializado exitosamente");
    return ESP_OK;
}
```

## 🧪 Plan de Testing

### Pruebas de Funcionalidad
1. **Test de filtrado básico**:
   ```c
   // Inyectar señal de prueba conocida
   float test_signal[] = {0.0, 0.5, 1.0, 0.5, 0.0};
   for (int i = 0; i < 5; i++) {
       float filtered = edrumulus_detection_filter_process(test_signal[i]);
       ESP_LOGI(TAG, "Input: %.3f -> Output: %.3f", test_signal[i], filtered);
   }
   ```

2. **Test de respuesta en frecuencia**:
   - Generar tonos de 20Hz, 40Hz, 100Hz, 400Hz, 800Hz
   - Verificar atenuación correcta fuera de banda

3. **Test de latencia**:
   - Medir tiempo de procesamiento por muestra
   - Objetivo: <50μs por muestra

### Métricas de Validación
```c
// Estructura para métricas de rendimiento
typedef struct {
    uint32_t total_hits;
    uint32_t spurious_hits;
    uint32_t valid_hits;
    float spurious_ratio;
    uint32_t avg_processing_time_us;
    uint32_t max_processing_time_us;
} filter_performance_metrics_t;
```

## 📊 Resultados Esperados

### Antes del Filtro (Estado Actual)
- **Hits por golpe real**: 4-6 detecciones
- **Velocidad real**: 118
- **Velocidades espurias**: 10 (múltiples)
- **Precisión**: ~20% (1 real de 5 total)

### Después del Filtro (Objetivo)
- **Hits por golpe real**: 1.1 detecciones
- **Velocidad real**: 115-120 (ligeramente filtrada)
- **Velocidades espurias**: <0.1 por golpe
- **Precisión**: >95% (1 real de 1.1 total)
- **Latencia adicional**: <0.5ms

## 🔧 Configuración y Ajustes

### Parámetros Ajustables en Tiempo Real
```c
// Comando para ajustar filtro via encoder
void adjust_filter_cutoffs(int encoder_delta) {
    static float low_cutoff = 40.0f;
    static float high_cutoff = 400.0f;
    
    if (encoder_delta > 0) {
        high_cutoff += 10.0f;  // Aumentar banda pasante
        if (high_cutoff > 1000.0f) high_cutoff = 1000.0f;
    } else {
        high_cutoff -= 10.0f;  // Reducir banda pasante
        if (high_cutoff < 100.0f) high_cutoff = 100.0f;
    }
    
    // Reconfigurar filtro
    edrumulus_filter_config_t new_config = g_filter_config;
    new_config.high_cutoff_hz = high_cutoff;
    edrumulus_detection_filter_set_config(&new_config);
    
    ESP_LOGI(TAG, "Filtro ajustado: %d-%d Hz", (int)low_cutoff, (int)high_cutoff);
}
```

## 🚀 Próximos Pasos

Una vez completada la Fase 2:
1. **Compilar y flashear** el sistema
2. **Probar detección** con diferentes intensidades
3. **Medir métricas** de rendimiento
4. **Ajustar parámetros** según resultados
5. **Proceder a Fase 3**: Detección de picos avanzada

---
*Documento técnico para implementación de Fase 2 - Filtro de Banda Pasante*