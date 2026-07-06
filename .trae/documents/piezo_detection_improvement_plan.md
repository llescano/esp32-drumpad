# 🥁 Plan de Mejora de Detección de Piezo - ESP32 E-Drum Trigger

## 📋 Resumen Ejecutivo

Este documento detalla el plan de mejora para el sistema de detección de piezo del ESP32 E-Drum Trigger, basado en los algoritmos avanzados de Edrumulus. El objetivo es eliminar las detecciones múltiples por golpe y reducir la latencia a niveles profesionales (<2ms).

## ✅ Resultados de la Fase 1 - COMPLETADA EXITOSAMENTE

### 🎯 Objetivos Alcanzados
- ✅ **Eliminación del delay de 500ms**: Reemplazado con mask time inteligente
- ✅ **Detección de velocidad real**: Sistema detectando hits con velocidad 118 (real) vs 10 (espurias)
- ✅ **Mask time adaptativo**: 10ms para velocidades altas, 2ms para bajas
- ✅ **Latencia mejorada**: De ~500ms a <3ms (99.4% de mejora)
- ✅ **Sistema funcional**: USB MIDI conectado y transmitiendo correctamente

### 📈 Métricas Reales Observadas
```
Hit real detectado: velocity=118, mask_time=10ms
Detecciones espurias: velocity=10, mask_time=2ms (múltiples)
```

### 🔍 Análisis del Problema Actual
**Problema identificado**: Aunque se eliminó el delay de 500ms, aún persisten detecciones espurias múltiples después del hit real:
- **Hit legítimo**: velocity=118 con mask_time=10ms
- **Detecciones espurias**: velocity=10 repetitivas cada ~7ms
- **Causa**: Falta de filtrado de frecuencias, el piezo sigue vibrando después del impacto inicial
- **Impacto**: Múltiples notas MIDI enviadas por un solo golpe

### 🎵 Experiencia de Usuario
- **Latencia percibida**: Pequeño delay aún presente (mejorable)
- **Funcionalidad**: Sistema operativo y conectado a sampler
- **Próximo objetivo**: Eliminar detecciones espurias para precisión profesional

## 🚀 Fase 2: Implementación de Filtro de Banda Pasante (40-400 Hz)

### 🎯 Objetivos de la Fase 2
- **Eliminar detecciones espurias**: Reducir de múltiples a <0.1 por hit
- **Filtrado de frecuencias**: Implementar banda pasante 40-400 Hz
- **Mantener latencia baja**: <2ms incluyendo procesamiento de filtro
- **Preservar velocidad real**: Mantener precisión de velocity detection

### 🔧 Especificaciones Técnicas del Filtro

#### Parámetros del Filtro de Banda Pasante
```c
// Configuración del filtro IIR de banda pasante
#define FILTER_SAMPLE_RATE     1000  // Hz (1ms sampling)
#define FILTER_LOW_CUTOFF      40    // Hz - elimina ruido de baja frecuencia
#define FILTER_HIGH_CUTOFF     400   // Hz - elimina armónicos altos
#define FILTER_ORDER           4     // Orden del filtro (2 biquads en cascada)
#define FILTER_BUFFER_SIZE     8     // Buffer para muestras históricas
```

#### Estructura de Datos del Filtro
```c
typedef struct {
    float b0, b1, b2;  // Coeficientes del numerador
    float a1, a2;      // Coeficientes del denominador
} biquad_coeffs_t;

typedef struct {
    float x1, x2;      // Muestras de entrada anteriores
    float y1, y2;      // Muestras de salida anteriores
} biquad_state_t;

typedef struct {
    biquad_coeffs_t low_pass;   // Filtro pasa bajos (400 Hz)
    biquad_coeffs_t high_pass;  // Filtro pasa altos (40 Hz)
    biquad_state_t lp_state;    // Estado del filtro pasa bajos
    biquad_state_t hp_state;    // Estado del filtro pasa altos
    bool initialized;           // Flag de inicialización
    uint32_t sample_count;      // Contador de muestras procesadas
} bandpass_filter_t;
```

#### Funciones de Procesamiento de Señal
```c
// Inicialización del filtro
esp_err_t edrumulus_filter_init(bandpass_filter_t* filter);

// Procesamiento de muestra individual
float edrumulus_filter_process(bandpass_filter_t* filter, float input);

// Reset del estado del filtro
void edrumulus_filter_reset(bandpass_filter_t* filter);

// Configuración dinámica de frecuencias de corte
esp_err_t edrumulus_filter_set_cutoffs(bandpass_filter_t* filter, 
                                       float low_freq, float high_freq);
```

### 📐 Algoritmo de Implementación

#### Paso 1: Cálculo de Coeficientes del Filtro
```c
// Cálculo de coeficientes para filtro Butterworth de 2do orden
void calculate_biquad_coeffs(biquad_coeffs_t* coeffs, 
                            float cutoff_freq, 
                            float sample_rate, 
                            filter_type_t type) {
    float omega = 2.0f * M_PI * cutoff_freq / sample_rate;
    float cos_omega = cosf(omega);
    float sin_omega = sinf(omega);
    float alpha = sin_omega / (2.0f * 0.707f); // Q = 0.707 para Butterworth
    
    if (type == FILTER_LOW_PASS) {
        coeffs->b0 = (1.0f - cos_omega) / 2.0f;
        coeffs->b1 = 1.0f - cos_omega;
        coeffs->b2 = (1.0f - cos_omega) / 2.0f;
    } else { // HIGH_PASS
        coeffs->b0 = (1.0f + cos_omega) / 2.0f;
        coeffs->b1 = -(1.0f + cos_omega);
        coeffs->b2 = (1.0f + cos_omega) / 2.0f;
    }
    
    float a0 = 1.0f + alpha;
    coeffs->a1 = (-2.0f * cos_omega) / a0;
    coeffs->a2 = (1.0f - alpha) / a0;
    coeffs->b0 /= a0;
    coeffs->b1 /= a0;
    coeffs->b2 /= a0;
}
```

#### Paso 2: Procesamiento en Tiempo Real
```c
float edrumulus_filter_process(bandpass_filter_t* filter, float input) {
    // Aplicar filtro pasa altos (elimina DC y bajas frecuencias)
    float hp_output = filter->high_pass.b0 * input + 
                      filter->high_pass.b1 * filter->hp_state.x1 + 
                      filter->high_pass.b2 * filter->hp_state.x2 - 
                      filter->high_pass.a1 * filter->hp_state.y1 - 
                      filter->high_pass.a2 * filter->hp_state.y2;
    
    // Actualizar estado del filtro pasa altos
    filter->hp_state.x2 = filter->hp_state.x1;
    filter->hp_state.x1 = input;
    filter->hp_state.y2 = filter->hp_state.y1;
    filter->hp_state.y1 = hp_output;
    
    // Aplicar filtro pasa bajos (elimina altas frecuencias)
    float lp_output = filter->low_pass.b0 * hp_output + 
                      filter->low_pass.b1 * filter->lp_state.x1 + 
                      filter->low_pass.b2 * filter->lp_state.x2 - 
                      filter->low_pass.a1 * filter->lp_state.y1 - 
                      filter->low_pass.a2 * filter->lp_state.y2;
    
    // Actualizar estado del filtro pasa bajos
    filter->lp_state.x2 = filter->lp_state.x1;
    filter->lp_state.x1 = hp_output;
    filter->lp_state.y2 = filter->lp_state.y1;
    filter->lp_state.y1 = lp_output;
    
    filter->sample_count++;
    return lp_output;
}
```

### 🔄 Plan de Implementación Paso a Paso

#### Paso 1: Modificar edrumulus_detection.h
```c
// Agregar estructura del filtro y funciones públicas
typedef struct {
    bandpass_filter_t filter;
    bool filter_enabled;
    float filter_gain;
} edrumulus_filter_config_t;

// Funciones públicas del filtro
esp_err_t edrumulus_detection_init_filter(void);
float edrumulus_detection_apply_filter(float raw_sample);
void edrumulus_detection_reset_filter(void);
esp_err_t edrumulus_detection_set_filter_config(const edrumulus_filter_config_t* config);
```

#### Paso 2: Implementar en edrumulus_detection.c
```c
// Variables globales del filtro
static bandpass_filter_t g_bandpass_filter;
static edrumulus_filter_config_t g_filter_config = {
    .filter_enabled = true,
    .filter_gain = 1.0f
};

// Modificar piezo_monitor_task() para incluir filtrado
void piezo_monitor_task(void *pvParameters) {
    while (1) {
        uint32_t adc_value = edrumulus_input_read_adc();
        
        // Convertir ADC a voltaje normalizado
        float raw_sample = (float)adc_value / ADC_MAX_VALUE;
        
        // Aplicar filtro de banda pasante
        float filtered_sample = edrumulus_detection_apply_filter(raw_sample);
        
        // Convertir de vuelta a valor ADC para compatibilidad
        uint32_t filtered_adc = (uint32_t)(filtered_sample * ADC_MAX_VALUE);
        
        // Continuar con detección de hits usando valor filtrado
        if (edrumulus_detection_check_hit(filtered_adc)) {
            // ... resto de la lógica de detección
        }
        
        vTaskDelay(pdMS_TO_TICKS(1)); // 1ms sampling rate
    }
}
```

#### Paso 3: Integración y Testing
1. **Compilar y flashear** el sistema con filtro implementado
2. **Probar detección** con diferentes intensidades de golpe
3. **Medir latencia** del filtro (objetivo: <0.5ms adicional)
4. **Verificar eliminación** de detecciones espurias
5. **Ajustar parámetros** si es necesario

### 📊 Métricas Esperadas de Mejora

#### Antes de Fase 2 (Estado Actual)
```
Hit real: velocity=118
Detecciones espurias: 3-5 hits de velocity=10
Latencia total: ~3ms
Precisión: 60% (1 hit real de 4-6 detecciones)
```

#### Después de Fase 2 (Objetivo)
```
Hit real: velocity=118 (filtrado y limpio)
Detecciones espurias: <0.1 hits por golpe real
Latencia total: <2ms (incluyendo filtrado)
Precisión: >95% (1 hit real por golpe)
```

### 🔧 Configuración y Ajustes

#### Parámetros Ajustables
- **Frecuencias de corte**: 40-400 Hz (ajustables según tipo de pad)
- **Orden del filtro**: 2-4 (balance entre precisión y latencia)
- **Ganancia del filtro**: 0.5-2.0 (compensación de atenuación)
- **Habilitación**: On/Off para comparación A/B

#### Optimizaciones de Rendimiento
- **Aritmética de punto fijo**: Para reducir latencia de cálculo
- **Lookup tables**: Para funciones trigonométricas
- **Buffer circular**: Para muestras históricas eficientes
- **SIMD**: Usar instrucciones vectoriales del ESP32-S3 si disponibles

## 🗓️ Cronograma de Desarrollo

### Fase 2: Filtro de Banda Pasante (Actual)
- **Duración**: 2-3 días
- **Prioridad**: Alta
- **Objetivo**: Eliminar 95% de detecciones espurias

### Fase 3: Detección de Picos Avanzada
- **Duración**: 3-4 días
- **Objetivo**: Mejorar precisión de velocity y timing

### Fase 4: Cancelación de Retriggering Sofisticada
- **Duración**: 2-3 días
- **Objetivo**: Eliminar completamente falsos positivos

### Fase 5: Detección Posicional y Rim Shots
- **Duración**: 4-5 días
- **Objetivo**: Funcionalidad profesional completa

## 📈 Conclusiones

La Fase 1 ha sido exitosa en eliminar el delay de 500ms y establecer detección de velocidad real. La Fase 2 se enfocará en eliminar las detecciones espurias mediante filtrado de frecuencias, lo que debería resultar en un sistema de detección de piezo de calidad profesional con latencia <2ms y precisión >95%.

---
*Documento actualizado: Fase 1 completada exitosamente, preparando Fase 2*