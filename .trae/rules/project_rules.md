# Reglas del Proyecto ESP32 E-Drum Trigger

## 🔧 Configuración del Entorno de Desarrollo

### ESP-IDF Export
**⚠️ IMPORTANTE:** Para trabajar con este proyecto, siempre ejecutar el siguiente comando en una nueva terminal antes de compilar o flashear:

```powershell
i:\esp32\v5.4.1\esp-idf\export.ps1
```

Este comando configura todas las variables de entorno necesarias para ESP-IDF v5.4.1.

### Hardware Target
- **Microcontrolador:** ESP32-S3 DevKit
- **Puerto Serie:** COM9
- **LED Direccionable:** GPIO48 (integrado en la placa)
- **Encoder Rotativo:** GPIO1-3 con botón integrado
- **Botón de Configuración:** GPIO0 (boot button de la placa)

## 📁 Organización del Código

### Estructura de Directorios
```
esp32-drumpad/
├── components/          # Componentes modulares ESP-IDF
│   ├── edrumulus_core/     # Algoritmos de detección principales
│   ├── edrumulus_midi/     # Manejo USB MIDI
│   ├── edrumulus_input/    # Procesamiento ADC y entrada
│   ├── edrumulus_led/      # Control LED direccionable
│   └── edrumulus_config/   # Configuración y NVS
├── main/               # Aplicación principal
└── build/              # Archivos de compilación (auto-generado)
```

### Reglas de Código
1. **Archivos Modulares:** Mantener cada componente bajo 500 líneas
2. **Separación de Responsabilidades:** Un componente = una funcionalidad específica
3. **Documentación:** Comentar todas las funciones públicas en español
4. **Nomenclatura:** Usar prefijo `edrumulus_` para todas las funciones públicas

## 🔧 Gestión de Componentes

### Componentes Administrados por IDF
**⚠️ REGLAS CRÍTICAS:**

1. **Usar siempre `managed_components`:** Cuando un componente oficial de Espressif esté disponible en el registro de componentes, usar exclusivamente la versión de `managed_components/` en lugar de copias manuales en `components/`.

2. **No modificar componentes oficiales:** Nunca modificar directamente archivos dentro de componentes oficiales de Espressif (como `esp_tinyusb`, `esp_adc`, etc.). Estas modificaciones se perderán en actualizaciones.

3. **Evitar duplicación:** Verificar que no existan múltiples versiones del mismo componente en diferentes directorios (`components/` vs `managed_components/`).

4. **Mantener compatibilidad futura:** Preferir siempre las versiones administradas por el sistema de gestión de dependencias de ESP-IDF para garantizar actualizaciones automáticas y compatibilidad.

### Resolución de Conflictos
- Si existe duplicación, eliminar la versión manual de `components/` y mantener solo `managed_components/`
- Si se requiere configuración personalizada, usar archivos de configuración del proyecto (sdkconfig, Kconfig) en lugar de modificar el componente
- Para funcionalidad adicional, crear componentes wrapper que extiendan la funcionalidad sin modificar el original

## 🔨 Comandos de Desarrollo

### Compilación
```powershell
# Desde el directorio esp32-drumpad/
idf.py build
```

### Flash y Monitor
```powershell
# Flash al ESP32-S3
idf.py -p COM9 flash

# Monitor serie
idf.py -p COM9 monitor

# Flash y monitor en un comando
idf.py -p COM9 flash monitor
```

**⚠️ IMPORTANTE:** Si hay un monitor en ejecución en una terminal, debe cerrarse antes de correr otro para evitar conflictos de puerto serie. Usar `Ctrl+]` para salir del monitor.

**Si `Ctrl+]` no funciona:** Cerrar la terminal completamente, abrir una nueva terminal, ejecutar `i:\esp32\v5.4.1\esp-idf\export.ps1` y después correr el nuevo monitor.

### Configuración
```powershell
# Abrir menuconfig para configuración avanzada
idf.py menuconfig

# Limpiar build
idf.py fullclean
```

## 📋 Estándares de Documentación

### Comentarios de Código
- **Funciones:** Documentar propósito, parámetros y valor de retorno
- **Constantes:** Explicar el significado y unidades si aplica
- **Algoritmos Complejos:** Comentar la lógica paso a paso

### Commits
- Usar mensajes descriptivos en español
- Formato: `[componente] descripción del cambio`
- Ejemplo: `[edrumulus_midi] agregar soporte para velocity sensitivity`

## ⚡ Configuraciones Específicas

### USB MIDI
- Configurado para funcionar como dispositivo USB MIDI nativo
- No requiere drivers adicionales en sistemas modernos
- Velocidad de transmisión optimizada para baja latencia

### Detección de Triggers
- Algoritmos portados desde Edrumulus original
- Configuración persistente en NVS (Non-Volatile Storage)
- Ajuste de sensibilidad via encoder rotativo

### LED de Estado
- **Verde Sólido:** Funcionamiento normal
- **Verde Parpadeante:** Modo configuración
- **Rojo:** Error del sistema
- **Azul:** MIDI conectado
- **Intensidad Variable:** Nivel de entrada de señal

## 🚨 Resolución de Problemas

### Errores Comunes
1. **Error de compilación:** Verificar que se ejecutó el export.ps1
2. **No detecta puerto:** Confirmar que el ESP32-S3 está en COM9
3. **USB MIDI no funciona:** Verificar configuración USB OTG en sdkconfig
4. **Conflictos de componentes:** Verificar que no hay duplicados entre `components/` y `managed_components/`

### Debug
- Usar `ESP_LOGI()`, `ESP_LOGW()`, `ESP_LOGE()` para logging
- Monitor serie a 115200 baud para debug
- LED integrado para indicación visual de estado

## 📝 Notas Importantes

- **Siempre** ejecutar export.ps1 en nuevas terminales
- **Nunca** modificar archivos en build/ (se regeneran automáticamente)
- **Nunca** modificar componentes oficiales en `managed_components/`
- **Respaldar** configuraciones importantes antes de cambios mayores
- **Probar** cada componente individualmente antes de integración

---
*Última actualización: Proyecto reorganizado en esp32-drumpad con gestión adecuada de componentes IDF*