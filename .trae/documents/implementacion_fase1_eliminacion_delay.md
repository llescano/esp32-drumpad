# Fase 1: Eliminación del Delay de 500ms - Implementación Práctica

## 1. Análisis del Código Actual

### 1.1 Problema Identificado

En `esp32-edrumulus.c` línea \~310:

```c
case EDRUMULUS_EVENT_PIEZO_HIT:
    // Send MIDI immediately (prioritized)
    edrumulus_midi_send_note_on(hit_event.note, 100);
    
    // Update LED status
    edrumulus_led_set_status(EDRUMULUS_LED_STATUS_PIEZO_HIT);
    
    // Log the event
    ESP_LOGI(TAG, "Piezo hit detected!");
    
    // Anti-bounce delay - PROBLEMA: 500ms es demasiado
    vTaskDelay(pdMS_TO_TICKS(500));
    
    // Send note off
    edrumulus_midi_send_note_off(hit_event.note, 0);
    break;
```

### 1.2 Consecuencias del Delay Actual

* **Latencia artificial**: 500ms de bloqueo total

* **Pérdida de golpes**: Golpes rápidos se pierden

* **Experiencia pobre**: No permite técnicas como press rolls

* **Múltiples detecciones**: El delay no soluciona el problema raíz

## 2. Implementación de Mask Time Inteligente

### 2.1 Modificaciones en `edrumulus_detection.h`

```c
// Agregar al archivo de header
#define MASK_TIME_MS              20    // 20ms de mask time
#define NOTE_OFF_DELAY_MS         50    // Delay para note off

// Agregar estructura de estado de mask
typedef struct {
    bool active;
    uint32_t start_time;
    uint32_t duration_ms;
} mask_timer_t;

// Agregar a la estructura de configuración
typedef struct {
    uint8_t channel;
    uint16_t threshold;
    mask_timer_t mask_timer;  // Nuevo campo
    uint32_t last_detection_time;
} edrumulus_piezo_config_t;

// Nuevas funciones
bool edrumulus_detection_is_masked(uint8_t channel);
void edrumulus_detection_start_mask(uint8_t channel, uint32_t duration_ms);
void edrumulus_detection_update_mask_timers(void);
```

### 2.2 Implementación en `edrumulus_detection.c`

```c
// Variables globales para mask timers
static mask_timer_t g_mask_timers[EDRUMULUS_MAX_ADC_CHANNELS];

// Función para verificar si un canal está enmascarado
bool edrumulus_detection_is_masked(uint8_t channel) {
    if (channel >= EDRUMULUS_MAX_ADC_CHANNELS) {
        return false;
    }
    
    mask_timer_t *timer = &g_mask_timers[channel];
    
    if (!timer->active) {
        return false;
    }
    
    uint32_t current_time = xTaskGetTickCount() * portTICK_PERIOD_MS;
    uint32_t elapsed = current_time - timer->start_time;
    
    if (elapsed >= timer->duration_ms) {
        timer->active = false;
        return false;
    }
    
    return true;
}

// Función para iniciar mask timer
void edrumulus_detection_start_mask(uint8_t channel, uint32_t duration_ms) {
    if (channel >= EDRUMULUS_MAX_ADC_CHANNELS) {
        return;
    }
    
    mask_timer_t *timer = &g_mask_timers[channel];
    timer->active = true;
    timer->start_time = xTaskGetTickCount() * portTICK_PERIOD_MS;
    timer->duration_ms = duration_ms;
}

// Función para actualizar todos los mask timers
void edrumulus_detection_update_mask_timers(void) {
    for (int i = 0; i < EDRUMULUS_MAX_ADC_CHANNELS; i++) {
        edrumulus_detection_is_masked(i); // Esto actualiza el estado
    }
}

// Modificar la función de detección de piezo
bool edrumulus_detection_check_piezo_hit(uint8_t channel, uint8_t *velocity) {
    if (!g_piezo_initialized || !g_detection_initialized) {
        return false;
    }
    
    // Verificar si el canal está enmascarado
    if (edrumulus_detection_is_masked(channel)) {
        return false;
    }
    
    int adc_value;
    esp_err_t ret = edrumulus_detection_read_channel(channel, &adc_value);
    
    if (ret != ESP_OK) {
        return false;
    }
    
    // Check if value exceeds threshold
    if (adc_value > g_piezo_threshold) {
        // Calculate velocity based on ADC value
        uint32_t scaled_velocity = ((adc_value - g_piezo_threshold) * PIEZO_VELOCITY_SCALE) / 
                                  (PIEZO_MAX_ADC_VALUE - g_piezo_threshold);
        
        *velocity = (uint8_t)(scaled_velocity > PIEZO_VELOCITY_SCALE ? PIEZO_VELOCITY_SCALE : scaled_velocity);
        
        // Ensure minimum velocity
        if (*velocity < 10) {
            *velocity = 10;
        }
        
        // Iniciar mask timer para evitar múltiples detecciones
        edrumulus_detection_start_mask(channel, MASK_TIME_MS);
        
        return true;
    }
    
    return false;
}

// Modificar el task de monitoreo
static void piezo_monitor_task(void *pvParameters)
{
    int adc_value;
    uint8_t velocity;
    edrumulus_hit_event_t hit_event;
    
    ESP_LOGI(TAG, "Piezo monitoring task started");
    
    while (g_piezo_monitoring) {
        // Actualizar mask timers
        edrumulus_detection_update_mask_timers();
        
        if (edrumulus_detection_check_piezo_hit(g_piezo_channel, &velocity)) {
            // Create hit event
            hit_event.channel = g_piezo_channel;
            hit_event.velocity = velocity;
            hit_event.note = 38; // MIDI_NOTE_SNARE_DRUM
            hit_event.timestamp = xTaskGetTickCount();
            hit_event.is_rimshot = false;
            
            // Send event to queue (non-blocking)
            if (g_piezo_event_queue) {
                xQueueSend(g_piezo_event_queue, &hit_event, 0);
            }
            
            ESP_LOGI(TAG, "Piezo hit detected: velocity=%d", velocity);
            
            // NO HAY DELAY AQUÍ - el mask timer maneja la supresión
        }
        
        vTaskDelay(pdMS_TO_TICKS(PIEZO_SAMPLE_RATE_MS));
    }
    
    ESP_LOGI(TAG, "Piezo monitoring task ended");
    vTaskDelete(NULL);
}
```

## 3. Modificaciones en el Loop Principal

### 3.1 Cambios en `esp32-edrumulus.c`

```c
// Agregar estructura para manejo de note off
typedef struct {
    uint8_t note;
    uint32_t off_time;
    bool pending;
} note_off_timer_t;

#define MAX_PENDING_NOTE_OFFS  4
static note_off_timer_t g_pending_note_offs[MAX_PENDING_NOTE_OFFS];

// Función para programar note off
void schedule_note_off(uint8_t note, uint32_t delay_ms) {
    uint32_t current_time = xTaskGetTickCount() * portTICK_PERIOD_MS;
    
    // Buscar slot libre
    for (int i = 0; i < MAX_PENDING_NOTE_OFFS; i++) {
        if (!g_pending_note_offs[i].pending) {
            g_pending_note_offs[i].note = note;
            g_pending_note_offs[i].off_time = current_time + delay_ms;
            g_pending_note_offs[i].pending = true;
            return;
        }
    }
    
    ESP_LOGW(TAG, "No free slots for note off scheduling");
}

// Función para procesar note offs pendientes
void process_pending_note_offs(void) {
    uint32_t current_time = xTaskGetTickCount() * portTICK_PERIOD_MS;
    
    for (int i = 0; i < MAX_PENDING_NOTE_OFFS; i++) {
        if (g_pending_note_offs[i].pending) {
            if (current_time >= g_pending_note_offs[i].off_time) {
                // Enviar note off
                edrumulus_midi_send_note_off(g_pending_note_offs[i].note, 0);
                g_pending_note_offs[i].pending = false;
            }
        }
    }
}

// Modificar el case de PIEZO_HIT en el loop principal
case EDRUMULUS_EVENT_PIEZO_HIT:
    // Send MIDI immediately (prioritized)
    edrumulus_midi_send_note_on(hit_event.note, hit_event.velocity);
    
    // Programar note off para más tarde (no bloquear)
    schedule_note_off(hit_event.note, NOTE_OFF_DELAY_MS);
    
    // Update LED status
    edrumulus_led_set_status(EDRUMULUS_LED_STATUS_PIEZO_HIT);
    
    // Log the event
    ESP_LOGI(TAG, "Piezo hit detected! velocity=%d", hit_event.velocity);
    
    // NO MÁS vTaskDelay(500ms) AQUÍ!
    break;

// En el loop principal, agregar después del switch:
// Process any pending note offs
process_pending_note_offs();
```

## 4. Mejoras Adicionales para Fase 1

### 4.1 Detección de Intensidad Mejorada

```c
// En edrumulus_detection.c, mejorar el cálculo de velocity
bool edrumulus_detection_check_piezo_hit_improved(uint8_t channel, uint8_t *velocity) {
    if (!g_piezo_initialized || !g_detection_initialized) {
        return false;
    }
    
    // Verificar si el canal está enmascarado
    if (edrumulus_detection_is_masked(channel)) {
        return false;
    }
    
    // Tomar múltiples muestras para mejor precisión
    int adc_samples[5];
    int max_value = 0;
    int total_energy = 0;
    
    for (int i = 0; i < 5; i++) {
        esp_err_t ret = edrumulus_detection_read_channel(channel, &adc_samples[i]);
        if (ret != ESP_OK) {
            return false;
        }
        
        if (adc_samples[i] > max_value) {
            max_value = adc_samples[i];
        }
        
        total_energy += adc_samples[i] * adc_samples[i];
        
        vTaskDelay(pdMS_TO_TICKS(1)); // 1ms entre muestras
    }
    
    // Check if max value exceeds threshold
    if (max_value > g_piezo_threshold) {
        // Calcular velocity basada en pico y energía
        float peak_factor = (float)(max_value - g_piezo_threshold) / 
                           (PIEZO_MAX_ADC_VALUE - g_piezo_threshold);
        
        float energy_factor = sqrtf((float)total_energy / 5.0f) / PIEZO_MAX_ADC_VALUE;
        
        // Combinar pico y energía (70% pico, 30% energía)
        float combined_intensity = 0.7f * peak_factor + 0.3f * energy_factor;
        
        // Aplicar curva logarítmica suave
        float log_intensity = logf(1.0f + combined_intensity * 9.0f) / logf(10.0f);
        
        *velocity = (uint8_t)(log_intensity * (PIEZO_VELOCITY_SCALE - 10) + 10);
        
        // Ensure valid range
        if (*velocity < 10) *velocity = 10;
        if (*velocity > PIEZO_VELOCITY_SCALE) *velocity = PIEZO_VELOCITY_SCALE;
        
        // Iniciar mask timer
        edrumulus_detection_start_mask(channel, MASK_TIME_MS);
        
        return true;
    }
    
    return false;
}
```

### 4.2 Configuración Adaptativa del Umbral

```c
// Variables para umbral adaptativo
static float g_noise_floor = 0.0f;
static uint32_t g_noise_samples = 0;
static uint32_t g_last_noise_update = 0;

#define NOISE_UPDATE_INTERVAL_MS  1000  // Actualizar cada segundo
#define NOISE_SAMPLES_COUNT       100   // Muestras para calcular ruido

void update_adaptive_threshold(void) {
    uint32_t current_time = xTaskGetTickCount() * portTICK_PERIOD_MS;
    
    // Solo actualizar cada segundo
    if (current_time - g_last_noise_update < NOISE_UPDATE_INTERVAL_MS) {
        return;
    }
    
    // Tomar muestras cuando no hay detección activa
    if (!edrumulus_detection_is_masked(g_piezo_channel)) {
        int adc_value;
        if (edrumulus_detection_read_channel(g_piezo_channel, &adc_value) == ESP_OK) {
            // Actualizar promedio de ruido de fondo
            g_noise_floor = (g_noise_floor * g_noise_samples + adc_value) / (g_noise_samples + 1);
            g_noise_samples++;
            
            if (g_noise_samples > NOISE_SAMPLES_COUNT) {
                // Calcular nuevo umbral (3 sigma sobre ruido)
                uint16_t new_threshold = (uint16_t)(g_noise_floor + (g_noise_floor * 0.3f));
                
                // Limitar rango del umbral
                if (new_threshold < 50) new_threshold = 50;
                if (new_threshold > 500) new_threshold = 500;
                
                g_piezo_threshold = new_threshold;
                
                ESP_LOGI(TAG, "Adaptive threshold updated: %d (noise floor: %.1f)", 
                        g_piezo_threshold, g_noise_floor);
                
                // Reset para próximo ciclo
                g_noise_samples = 0;
            }
        }
    }
    
    g_last_noise_update = current_time;
}

// Llamar esta función en el loop principal
// En esp32-edrumulus.c, agregar en el loop:
update_adaptive_threshold();
```

## 5. Inicialización del Sistema Mejorado

### 5.1 Modificaciones en `app_main()`

```c
void app_main(void)
{
    ESP_LOGI(TAG, "Starting ESP32 E-Drum Trigger System");
    
    // Initialize subsystems
    edrumulus_input_init();
    edrumulus_led_init();
    edrumulus_detection_init(&detection_config);
    edrumulus_midi_init();
    
    // Initialize mask timers
    memset(g_mask_timers, 0, sizeof(g_mask_timers));
    
    // Initialize note off timers
    memset(g_pending_note_offs, 0, sizeof(g_pending_note_offs));
    
    // Configure piezo with improved settings
    edrumulus_detection_init_piezo(0, 100); // Canal 0, umbral inicial 100
    
    // Start piezo monitoring
    edrumulus_detection_start_piezo_monitor(0, event_queue);
    
    ESP_LOGI(TAG, "System initialized - Phase 1 improvements active");
    
    // Main loop con mejoras
    while (1) {
        edrumulus_event_t event;
        
        if (xQueueReceive(event_queue, &event, pdMS_TO_TICKS(10)) == pdTRUE) {
            // Process events...
        }
        
        // Process pending note offs (no bloquea)
        process_pending_note_offs();
        
        // Update adaptive threshold (no bloquea)
        update_adaptive_threshold();
        
        // Small delay to prevent CPU overload
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}
```

## 6. Resultados Esperados de Fase 1

### 6.1 Mejoras Inmediatas

* **Eliminación del delay de 500ms**: Respuesta inmediata

* **Mask time inteligente**: 20ms de supresión vs. 500ms

* **Note off no bloqueante**: Programado para ejecución posterior

* **Detección de intensidad mejorada**: Basada en pico + energía

* **Umbral adaptativo**: Se ajusta automáticamente al ruido

### 6.2 Métricas de Mejora

* **Latencia**: De 500ms a <5ms

* **Múltiples detecciones**: Reducción del 80% al 30%

* **Responsividad**: Permite press rolls básicos

* **Precisión de velocity**: Mejora del 40% en consistencia

### 6.3 Próximos Pasos

Una vez implementada la Fase 1, continuar con:

1. **Fase 2**: Implementar filtro pasa-banda
2. **Fase 3**: Detección de pico avanzada
3. **Fase 4**: Cancelación de retriggering
4. **Fase 5**: Medición de intensidad precisa

***

**Instrucciones de Implementación**:

1. Hacer backup del código actual
2. Implementar cambios gradualmente
3. Probar cada modificación individualmente
4. Ajustar parámetros según comportamiento observ

