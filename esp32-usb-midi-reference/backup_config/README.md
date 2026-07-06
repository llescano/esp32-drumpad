# ESP32-S3 USB MIDI Reference Project

Este es un proyecto de referencia simple para verificar el funcionamiento del USB OTG en ESP32-S3 como dispositivo MIDI.

## Estado del Proyecto

✅ **Completado** - Proyecto de referencia funcionando correctamente
- Compilación exitosa
- Flasheo exitoso al ESP32-S3
- Dispositivo USB MIDI detectado como 'ESP32-S3 MIDI'
- Secuencia de notas MIDI enviándose cada segundo (escala de Do mayor)

## Hardware Requerido

- **ESP32-S3-DevKitC-1** (cualquier variante)
- **Cable USB Micro-B** para conexión al puerto USB nativo
- **Computadora** con software MIDI (Windows/Linux/macOS)

## ⚠️ Conexión USB Importante

**CRÍTICO:** La ESP32-S3-DevKitC-1 tiene **DOS puertos micro-USB**:

1. **Puerto UART** (etiquetado como "UART" o "COM"): Para programación y monitor serie
2. **Puerto USB OTG** (etiquetado como "USB"): Para dispositivo USB MIDI

### Pines USB OTG
- **GPIO19**: USB D- (Data Minus)
- **GPIO20**: USB D+ (Data Plus)

### Procedimiento de Conexión

1. **Para programar**: Usar el puerto **UART** (micro-USB)
2. **Para MIDI**: Desconectar UART y conectar al puerto **USB** (micro-USB)
3. **NO usar ambos puertos simultáneamente**

## Configuración del Entorno

### Prerequisitos
- ESP-IDF v5.0 o superior
- Python 3.7+
- Drivers USB para ESP32-S3

### Configuración ESP-IDF
```bash
# Windows PowerShell
i:\esp32\v5.4.1\esp-idf\export.ps1

# Linux/macOS
. $HOME/esp/esp-idf/export.sh
```

## Compilación y Flash

### 1. Compilar el Proyecto
```bash
cd esp32-usb-midi-reference
idf.py build
```

### 2. Flashear (usando puerto UART)
```bash
# Conectar cable al puerto UART
idf.py -p COM9 flash monitor
```

### 3. Cambiar a Puerto USB para MIDI
1. Desconectar cable del puerto UART
2. Conectar cable al puerto USB (etiquetado "USB")
3. El dispositivo debería aparecer como "ESP32-S3 MIDI"

## Verificación del Funcionamiento

### En Windows
1. Abrir **Device Manager**
2. Buscar en **Sound, video and game controllers**
3. Debería aparecer: **ESP32-S3 MIDI**

### Software MIDI Recomendado

#### Windows
- **MIDI-OX**: Monitor y test de dispositivos MIDI
- **LoopMIDI**: Virtual MIDI ports

#### Linux
- **qsynth** con **qjackctl**
- **amidi**: Herramientas de línea de comandos
```bash
# Listar dispositivos MIDI
amidi -l

# Recibir datos MIDI
amidi -p hw:1,0 -d
```

#### macOS
- **SimpleSynth**: Sintetizador simple
- **Audio MIDI Setup**: Utilidad del sistema

### Salida Esperada

El dispositivo enviará una secuencia de notas (escala de Do mayor) cada segundo:
- **Notas**: C4, D4, E4, F4, G4, A4, B4, C5
- **Canal MIDI**: 1
- **Velocidad**: 127 (máxima)

## Resolución de Problemas

### Dispositivo No Reconocido

1. **Verificar puerto correcto**:
   - ¿Estás usando el puerto USB (no UART)?
   - ¿El cable soporta datos (no solo carga)?

2. **Verificar drivers**:
   ```bash
   # Windows: Verificar en Device Manager
   # Buscar "Unknown Device" o errores
   ```

3. **Reset del dispositivo**:
   - Presionar botón **Reset** en la placa
   - Desconectar y reconectar USB

### Monitor Serie para Debug

```bash
# Conectar al puerto UART para ver logs
idf.py -p COM9 monitor
```

**Salida esperada**:
```
I (285) usb_midi_example: USB MIDI Example for ESP32-S3
I (285) usb_midi_example: Connect to USB port labeled 'USB' (not UART port)
I (285) usb_midi_example: GPIO19=D-, GPIO20=D+ for USB OTG
I (295) usb_midi_example: Installing TinyUSB driver...
I (455) TinyUSB: TinyUSB Driver installed
I (465) usb_midi_example: TinyUSB driver installed successfully
I (465) usb_midi_example: USB MIDI device ready. Connect to computer and check for MIDI device.
I (475) usb_midi_example: Device should appear as 'ESP32-S3 MIDI' in MIDI software.
```

### Errores Comunes

#### Error: "USB: Disconnected"
- **Causa**: Cable conectado al puerto UART en lugar del USB
- **Solución**: Cambiar al puerto USB etiquetado

#### Error: "MIDI not mounted"
- **Causa**: Dispositivo USB no reconocido por el host
- **Solución**: Verificar drivers y configuración USB OTG

#### Error de Compilación
- **Causa**: ESP-IDF no configurado
- **Solución**: Ejecutar `export.ps1` antes de compilar

## Configuración Avanzada

### Modificar Descriptores USB

Editar `tusb_midi_main.c`:
```c
// Cambiar nombre del dispositivo
static const char* s_str_desc[4] = {
    (char[]){0x09, 0x04},
    "Tu Empresa",           // Manufacturer
    "Mi Dispositivo MIDI",  // Product
    "123456",              // Serial
};
```

### Cambiar Secuencia de Notas

```c
// En periodic_midi_write_example_cb()
uint8_t const note_sequence[] = {60, 64, 67, 72}; // C major chord
```

## Estructura del Proyecto

```
esp32-usb-midi-reference/
├── CMakeLists.txt          # Configuración principal del proyecto
├── sdkconfig.defaults      # Configuración por defecto
├── README.md              # Este archivo
└── main/
    ├── CMakeLists.txt     # Configuración del componente main
    └── tusb_midi_main.c   # Código principal
```

## Referencias

- [ESP32-S3-DevKitC-1 User Guide](https://docs.espressif.com/projects/esp-dev-kits/en/latest/esp32s3/esp32-s3-devkitc-1/user_guide_v1.1.html)
- [ESP-IDF USB Device Documentation](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/api-reference/peripherals/usb_device.html)
- [TinyUSB MIDI Example](https://github.com/espressif/esp-idf/tree/master/examples/peripherals/usb/device/tusb_midi)

## Licencia

Este proyecto está basado en el ejemplo oficial de ESP-IDF y mantiene la licencia MIT original.

---

**Nota**: Este proyecto es únicamente para verificar el funcionamiento básico del USB OTG en ESP32-S3. Para proyectos más complejos, considera usar el framework completo de tu aplicación principal.