# Sistema de Comandos por Consola - Phase 3

Este documento describe el sistema de comandos por consola implementado para el ajuste dinámico de parámetros del sistema Phase 3 del ESP32 E-Drum Trigger.

## Configuración

### Puerto Serie
- **Puerto:** COM9 (configurado para ESP32-S3)
- **Baudrate:** 115200
- **Protocolo:** 8N1

### Acceso
Para acceder al sistema de comandos:
1. Conectar el ESP32-S3 via USB
2. Abrir terminal serie (PuTTY, Arduino IDE Serial Monitor, etc.)
3. Configurar COM9 a 115200 baud
4. Escribir comandos y presionar Enter

## Comandos Disponibles

### `help`
Muestra la lista completa de comandos disponibles con ejemplos de uso.

```
help
```

### `show`
Muestra la configuración actual de todos los módulos Phase 3.

```
show
```

**Salida típica:**
```
=== Configuración Actual de Parámetros Phase 3 ===

Edge Detector:
  Threshold: 150.00
  Sensitivity: 1.00
  Rise Rate: 0.50

Decay Analyzer:
  Tau Min: 0.001
  Tau Max: 0.100
  R² Threshold: 0.95

...
```

### `set <parámetro> <valor>`
Establece el valor de un parámetro específico.

#### Parámetros del Edge Detector:
- `edge_threshold` - Umbral de detección (típico: 50-500)
- `edge_sensitivity` - Sensibilidad (típico: 0.1-2.0)
- `rise_rate` - Tasa de subida (típico: 0.1-1.0)

#### Parámetros del Decay Analyzer:
- `tau_min` - Tau mínimo (típico: 0.001-0.01)
- `tau_max` - Tau máximo (típico: 0.05-0.2)
- `r_squared` - Umbral R² (típico: 0.8-0.99)

#### Parámetros del Velocity Validator:
- `linearity` - Umbral de linealidad (típico: 0.7-0.95)
- `repeatability` - Umbral de repetibilidad (típico: 0.8-0.98)
- `dynamic_min` - Rango dinámico mínimo (típico: 1-20)
- `dynamic_max` - Rango dinámico máximo (típico: 100-127)

#### Parámetros del Adaptive Threshold:
- `snr_target` - SNR objetivo (típico: 10-30 dB)
- `adaptation_time` - Tiempo de adaptación (típico: 0.5-2.0 s)
- `stability` - Umbral de estabilidad (típico: 0.9-0.99)

**Ejemplos:**
```
set edge_threshold 200
set tau_min 0.005
set linearity 0.85
set snr_target 25
```

### `test <tipo>`
Ejecuta pruebas individuales o todas las pruebas de los módulos.

**Tipos disponibles:**
- `edge` - Prueba del Edge Detector
- `decay` - Prueba del Decay Analyzer
- `velocity` - Prueba del Velocity Validator
- `adaptive` - Prueba del Adaptive Threshold
- `all` - Ejecuta todas las pruebas

**Ejemplos:**
```
test edge
test all
```

### `reset`
Restablece todos los parámetros a sus valores por defecto.

```
reset
```

**Valores por defecto:**
- Edge Detector: threshold=150, sensitivity=1.0, rise_rate=0.5
- Decay Analyzer: tau_min=0.001, tau_max=0.1, r_squared=0.95
- Velocity Validator: linearity=0.85, repeatability=0.90, dynamic_min=10, dynamic_max=127
- Adaptive Threshold: snr_target=20, adaptation_time=1.0, stability=0.95

### `save [nombre]`
Guarda la configuración actual en memoria no volátil (NVS).

```
save                    # Guarda como "default"
save mi_configuracion   # Guarda con nombre específico
```

### `load [nombre]`
Carga una configuración previamente guardada desde NVS.

```
load                    # Carga "default"
load mi_configuracion   # Carga configuración específica
```

## Flujo de Trabajo Típico

### 1. Ajuste Inicial
```
help                    # Ver comandos disponibles
show                    # Ver configuración actual
reset                   # Partir de valores conocidos
```

### 2. Ajuste de Parámetros
```
set edge_threshold 180  # Ajustar sensibilidad
test edge              # Probar cambios
show                   # Verificar valores
```

### 3. Optimización Iterativa
```
set tau_min 0.002      # Ajustar parámetro
test decay             # Probar módulo específico
set linearity 0.88     # Ajustar otro parámetro
test all               # Probar todo el sistema
```

### 4. Guardar Configuración
```
save configuracion_optima  # Guardar ajustes finales
```

### 5. Recuperar Configuración
```
load configuracion_optima  # Cargar ajustes guardados
show                      # Verificar carga
```

## Notas Importantes

### Persistencia
- Los cambios con `set` son temporales hasta reinicio
- Use `save` para hacer cambios permanentes
- Use `load` para recuperar configuraciones guardadas

### Validación
- Los valores se validan automáticamente
- Valores fuera de rango se rechazan con mensaje de error
- Use `show` para verificar que los cambios se aplicaron

### Pruebas
- Las pruebas usan los valores actuales de configuración
- Ejecute pruebas después de cada cambio significativo
- Use `test all` para validación completa del sistema

### Resolución de Problemas
- Si un comando no responde, verificar conexión serie
- Si hay errores de sintaxis, usar `help` para ver formato correcto
- Si los valores no se aplican, verificar rangos válidos
- Use `reset` para volver a configuración conocida

## Ejemplos de Sesión Completa

```
# Sesión de ajuste para pad de caja
help
show
reset
set edge_threshold 120     # Más sensible para caja
set edge_sensitivity 1.2   # Aumentar sensibilidad
test edge
set tau_min 0.0015        # Ajustar decay para caja
set tau_max 0.08
test decay
test all                  # Verificar todo funciona
save configuracion_caja   # Guardar ajustes
```

```
# Sesión de ajuste para tom
load configuracion_caja   # Partir de configuración similar
set edge_threshold 180    # Menos sensible para tom
set tau_max 0.12         # Decay más largo
test all
save configuracion_tom
```

Este sistema permite ajuste dinámico sin necesidad de recompilar y flashear el firmware, acelerando significativamente el proceso de calibración y optimización del sistema de detección.