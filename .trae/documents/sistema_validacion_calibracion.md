# Sistema de Validación y Calibración - E-Drum ESP32

## 1. Introducción

Este documento establece un protocolo completo de validación y calibración para el sistema de detección de e-drums ESP32. El sistema permite validar cada etapa del algoritmo de detección mediante la correlación entre logs del sistema, capturas de osciloscopio y registros de datos, además de generar datos sintéticos para pruebas controladas.

## 2. Arquitectura de Validación

### 2.1 Fuentes de Datos

#### Logs del Sistema (ESP32)
- **Frecuencia**: Tiempo real (8kHz)
- **Contenido**: Valores ADC, resultados de filtros, decisiones de detección
- **Formato**: ESP_LOG con timestamps precisos
- **Ventajas**: Acceso directo a algoritmos internos

#### Capturas de Osciloscopio
- **Frecuencia**: 1MHz+ (alta resolución temporal)
- **Contenido**: Señal analógica cruda del piezo
- **Formato**: Pantalla visual + exportación CSV/TXT
- **Ventajas**: Verdad absoluta de la señal física

#### Registros de Datos
- **Frecuencia**: Configurable (1-10kHz)
- **Contenido**: Buffers de señal procesada por etapas
- **Formato**: Arrays numéricos exportables
- **Ventajas**: Análisis post-procesamiento detallado

### 2.2 Metodología de Correlación

```
Señal Física → [Osciloscopio] → Análisis Temporal
     ↓
ADC ESP32 → [Phase 1] → [Phase 2] → [Phase 3] → MIDI
     ↓           ↓           ↓           ↓
   Logs      Logs      Logs      Logs
     ↓           ↓           ↓           ↓
Validación  Validación  Validación  Validación
```

## 3. Protocolo de Validación por Etapas

### 3.1 Phase 1: Validación ADC y Muestreo

#### Objetivos
- Verificar precisión del ADC (12-bit)
- Validar frecuencia de muestreo (8kHz)
- Confirmar ausencia de aliasing

#### Procedimiento
1. **Señal de Prueba**: Generador de funciones (sine wave 100Hz, 1V)
2. **Captura Simultánea**:
   - Osciloscopio: Canal 1 → Señal generador
   - Osciloscopio: Canal 2 → Salida ADC ESP32
   - Logs ESP32: Valores ADC crudos

3. **Métricas de Validación**:
   - **Precisión**: Error < 1% entre osciloscopio y ADC
   - **Linealidad**: R² > 0.99 en rango 0-3.3V
   - **Ruido**: SNR > 60dB
   - **Frecuencia**: Desviación < 0.1% de 8kHz

#### Criterios de Aceptación
```c
// Logs esperados
D (timestamp) edrumulus_input: ADC Channel 0: raw=2048, voltage=1.65V
D (timestamp) edrumulus_input: Sample rate: 8000.2 Hz (deviation: 0.0025%)
D (timestamp) edrumulus_input: SNR measured: 62.3 dB
```

### 3.2 Phase 2: Validación Filtro Banda Pasante

#### Objetivos
- Verificar respuesta en frecuencia 40-400Hz
- Validar atenuación fuera de banda
- Confirmar estabilidad del filtro IIR

#### Procedimiento
1. **Barrido de Frecuencias**: 10Hz a 1kHz (pasos de 10Hz)
2. **Captura por Frecuencia**:
   - Osciloscopio: Señal entrada y salida del filtro
   - Logs ESP32: Valores antes y después del filtro

3. **Métricas de Validación**:
   - **Banda Pasante**: Atenuación < 3dB (40-400Hz)
   - **Banda Rechazada**: Atenuación > 20dB (<40Hz, >400Hz)
   - **Ripple**: < 1dB en banda pasante
   - **Estabilidad**: Sin oscilaciones o saturación

#### Datos Sintéticos
```c
// Generación de señal de prueba
float test_signal = amplitude * sin(2 * PI * frequency * t) + noise;
// Frecuencias de prueba: 20, 50, 100, 200, 300, 400, 500, 800 Hz
```

#### Criterios de Aceptación
```c
// Logs esperados
D (timestamp) edrumulus_detection: Filter input: 1.65V @ 100Hz
D (timestamp) edrumulus_detection: Filter output: 1.62V @ 100Hz (attenuation: -0.2dB)
D (timestamp) edrumulus_detection: Filter input: 1.65V @ 800Hz
D (timestamp) edrumulus_detection: Filter output: 0.16V @ 800Hz (attenuation: -20.3dB)
```

### 3.3 Phase 3: Validación Algoritmos Avanzados

#### 3.3.1 Edge Detector

**Objetivos**:
- Validar detección de rise/fall rate
- Verificar rechazo de rebotes mecánicos

**Procedimiento**:
1. **Golpe Real**: Stick en piezo
2. **Rebote Simulado**: Vibración mecánica
3. **Comparación**: Osciloscopio vs decisión algoritmo

**Métricas**:
- **Sensibilidad**: Detección > 95% golpes reales
- **Especificidad**: Rechazo > 90% rebotes
- **Rise Rate Threshold**: 0.050 V/ms

#### 3.3.2 Decay Analyzer

**Objetivos**:
- Validar modelo exponencial de decaimiento
- Verificar parámetros τ (tau) y A₀

**Procedimiento**:
1. **Captura Completa**: 500ms post-golpe
2. **Fitting Exponencial**: y = A₀ * e^(-t/τ)
3. **Correlación**: R² entre modelo y datos

**Métricas**:
- **Goodness of Fit**: R² > 0.85
- **Tau Range**: 50-200ms (típico piezo)
- **Amplitude Accuracy**: ±5% vs osciloscopio

#### 3.3.3 Velocity Validator

**Objetivos**:
- Verificar mapeo velocidad vs amplitud
- Validar threshold mínimo (15)

**Procedimiento**:
1. **Golpes Calibrados**: 10 niveles de fuerza
2. **Medición Referencia**: Acelerómetro o fuerza
3. **Correlación**: Velocidad MIDI vs fuerza real

**Métricas**:
- **Linealidad**: R² > 0.90
- **Rango Dinámico**: 15-127 MIDI
- **Repetibilidad**: σ < 5% para mismo golpe

#### 3.3.4 Adaptive Threshold

**Objetivos**:
- Validar adaptación a ruido ambiente
- Verificar cálculo SNR en tiempo real

**Procedimiento**:
1. **Ruido Variable**: 0.1V a 1V RMS
2. **Threshold Tracking**: Monitoreo continuo
3. **Respuesta Dinámica**: Tiempo de adaptación

**Métricas**:
- **SNR Target**: Mantener 20dB mínimo
- **Adaptation Time**: < 2 segundos
- **Stability**: ±10% variación threshold

## 4. Generación de Datos Sintéticos

### 4.1 Modelos de Señal

#### Golpe Real
```c
// Modelo matemático de golpe de piezo
float piezo_hit_model(float t, float amplitude, float frequency, float decay) {
    if (t < 0) return 0;
    
    // Componente principal: decaimiento exponencial modulado
    float envelope = amplitude * exp(-t / decay);
    
    // Frecuencia resonante del piezo
    float oscillation = sin(2 * PI * frequency * t);
    
    // Ruido realista
    float noise = 0.05 * amplitude * random_gaussian();
    
    return envelope * oscillation + noise;
}
```

#### Rebote Mecánico
```c
// Modelo de rebote: múltiples impactos decrecientes
float mechanical_bounce(float t, float initial_amp, int bounces) {
    float signal = 0;
    float bounce_interval = 0.002; // 2ms entre rebotes
    
    for (int i = 0; i < bounces; i++) {
        float bounce_time = i * bounce_interval;
        if (t >= bounce_time) {
            float amp = initial_amp * pow(0.6, i); // Decrecimiento 40%
            signal += piezo_hit_model(t - bounce_time, amp, 200, 0.01);
        }
    }
    return signal;
}
```

#### Ruido Ambiente
```c
// Ruido rosa + interferencias
float ambient_noise(float t, float level) {
    float pink_noise = pink_noise_generator(t);
    float power_hum = 0.1 * sin(2 * PI * 50 * t); // 50Hz mains
    float rf_interference = 0.05 * sin(2 * PI * 433e6 * t); // RF
    
    return level * (pink_noise + power_hum + rf_interference);
}
```

### 4.2 Casos de Prueba Sintéticos

#### Test Suite Completo
```c
typedef struct {
    char name[32];
    float duration_ms;
    float (*generator)(float t);
    bool expected_detection;
    int expected_velocity;
} synthetic_test_t;

synthetic_test_t test_cases[] = {
    {"clean_hit_soft", 100, clean_hit_50, true, 45},
    {"clean_hit_hard", 100, clean_hit_120, true, 115},
    {"double_bounce", 50, double_bounce, false, 0},
    {"noise_spike", 10, noise_spike, false, 0},
    {"low_velocity", 100, low_velocity_10, false, 0},
    {"rim_shot", 150, rim_shot, true, 85},
    {"cross_talk", 200, cross_talk, false, 0}
};
```

## 5. Procedimientos de Calibración

### 5.1 Calibración del Filtro

#### Parámetros Ajustables
```c
typedef struct {
    float low_cutoff;    // 40Hz nominal
    float high_cutoff;   // 400Hz nominal
    float q_factor;      // 0.707 Butterworth
    int filter_order;    // 2nd order
} filter_calibration_t;
```

#### Procedimiento
1. **Medición Respuesta**: Barrido 10Hz-1kHz
2. **Ajuste Automático**: Algoritmo de optimización
3. **Validación**: Verificar especificaciones
4. **Almacenamiento**: Guardar en NVS

### 5.2 Calibración Detector de Rebotes

#### Parámetros Críticos
```c
typedef struct {
    float rise_rate_threshold;    // 0.050 V/ms
    float fall_rate_threshold;    // 0.030 V/ms
    float min_peak_separation;    // 10ms
    float rebound_timeout;        // 50ms
} rebound_calibration_t;
```

#### Metodología
1. **Captura Rebotes Reales**: 100 muestras
2. **Análisis Estadístico**: Distribución rise/fall rates
3. **Threshold Optimization**: Minimizar falsos positivos/negativos
4. **Validación Cruzada**: 80/20 train/test split

### 5.3 Calibración Mapeo de Velocidad

#### Curva de Respuesta
```c
// Mapeo no-lineal para respuesta natural
int velocity_mapping(float amplitude) {
    // Curva logarítmica para sensación natural
    float normalized = (amplitude - min_threshold) / (max_amplitude - min_threshold);
    float velocity_float = 15 + 112 * pow(normalized, 0.7);
    return (int)clamp(velocity_float, 15, 127);
}
```

#### Procedimiento de Calibración
1. **Golpes de Referencia**: Medidor de fuerza calibrado
2. **Mapeo Inicial**: Función lineal
3. **Ajuste Perceptual**: Pruebas con músico
4. **Optimización**: Algoritmo genético o similar

## 6. Métricas de Validación

### 6.1 Métricas de Rendimiento

#### Latencia del Sistema
```c
// Medición timestamp to timestamp
typedef struct {
    uint64_t physical_hit_time;    // Osciloscopio trigger
    uint64_t adc_sample_time;      // Primera muestra > threshold
    uint64_t detection_time;       // Decisión final algoritmo
    uint64_t midi_output_time;     // Envío mensaje MIDI
} latency_measurement_t;

// Target: < 10ms total
```

#### Precisión de Detección
```c
typedef struct {
    int true_positives;     // Hits detectados correctamente
    int false_positives;    // Rebotes detectados como hits
    int true_negatives;     // Rebotes rechazados correctamente
    int false_negatives;    // Hits perdidos
} detection_metrics_t;

// Calcular: Sensitivity, Specificity, Precision, F1-Score
```

### 6.2 Criterios de Aceptación del Sistema

#### Rendimiento Mínimo
- **Latencia Total**: < 10ms (95th percentile)
- **Detección Sensitivity**: > 95%
- **Detección Specificity**: > 90%
- **Velocity Accuracy**: R² > 0.90 vs referencia
- **Stability**: < 1% drift en 1 hora operación

#### Robustez
- **Temperature Range**: 0-50°C sin degradación
- **Voltage Range**: 4.5-5.5V operación estable
- **EMI Immunity**: Funcional con RF hasta 10V/m

## 7. Herramientas de Análisis

### 7.1 Scripts de Análisis Python

#### Correlación Osciloscopio-Logs
```python
import numpy as np
import pandas as pd
import matplotlib.pyplot as plt
from scipy import signal

def correlate_oscilloscope_logs(osc_file, log_file):
    # Cargar datos osciloscopio (CSV)
    osc_data = pd.read_csv(osc_file)
    
    # Parsear logs ESP32
    log_data = parse_esp32_logs(log_file)
    
    # Sincronización temporal
    sync_offset = find_sync_offset(osc_data, log_data)
    
    # Correlación cruzada
    correlation = signal.correlate(osc_data['voltage'], 
                                 log_data['adc_voltage'])
    
    return correlation, sync_offset
```

#### Análisis de Respuesta en Frecuencia
```python
def analyze_filter_response(input_signal, output_signal, fs=8000):
    # FFT de entrada y salida
    freq_in, mag_in = signal.welch(input_signal, fs)
    freq_out, mag_out = signal.welch(output_signal, fs)
    
    # Función de transferencia
    H = mag_out / mag_in
    H_dB = 20 * np.log10(H)
    
    # Verificar especificaciones
    passband_mask = (freq_in >= 40) & (freq_in <= 400)
    stopband_mask = (freq_in < 40) | (freq_in > 400)
    
    passband_ripple = np.max(H_dB[passband_mask]) - np.min(H_dB[passband_mask])
    stopband_attenuation = np.max(H_dB[stopband_mask])
    
    return {
        'frequency': freq_in,
        'magnitude_db': H_dB,
        'passband_ripple': passband_ripple,
        'stopband_attenuation': stopband_attenuation
    }
```

### 7.2 Dashboard de Validación

#### Interfaz Web en Tiempo Real
```html
<!DOCTYPE html>
<html>
<head>
    <title>E-Drum Validation Dashboard</title>
    <script src="https://cdn.plot.ly/plotly-latest.min.js"></script>
</head>
<body>
    <div id="signal-plot"></div>
    <div id="metrics-table"></div>
    <div id="status-indicators"></div>
    
    <script>
        // WebSocket connection to ESP32
        const ws = new WebSocket('ws://192.168.1.100:8080');
        
        ws.onmessage = function(event) {
            const data = JSON.parse(event.data);
            updatePlots(data);
            updateMetrics(data);
        };
    </script>
</body>
</html>
```

### 7.3 Automatización de Pruebas

#### Test Runner
```python
class EDrumTestSuite:
    def __init__(self, esp32_port, oscilloscope_ip):
        self.esp32 = ESP32Interface(esp32_port)
        self.scope = OscilloscopeInterface(oscilloscope_ip)
        self.results = []
    
    def run_full_validation(self):
        tests = [
            self.test_adc_accuracy,
            self.test_filter_response,
            self.test_edge_detection,
            self.test_velocity_mapping,
            self.test_latency_measurement
        ]
        
        for test in tests:
            result = test()
            self.results.append(result)
            if not result.passed:
                print(f"FAIL: {test.__name__}")
                return False
        
        return True
    
    def generate_report(self):
        # Generar reporte PDF con gráficos y métricas
        pass
```

## 8. Implementación Práctica

### 8.1 Configuración del Entorno

#### Hardware Requerido
- ESP32-S3 DevKit con e-drum system
- Osciloscopio digital (≥100MHz, 2 canales)
- Generador de funciones
- Piezo de referencia calibrado
- Medidor de fuerza (opcional)

#### Software Requerido
- ESP-IDF v5.4.1
- Python 3.8+ con scipy, numpy, matplotlib
- Software osciloscopio con exportación CSV
- Editor de texto para logs

### 8.2 Flujo de Trabajo

#### Sesión de Validación Típica
1. **Setup**: Conectar hardware, inicializar software
2. **Calibración**: Ejecutar rutinas de calibración automática
3. **Validación**: Correr test suite completo
4. **Análisis**: Procesar datos y generar reportes
5. **Ajustes**: Modificar parámetros si es necesario
6. **Documentación**: Guardar resultados y configuración

#### Cronograma Sugerido
- **Día 1**: Validación Phase 1 (ADC)
- **Día 2**: Validación Phase 2 (Filtro)
- **Día 3**: Validación Phase 3 (Algoritmos)
- **Día 4**: Pruebas de integración
- **Día 5**: Optimización y documentación

## 9. Conclusiones

Este sistema de validación y calibración proporciona una metodología completa para verificar el funcionamiento del e-drum system ESP32. La combinación de logs en tiempo real, capturas de osciloscopio y datos sintéticos permite una validación exhaustiva de cada componente del algoritmo de detección.

La implementación de este protocolo asegura que el sistema cumple con los requisitos de rendimiento y proporciona una base sólida para futuras mejoras y optimizaciones.

---

**Documento**: Sistema de Validación y Calibración v1.0  
**Fecha**: 2024  
**Autor**: ESP32 E-Drum Development Team  
**Estado**: Implementación en progreso