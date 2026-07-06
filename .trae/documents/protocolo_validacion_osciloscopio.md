# Protocolo de Validación con Osciloscopio - ESP32 E-Drum System

## 1. Introducción

Este documento establece el protocolo estándar para validar el funcionamiento del sistema ESP32 e-drum utilizando capturas de osciloscopio. La validación permite correlacionar las señales físicas del piezo con los logs del sistema y verificar la efectividad de los algoritmos Phase 3 de detección y cancelación de rebotes.

## 2. Configuración del Osciloscopio

### 2.1 Configuración Básica
- **Canales:** 2 canales mínimo
  - CH1: Señal piezo cruda (antes del filtro)
  - CH2: Señal filtrada (después del filtro 40-400Hz)
- **Escala Temporal:** 2.00ms/div (para capturar rebotes completos)
- **Escala Vertical:** 
  - CH1: 1.00V/div
  - CH2: 2.00V/div
- **Trigger:** Edge, CH1, Normal sweep
- **Acoplamiento:** DC para ambos canales

### 2.2 Puntos de Medición
- **Entrada Piezo:** Antes del circuito acondicionador
- **Salida ADC:** Después del filtro anti-aliasing
- **Referencia:** Tierra común del ESP32

### 2.3 Exportación de Datos del Osciloscopio
- **Formatos Compatibles:** CSV, TXT, binario nativo del osciloscopio
- **Resolución:** Máxima disponible (típicamente 8-12 bits)
- **Frecuencia de Muestreo:** Mínimo 10 kSa/s por canal
- **Configuración de Exportación:**
  - Incluir timestamps absolutos
  - Exportar ambos canales simultáneamente
  - Mantener unidades originales (V, s)
  - Incluir metadatos de configuración

## 3. Protocolo de Pruebas

### 3.1 Preparación
1. Conectar osciloscopio a los puntos de medición
2. Configurar ESP32 con logs de debug habilitados:
   ```
   CONFIG_LOG_DEFAULT_LEVEL_DEBUG=y
   CONFIG_EDRUMULUS_DEBUG_ENABLED=y
   ```
3. Iniciar monitor serie en terminal separada
4. Sincronizar timestamp del osciloscopio con logs del sistema

### 3.2 Tipos de Golpes a Validar

#### 3.2.1 Golpe Suave (Velocity 1-30)
- **Objetivo:** Verificar detección de señales débiles
- **Criterios:** 
  - Una sola detección MIDI
  - Velocity proporcional a amplitud
  - Sin rebotes detectados

#### 3.2.2 Golpe Medio (Velocity 31-80)
- **Objetivo:** Validar rango normal de operación
- **Criterios:**
  - Detección precisa del pico principal
  - Rechazo efectivo de rebotes mecánicos
  - Latencia < 3.8ms

#### 3.2.3 Golpe Fuerte (Velocity 81-127)
- **Objetivo:** Probar límites del sistema
- **Criterios:**
  - Sin saturación del ADC
  - Rechazo de múltiples rebotes
  - Velocity máxima sin clipping

### 3.3 Secuencia de Prueba
1. **Captura de Referencia:** Golpe único sin rebotes
2. **Captura de Rebotes:** Golpe con rebotes mecánicos visibles
3. **Captura de Ruido:** Señal de fondo sin golpes
4. **Captura de Saturación:** Golpe muy fuerte (límite del sistema)

## 4. Interpretación de Señales

### 4.1 Análisis de Forma de Onda

#### 4.1.1 Golpe Válido
```
Características esperadas:
- Rise time: < 1ms
- Peak amplitude: > threshold dinámico
- Decay exponencial: τ = 2-5ms
- Rise rate: > 0.050 V/ms
```

#### 4.1.2 Rebote Mecánico
```
Características típicas:
- Rise time: > 2ms
- Amplitude: < 50% del pico principal
- Rise rate: < 0.050 V/ms
- Patrón irregular de decaimiento
```

### 4.2 Correlación con Logs del Sistema

#### 4.2.1 Log de Detección Válida
```
D (timestamp) edrumulus_detection: Valid hit detected: channel=X, velocity=Y, adc=Z, filtered=W
I (timestamp) edrumulus_detection: Piezo hit detected: velocity=Y, mask_time=Zms
D (timestamp) edrumulus_midi: Note ON: ch=9, note=X, vel=Y
```

#### 4.2.2 Log de Rechazo de Rebote
```
D (timestamp) edrumulus_detection: Rebound rejected (low rise rate): X.XXX < 0.050
D (timestamp) edrumulus_detection: Signal rejected by rebound detector on channel X
```

## 5. Validación de Algoritmos Phase 3

### 5.1 Edge Detector
- **Verificar:** Rise rate calculation
- **Método:** Medir pendiente en osciloscopio vs log
- **Criterio:** Diferencia < 10%

### 5.2 Decay Analyzer
- **Verificar:** Exponential decay fitting
- **Método:** Análisis matemático de la curva
- **Criterio:** R² > 0.85 para ajuste exponencial

### 5.3 Velocity Validator
- **Verificar:** Correlación amplitud-velocity
- **Método:** Gráfico scatter plot
- **Criterio:** Correlación lineal R > 0.9

### 5.4 Adaptive Threshold
- **Verificar:** Ajuste dinámico del umbral
- **Método:** Monitorear threshold vs noise floor
- **Criterio:** Threshold = 2.5 × noise_floor

## 6. Criterios de Evaluación

### 6.1 Efectividad del Detector de Rebotes
- **Métrica Principal:** False Positive Rate < 2%
- **Métrica Secundaria:** True Positive Rate > 98%
- **Cálculo:**
  ```
  FPR = Rebotes detectados como golpes / Total rebotes
  TPR = Golpes válidos detectados / Total golpes válidos
  ```

### 6.2 Precisión de Velocity
- **Rango Aceptable:** ±5 MIDI units para golpes medios
- **Linealidad:** R² > 0.9 en curva amplitud-velocity
- **Repetibilidad:** σ < 3 MIDI units para golpes idénticos

### 6.3 Latencia del Sistema
- **Objetivo:** < 3.8ms total
- **Medición:** Tiempo desde pico de señal hasta MIDI OUT
- **Componentes:**
  - ADC sampling: ~0.1ms
  - Filter processing: ~0.5ms
  - Phase 3 algorithms: ~1.2ms
  - MIDI transmission: ~2.0ms

## 7. Procedimiento de Correlación

### 7.1 Sincronización Temporal
1. Marcar timestamp en osciloscopio al inicio de captura
2. Registrar timestamp del primer log correspondiente
3. Calcular offset temporal: `Δt = t_log - t_scope`
4. Aplicar offset a todas las mediciones

### 7.2 Análisis de Correlación
1. **Identificar Picos:** Localizar máximos en forma de onda
2. **Buscar Logs:** Encontrar logs correspondientes usando timestamps
3. **Validar Parámetros:** Comparar amplitud vs velocity, timing vs latencia
4. **Documentar Discrepancias:** Registrar diferencias > tolerancia

### 7.3 Generación de Reportes
```
Reporte de Validación:
- Timestamp de prueba
- Configuración del sistema
- Resultados por tipo de golpe
- Métricas de performance
- Capturas de osciloscopio adjuntas
- Logs del sistema correspondientes
- Conclusiones y recomendaciones
```

## 8. Casos de Prueba Específicos

### 8.1 Caso: Golpe con Rebotes Múltiples
**Objetivo:** Validar rechazo de rebotes mecánicos
**Setup:** Golpe fuerte en pad con rebotes visibles
**Expectativa:** Una sola detección MIDI, múltiples rechazos en log

### 8.2 Caso: Golpes Rápidos Consecutivos
**Objetivo:** Verificar mask time inteligente
**Setup:** Dos golpes separados por 5ms
**Expectativa:** Detección del primero, rechazo del segundo

### 8.3 Caso: Señal Saturada
**Objetivo:** Comportamiento en límites del ADC
**Setup:** Golpe que sature la entrada (>3.3V)
**Expectativa:** Velocity = 127, sin detecciones erróneas

### 8.4 Caso: Ruido de Fondo
**Objetivo:** Inmunidad a interferencias
**Setup:** Sin golpes, solo ruido eléctrico
**Expectativa:** Cero detecciones, threshold adaptativo estable

## 9. Herramientas de Análisis

### 9.1 Software Recomendado
- **Osciloscopio:** Rigol DS2000 series o equivalente
- **Análisis:** MATLAB/Octave para procesamiento de señales
- **Logs:** ESP-IDF Monitor con timestamps precisos
- **Correlación:** Scripts Python personalizados
- **Procesamiento de Datos Exportados:** NumPy, SciPy, Pandas

### 9.2 Procesamiento de Archivos Exportados

#### 9.2.1 Formato CSV Estándar
```csv
Time(s),CH1(V),CH2(V)
0.000000,-0.001,0.002
0.000001,0.003,-0.001
...
```

#### 9.2.2 Script de Análisis Automático
```python
import pandas as pd
import numpy as np
from scipy import signal

def analyze_exported_data(csv_file, log_file):
    # Cargar datos del osciloscopio
    scope_data = pd.read_csv(csv_file)
    
    # Detectar picos con valores exactos
    peaks, properties = signal.find_peaks(
        scope_data['CH1(V)'], 
        height=0.1,  # Threshold mínimo
        distance=100  # Separación mínima entre picos
    )
    
    # Calcular rise rate exacto
    for peak_idx in peaks:
        rise_rate = calculate_exact_rise_rate(
            scope_data['Time(s)'].iloc[peak_idx-10:peak_idx+1],
            scope_data['CH1(V)'].iloc[peak_idx-10:peak_idx+1]
        )
        
    # Correlacionar con logs del sistema
    correlate_with_logs(peaks, log_file)
    
    return analysis_results

def calculate_exact_rise_rate(time_data, voltage_data):
    # Ajuste lineal en ventana de 1ms antes del pico
    slope, intercept = np.polyfit(time_data, voltage_data, 1)
    return slope * 1000  # Convertir a V/ms
```

### 9.3 Ventajas de los Datos Exportados

#### 9.3.1 Precisión Mejorada
- **Resolución Temporal:** Hasta 1 ns dependiendo del osciloscopio
- **Resolución de Amplitud:** 12-16 bits vs 8 bits de captura de pantalla
- **Eliminación de Errores de Lectura:** Valores numéricos exactos
- **Análisis Matemático:** Cálculos precisos de derivadas, integrales

#### 9.3.2 Análisis Avanzado Posible
```python
# Análisis de espectro de frecuencias
def frequency_analysis(time_data, voltage_data, sample_rate):
    freqs, psd = signal.welch(voltage_data, sample_rate)
    
    # Verificar que el filtro 40-400Hz está funcionando
    passband_power = np.sum(psd[(freqs >= 40) & (freqs <= 400)])
    total_power = np.sum(psd)
    filter_efficiency = passband_power / total_power
    
    return filter_efficiency

# Análisis de correlación cruzada
def cross_correlation_analysis(ch1_data, ch2_data):
    correlation = signal.correlate(ch1_data, ch2_data, mode='full')
    delay = np.argmax(correlation) - len(ch2_data) + 1
    
    # Calcular latencia del filtro
    filter_delay_samples = delay
    return filter_delay_samples
```

#### 9.3.3 Validación Automática de Algoritmos
```python
def validate_phase3_algorithms(exported_data, system_logs):
    results = {
        'edge_detector_accuracy': 0,
        'velocity_correlation': 0,
        'rebound_rejection_rate': 0
    }
    
    # Validar edge detector con datos exactos
    for peak in detected_peaks:
        calculated_rise_rate = calculate_exact_rise_rate(peak)
        logged_rise_rate = extract_from_logs(peak.timestamp)
        
        accuracy = 1 - abs(calculated_rise_rate - logged_rise_rate) / calculated_rise_rate
        results['edge_detector_accuracy'] += accuracy
    
    # Validar correlación velocity vs amplitud
    amplitudes = [peak.max_voltage for peak in detected_peaks]
    velocities = [log.velocity for log in corresponding_logs]
    correlation_coeff = np.corrcoef(amplitudes, velocities)[0,1]
    results['velocity_correlation'] = correlation_coeff
    
    return results
```

## 10. Mantenimiento del Protocolo

### 10.1 Revisiones Periódicas
- **Frecuencia:** Cada actualización mayor del firmware
- **Responsable:** Equipo de desarrollo
- **Criterios:** Cambios en algoritmos, hardware o especificaciones

### 10.2 Actualización de Criterios
- **Trigger:** Performance degradation > 5%
- **Proceso:** Análisis de causa raíz, ajuste de parámetros
- **Documentación:** Actualizar este protocolo con nuevos hallazgos

---

**Versión:** 1.0  
**Fecha:** Enero 2025  
**Autor:** Equipo ESP32 E-Drum Development  
**Próxima Revisión:** Marzo 2025