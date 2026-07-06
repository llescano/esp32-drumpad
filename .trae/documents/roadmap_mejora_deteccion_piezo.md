# Roadmap de Mejora - Detección de Sensor Piezo

## 1. Análisis del Problema Actual

### 1.1 Situación Actual
El sistema actual presenta múltiples detecciones por cada golpe en el sensor piezo debido a:

- **Algoritmo simplificado**: Solo compara valor ADC contra umbral fijo
- **Delay artificial**: Usa `vTaskDelay(500ms)` para evitar rebotes
- **Falta de filtrado**: No implementa filtros pasa-banda para ruido
- **Sin cancelación de retriggering**: No considera la curva de decaimiento natural
- **Detección de pico básica**: No identifica el pico principal vs. oscilaciones

### 1.2 Problemas Identificados
1. **Múltiples triggers por golpe**: Un golpe genera 3-5 detecciones
2. **Latencia artificial**: 500ms de delay reduce responsividad
3. **Medición de intensidad imprecisa**: Basada solo en valor ADC instantáneo
4. **Sensibilidad al ruido**: ADC captura ruido de alta frecuencia
5. **Falta de adaptabilidad**: No se ajusta a diferentes tipos de golpe

## 2. Algoritmos Avanzados de Edrumulus a Implementar

### 2.1 Filtro Pasa-Banda (40Hz - 400Hz)
**Propósito**: Filtrar ruido ADC y mejorar detección de picos

**Implementación**:
- Filtro IIR de segundo orden
- Frecuencias de corte: 40Hz (elimina ruido bajo) y 400Hz (elimina ruido alto)
- Delay del filtro: <2ms (compensable con FIFO)

**Beneficios**:
- Reduce falsos positivos por ruido
- Mejora definición de picos principales
- Mantiene latencia baja

### 2.2 Detección de Pico Principal
**Propósito**: Identificar el primer pico significativo vs. oscilaciones

**Algoritmo**:
1. **Pre-scan time**: Búsqueda retrospectiva en FIFO
2. **Scan time**: Ventana de análisis post-detección
3. **Mask time**: Período de supresión post-pico

**Parámetros**:
- Pre-scan: 5ms antes del trigger
- Scan: 10ms después del trigger
- Mask: 20ms de supresión

### 2.3 Cancelación de Retriggering
**Propósito**: Sustraer curva de decaimiento conocida

**Implementación**:
- Modelo exponencial de decaimiento: `y(t) = A * e^(-t/τ)`
- Sustracción adaptativa basada en intensidad del golpe
- Ajuste por posición estimada del golpe

**Beneficios**:
- Elimina detecciones por vibración residual
- Permite press rolls rápidos
- Mejora detección de golpes consecutivos

### 2.4 Medición de Intensidad Mejorada
**Propósito**: Calcular velocity MIDI precisa

**Algoritmo**:
1. Detectar pico máximo en ventana de scan
2. Calcular área bajo la curva (energía total)
3. Aplicar curva de respuesta logarítmica
4. Compensar por posición estimada

## 3. Roadmap de Desarrollo por Fases

### Fase 1: Eliminación del Delay Artificial (Semana 1)
**Objetivo**: Remover `vTaskDelay(500ms)` e implementar detección básica mejorada

**Tareas**:
1. ✅ Eliminar delay de 500ms en `esp32-edrumulus.c`
2. Implementar ventana de mask time (20ms) en lugar de delay
3. Agregar contador de samples para timing preciso
4. Implementar FIFO circular para pre-scan

**Entregables**:
- Detección sin delay artificial
- Reducción de múltiples triggers del 80% al 40%
- Latencia <5ms garantizada

### Fase 2: Filtro Pasa-Banda (Semana 2)
**Objetivo**: Implementar filtrado de señal para reducir ruido

**Tareas**:
1. Diseñar filtro IIR Butterworth de 2do orden
2. Implementar filtro en `edrumulus_detection.c`
3. Agregar FIFO para compensación de delay
4. Calibrar frecuencias de corte (40Hz-400Hz)

**Entregables**:
- Filtro pasa-banda funcional
- Reducción de ruido >60%
- Mejora en definición de picos

### Fase 3: Detección de Pico Avanzada (Semana 3)
**Objetivo**: Implementar algoritmo de detección de pico principal

**Tareas**:
1. Implementar ventanas de pre-scan, scan y mask
2. Algoritmo de búsqueda de pico máximo
3. Validación de pico vs. ruido
4. Ajuste dinámico de umbrales

**Entregables**:
- Detección de pico único por golpe
- Reducción de múltiples triggers al <10%
- Mejora en consistencia de detección

### Fase 4: Cancelación de Retriggering (Semana 4)
**Objetivo**: Implementar sustracción de curva de decaimiento

**Tareas**:
1. Modelar curva de decaimiento exponencial
2. Implementar sustracción adaptativa
3. Calibrar parámetros de decaimiento
4. Optimizar para diferentes intensidades

**Entregables**:
- Cancelación de retriggering funcional
- Soporte para press rolls rápidos
- Detección limpia de golpes consecutivos

### Fase 5: Medición de Intensidad Precisa (Semana 5)
**Objetivo**: Implementar cálculo de velocity MIDI mejorado

**Tareas**:
1. Algoritmo de cálculo de energía total
2. Curva de respuesta logarítmica
3. Compensación por posición
4. Calibración con golpes reales

**Entregables**:
- Velocity MIDI precisa y consistente
- Rango dinámico completo (1-127)
- Respuesta natural a intensidad de golpe

## 4. Implementación Técnica Detallada

### 4.1 Estructura de Datos
```c
// Buffer circular para muestras ADC
typedef struct {
    int16_t samples[FIFO_SIZE];
    uint16_t write_index;
    uint16_t read_index;
    bool full;
} adc_fifo_t;

// Estado de detección
typedef struct {
    bool in_scan_window;
    bool in_mask_window;
    uint32_t scan_start_time;
    uint32_t mask_start_time;
    int16_t peak_value;
    uint16_t peak_position;
} detection_state_t;

// Parámetros de filtro
typedef struct {
    float b0, b1, b2;  // Coeficientes numerador
    float a1, a2;      // Coeficientes denominador
    float x1, x2;      // Muestras anteriores entrada
    float y1, y2;      // Muestras anteriores salida
} iir_filter_t;
```

### 4.2 Configuración de Sampling
```c
#define ADC_SAMPLE_RATE_HZ    8000    // 8kHz como Edrumulus original
#define FIFO_SIZE_MS          50      // 50ms de buffer
#define FIFO_SIZE             (ADC_SAMPLE_RATE_HZ * FIFO_SIZE_MS / 1000)
#define PRE_SCAN_MS           5       // Pre-scan window
#define SCAN_WINDOW_MS        10      // Scan window
#define MASK_TIME_MS          20      // Mask time
```

### 4.3 Algoritmo Principal
```c
bool edrumulus_detection_process_sample(int16_t adc_sample, uint8_t *velocity) {
    // 1. Agregar muestra al FIFO
    fifo_add_sample(&adc_fifo, adc_sample);
    
    // 2. Aplicar filtro pasa-banda
    float filtered = iir_bandpass_filter(&bp_filter, adc_sample);
    
    // 3. Aplicar cancelación de retriggering si está activa
    if (retrigger_active) {
        filtered -= calculate_decay_value();
    }
    
    // 4. Verificar umbral en señal filtrada
    if (!detection_state.in_mask_window && filtered > threshold) {
        start_detection_sequence();
        return false; // No enviar MIDI aún
    }
    
    // 5. Procesar ventana de scan
    if (detection_state.in_scan_window) {
        return process_scan_window(velocity);
    }
    
    return false;
}
```

## 5. Métricas de Éxito

### 5.1 Objetivos Cuantitativos
- **Múltiples detecciones**: Reducir de 3-5 por golpe a <1.1 por golpe
- **Latencia total**: Mantener <3ms (comparable a Roland TD-50)
- **Precisión de velocity**: ±5% de consistencia en golpes repetidos
- **Rango dinámico**: Utilizar 90% del rango MIDI (1-127)
- **Falsos positivos**: <1% en condiciones normales

### 5.2 Objetivos Cualitativos
- Respuesta natural y predecible
- Soporte para técnicas avanzadas (press rolls, ghost notes)
- Estabilidad en diferentes condiciones ambientales
- Facilidad de calibración para diferentes pads

## 6. Consideraciones de Implementación

### 6.1 Recursos del Sistema
- **CPU**: Algoritmos optimizados para ESP32-S3
- **Memoria**: Buffers circulares eficientes
- **Tiempo real**: Task de alta prioridad para ADC

### 6.2 Configurabilidad
- Parámetros ajustables via encoder rotativo
- Persistencia en NVS
- Perfiles para diferentes tipos de pad

### 6.3 Debug y Monitoreo
- Logging detallado de algoritmos
- Métricas de rendimiento
- Visualización de señales filtradas

---

**Próximo paso**: Iniciar Fase 1 con eliminación del delay de 500ms e implementación de mask time inteligente.