# Phase 3: Detección Avanzada de Picos y Cancelación Sofisticada de Retriggering

## 🎯 Objetivo

Eliminar rebotes mecánicos del piezo que causan múltiples detecciones de un solo golpe físico. Los logs muestran detecciones con 5ms de separación (velocidades 51 y 92) seguidas de hits espurios (velocity=10), indicando rebotes mecánicos que el filtro de banda pasante actual no elimina completamente.

## 📊 Análisis del Problema

### Características de los Rebotes Mecánicos
- **Timing:** 5ms entre detecciones (demasiado rápido para ser golpes humanos separados)
- **Patrón de Velocidad:** Golpe principal (51-92) seguido de rebotes débiles (velocity=10)
- **Decaimiento:** Los rebotes tienen menor amplitud que el golpe original
- **Frecuencia:** Rebotes en frecuencias más altas que el golpe fundamental

### Limitaciones del Sistema Actual
- Filtro de banda pasante (40-400Hz) no elimina rebotes en el rango de frecuencia válido
- Mask time adaptativo (2-10ms) insuficiente para rebotes de 5ms
- Falta análisis de características de señal para distinguir golpes reales vs rebotes

## 🔬 Algoritmos de Phase 3

### 1. Detección Avanzada de Picos

#### Análisis de Flancos (Rising/Falling Edge)
```c
typedef struct {
    float current_value;
    float previous_value;
    float peak_value;
    uint32_t peak_timestamp;
    bool rising_edge_detected;
    bool falling_edge_detected;
    float rise_rate;        // Velocidad de subida (V/ms)
    float fall_rate;        // Velocidad de bajada (V/ms)
} edrumulus_edge_detector_t;
```

**Características de Golpes Reales vs Rebotes:**
- **Golpe Real:** Rise rate alto (>0.1 V/ms), fall rate moderado
- **Rebote:** Rise rate bajo (<0.05 V/ms), fall rate rápido
- **Duración:** Golpe real >10ms, rebote <5ms

#### Algoritmo de Detección
1. **Detección de Flanco Ascendente:** `current > previous + threshold_rise`
2. **Análisis de Velocidad de Subida:** `rise_rate = (peak - start) / time_to_peak`
3. **Validación de Pico:** Verificar que el pico se mantiene >2ms
4. **Detección de Flanco Descendente:** `current < peak * 0.7`
5. **Análisis de Decaimiento:** Verificar patrón de decaimiento exponencial

### 2. Análisis de Decaimiento de Señal

#### Modelo de Decaimiento Exponencial
```c
typedef struct {
    float decay_constant;   // Constante de decaimiento (λ)
    float initial_amplitude;
    uint32_t decay_start_time;
    float expected_value;   // Valor esperado según modelo
    float deviation;        // Desviación del modelo
} edrumulus_decay_analyzer_t;
```

**Modelo Matemático:**
- **Golpe Real:** `V(t) = V₀ * e^(-λt)` donde λ ≈ 0.1-0.3 ms⁻¹
- **Rebote:** No sigue modelo exponencial, tiene picos secundarios

#### Algoritmo de Validación
1. **Captura de Decaimiento:** Registrar 20ms después del pico
2. **Ajuste de Modelo:** Calcular λ usando mínimos cuadrados
3. **Validación:** Si R² > 0.85, es golpe real; si R² < 0.6, es rebote
4. **Predicción:** Usar modelo para predecir valores futuros

### 3. Validación de Velocidad Inteligente

#### Filtro de Velocidades Espurias
```c
typedef struct {
    uint8_t min_valid_velocity;     // Velocidad mínima válida (15)
    uint8_t velocity_history[5];    // Historial de velocidades
    float velocity_consistency;     // Consistencia de velocidades
    bool velocity_jump_detected;    // Salto anómalo de velocidad
} edrumulus_velocity_validator_t;
```

**Reglas de Validación:**
- **Velocidad Mínima:** velocity < 15 → automáticamente rebote
- **Consistencia Temporal:** Variación >50% en <10ms → rebote
- **Patrón de Decaimiento:** Velocidades deben decrecer monotónicamente
- **Saltos Anómalos:** Incremento >20% después de golpe → rebote

### 4. Ajuste Dinámico de Threshold

#### Threshold Adaptativo
```c
typedef struct {
    uint16_t base_threshold;        // Threshold base
    float noise_floor;              // Nivel de ruido medido
    float signal_to_noise_ratio;    // SNR actual
    uint16_t adaptive_threshold;    // Threshold calculado dinámicamente
    uint32_t last_adjustment_time;  // Última vez que se ajustó
} edrumulus_adaptive_threshold_t;
```

**Algoritmo de Ajuste:**
1. **Medición de Ruido:** Calcular RMS durante períodos silenciosos
2. **Cálculo de SNR:** `SNR = 20 * log10(signal_peak / noise_rms)`
3. **Ajuste de Threshold:** `threshold = noise_floor * (2 + SNR/10)`
4. **Límites:** Mantener threshold entre 50-500 (ADC units)

## 🏗️ Estructuras de Datos

### Detector de Rebotes Principal
```c
typedef struct {
    edrumulus_edge_detector_t edge_detector;
    edrumulus_decay_analyzer_t decay_analyzer;
    edrumulus_velocity_validator_t velocity_validator;
    edrumulus_adaptive_threshold_t adaptive_threshold;
    
    // Buffer circular para análisis temporal
    float signal_buffer[50];        // 50ms de historia a 1kHz
    uint8_t buffer_index;
    
    // Estado del detector
    bool hit_in_progress;
    uint32_t hit_start_time;
    float hit_peak_value;
    uint8_t hit_velocity;
    
    // Estadísticas
    uint32_t total_hits_detected;
    uint32_t rebounds_rejected;
    float rejection_rate;
} edrumulus_rebound_detector_t;
```

## 🔧 Implementación

### Funciones Principales

1. **`edrumulus_rebound_detector_init()`**
   - Inicializar todas las estructuras
   - Configurar parámetros por defecto
   - Resetear buffers y estadísticas

2. **`edrumulus_rebound_detector_process(float signal)`**
   - Procesar muestra de señal
   - Ejecutar análisis de flancos
   - Validar usando todos los algoritmos
   - Retornar true solo para golpes válidos

3. **`edrumulus_rebound_detector_is_valid_hit()`**
   - Combinar resultados de todos los análisis
   - Aplicar lógica de decisión
   - Actualizar estadísticas

4. **`edrumulus_rebound_detector_get_statistics()`**
   - Retornar estadísticas de rendimiento
   - Tasa de rechazo de rebotes
   - Efectividad del algoritmo

### Integración con Sistema Existente

```c
// En edrumulus_detection_check_piezo_hit()
bool edrumulus_detection_check_piezo_hit(uint8_t channel, uint8_t *velocity)
{
    // ... código existente ...
    
    // Aplicar filtro de banda pasante (Phase 2)
    float filtered_output = edrumulus_detection_filter_process(normalized_input);
    
    // NUEVO: Aplicar detector de rebotes (Phase 3)
    if (!edrumulus_rebound_detector_process(filtered_output)) {
        return false;  // Rebote detectado, rechazar
    }
    
    // Continuar con detección normal solo si pasa validación
    if (filtered_adc_value > g_piezo_threshold) {
        // ... resto del código ...
    }
}
```

## 📈 Métricas de Éxito

### Objetivos de Rendimiento
- **Eliminación de Rebotes:** >98% de rebotes mecánicos eliminados
- **Preservación de Golpes Reales:** >99% de golpes válidos detectados
- **Latencia Adicional:** <1ms de procesamiento adicional
- **Falsos Positivos:** <1% de golpes reales rechazados
- **Falsos Negativos:** <2% de rebotes no detectados

### Casos de Prueba
1. **Golpe Simple:** Un golpe debe generar una sola detección
2. **Golpes Rápidos:** Golpes separados >20ms deben detectarse individualmente
3. **Golpes Suaves:** Velocity >15 debe detectarse correctamente
4. **Golpes Fuertes:** No debe generar múltiples detecciones
5. **Ruido de Fondo:** No debe generar falsas detecciones

## 🚀 Plan de Implementación

### Fase 3.1: Estructuras Base
1. Definir estructuras en `edrumulus_detection.h`
2. Implementar funciones de inicialización
3. Crear buffer circular para análisis temporal

### Fase 3.2: Algoritmos Core
1. Implementar detector de flancos
2. Desarrollar analizador de decaimiento
3. Crear validador de velocidad

### Fase 3.3: Integración
1. Integrar con sistema de detección existente
2. Ajustar parámetros para optimización
3. Implementar logging para debug

### Fase 3.4: Validación
1. Compilar y flashear firmware
2. Probar con diferentes tipos de golpes
3. Medir métricas de rendimiento
4. Ajustar parámetros según resultados

## 🔍 Configuración y Debug

### Parámetros Ajustables
```c
#define EDRUMULUS_REBOUND_MIN_RISE_RATE     0.05f   // V/ms
#define EDRUMULUS_REBOUND_MIN_PEAK_DURATION 2       // ms
#define EDRUMULUS_REBOUND_MIN_VELOCITY      15      // MIDI velocity
#define EDRUMULUS_REBOUND_DECAY_R_SQUARED   0.85f   // Correlación mínima
#define EDRUMULUS_REBOUND_MAX_VELOCITY_JUMP 0.5f    // 50% máximo
```

### Logging de Debug
- Registrar características de cada señal detectada
- Mostrar razón de rechazo para rebotes
- Estadísticas en tiempo real de efectividad
- Gráficas de señal para análisis offline

Esta implementación debería eliminar definitivamente los rebotes mecánicos del piezo, permitiendo detección precisa de golpes individuales con latencia ultra-baja.