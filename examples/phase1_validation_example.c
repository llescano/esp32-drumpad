/**
 * @file phase1_validation_example.c
 * @brief Ejemplo de uso del sistema de validación Phase 1 (ADC y muestreo)
 * 
 * Este archivo muestra cómo integrar y usar las funciones de validación
 * Phase 1 en una aplicación ESP32 E-Drum.
 */

#include "edrumulus_detection.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"

static const char *TAG = "phase1_example";

/**
 * @brief Ejecutar validación completa de un canal ADC
 * 
 * @param channel Canal ADC a validar (0-7)
 * @return esp_err_t ESP_OK si la validación es exitosa
 */
esp_err_t run_channel_validation(uint8_t channel)
{
    ESP_LOGI(TAG, "=== Iniciando validación Phase 1 para canal %d ===", channel);
    
    // Ejecutar suite completa de validación
    esp_err_t ret = edrumulus_adc_validation_run_full_suite(channel);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Error en validación del canal %d: %s", channel, esp_err_to_name(ret));
        return ret;
    }
    
    // Obtener resultados
    edrumulus_adc_validation_t *results;
    ret = edrumulus_adc_validation_get_results(channel, &results);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Error obteniendo resultados del canal %d", channel);
        return ret;
    }
    
    // Mostrar resultados detallados
    ESP_LOGI(TAG, "\n=== RESULTADOS VALIDACIÓN CANAL %d ===", channel);
    
    // Resultados de precisión ADC
    ESP_LOGI(TAG, "📊 PRECISIÓN ADC:");
    ESP_LOGI(TAG, "   Linealidad R²: %.4f (%s)", 
             results->adc_precision.linearity_r_squared,
             results->adc_precision.linearity_valid ? "✅ PASS" : "❌ FAIL");
    ESP_LOGI(TAG, "   Error precisión: %.2f%% (%s)", 
             results->adc_precision.precision_error,
             results->adc_precision.precision_valid ? "✅ PASS" : "❌ FAIL");
    ESP_LOGI(TAG, "   Rango ADC: %d - %d (muestras: %d)", 
             results->adc_precision.raw_adc_min,
             results->adc_precision.raw_adc_max,
             results->adc_precision.total_samples);
    
    // Resultados de muestreo
    ESP_LOGI(TAG, "⏱️  FRECUENCIA MUESTREO:");
    ESP_LOGI(TAG, "   Frecuencia objetivo: %.1f Hz", results->sampling_metrics.target_sample_rate);
    ESP_LOGI(TAG, "   Frecuencia medida: %.1f Hz (%s)", 
             results->sampling_metrics.actual_sample_rate,
             results->sampling_metrics.rate_valid ? "✅ PASS" : "❌ FAIL");
    ESP_LOGI(TAG, "   Jitter: %llu μs (%s)", 
             results->sampling_metrics.jitter_us,
             results->sampling_metrics.jitter_valid ? "✅ PASS" : "❌ FAIL");
    ESP_LOGI(TAG, "   Intervalo promedio: %llu μs", results->sampling_metrics.avg_interval_us);
    
    // Resultados de SNR
    ESP_LOGI(TAG, "🔊 RELACIÓN SEÑAL/RUIDO:");
    if (results->snr_calculator.snr_valid) {
        ESP_LOGI(TAG, "   SNR: %.1f dB (%s)", 
                 results->snr_calculator.snr_db,
                 results->snr_calculator.snr_acceptable ? "✅ PASS" : "❌ FAIL");
        ESP_LOGI(TAG, "   RMS Señal: %.1f", results->snr_calculator.signal_rms);
        ESP_LOGI(TAG, "   RMS Ruido: %.1f", results->snr_calculator.noise_rms);
    } else {
        ESP_LOGI(TAG, "   SNR: ❌ MEDICIÓN INVÁLIDA");
    }
    
    // Resultado general
    bool validation_passed = edrumulus_adc_validation_passed(channel);
    ESP_LOGI(TAG, "\n🎯 RESULTADO GENERAL: %s", 
             validation_passed ? "✅ VALIDACIÓN EXITOSA" : "❌ VALIDACIÓN FALLIDA");
    ESP_LOGI(TAG, "   Duración: %lu ms\n", results->validation_duration);
    
    // Mostrar log de validación
    const char *validation_log = edrumulus_adc_validation_get_log(channel);
    ESP_LOGI(TAG, "📝 Log de validación: %s", validation_log);
    
    return validation_passed ? ESP_OK : ESP_FAIL;
}

/**
 * @brief Ejecutar validaciones individuales paso a paso
 * 
 * @param channel Canal ADC a validar
 * @return esp_err_t ESP_OK si todas las validaciones son exitosas
 */
esp_err_t run_step_by_step_validation(uint8_t channel)
{
    ESP_LOGI(TAG, "=== Validación paso a paso para canal %d ===", channel);
    
    esp_err_t ret;
    
    // Paso 1: Test de precisión ADC
    ESP_LOGI(TAG, "🔧 Paso 1: Validando precisión ADC...");
    ret = edrumulus_adc_validation_test_precision(channel, 0.1f, 3.2f, 50);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Fallo en test de precisión ADC");
        return ret;
    }
    
    vTaskDelay(pdMS_TO_TICKS(500)); // Pausa entre tests
    
    // Paso 2: Test de frecuencia de muestreo
    ESP_LOGI(TAG, "⏱️  Paso 2: Validando frecuencia de muestreo...");
    ret = edrumulus_adc_validation_test_sampling_rate(channel, 2000); // 2 segundos
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Fallo en test de frecuencia de muestreo");
        return ret;
    }
    
    vTaskDelay(pdMS_TO_TICKS(500)); // Pausa entre tests
    
    // Paso 3: Medición de SNR
    ESP_LOGI(TAG, "🔊 Paso 3: Midiendo SNR...");
    ret = edrumulus_adc_validation_measure_snr(channel, 100, 100);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Fallo en medición de SNR");
        return ret;
    }
    
    ESP_LOGI(TAG, "✅ Validación paso a paso completada");
    return ESP_OK;
}

/**
 * @brief Tarea principal de ejemplo de validación Phase 1
 */
void phase1_validation_task(void *pvParameters)
{
    ESP_LOGI(TAG, "🚀 Iniciando ejemplo de validación Phase 1");
    
    // Inicializar sistema de detección
    edrumulus_detection_config_t detection_config = {
        .sample_rate_hz = 8000,
        .channels = 4,
        .event_queue_size = 10
    };
    
    esp_err_t ret = edrumulus_detection_init(&detection_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Error inicializando sistema de detección: %s", esp_err_to_name(ret));
        vTaskDelete(NULL);
        return;
    }
    
    // Inicializar sistema de validación Phase 1
    ret = edrumulus_adc_validation_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Error inicializando validación Phase 1: %s", esp_err_to_name(ret));
        vTaskDelete(NULL);
        return;
    }
    
    ESP_LOGI(TAG, "✅ Sistemas inicializados correctamente");
    
    // Ejemplo 1: Validación completa de canal 0
    ESP_LOGI(TAG, "\n📋 EJEMPLO 1: Validación completa automática");
    ret = run_channel_validation(0);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Validación automática del canal 0 falló");
    }
    
    vTaskDelay(pdMS_TO_TICKS(2000)); // Pausa entre ejemplos
    
    // Ejemplo 2: Validación paso a paso de canal 1
    ESP_LOGI(TAG, "\n📋 EJEMPLO 2: Validación paso a paso");
    ret = run_step_by_step_validation(1);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Validación paso a paso del canal 1 falló");
    }
    
    // Mostrar resultados finales
    run_channel_validation(1);
    
    vTaskDelay(pdMS_TO_TICKS(2000));
    
    // Ejemplo 3: Validación de múltiples canales
    ESP_LOGI(TAG, "\n📋 EJEMPLO 3: Validación de múltiples canales");
    for (uint8_t ch = 0; ch < 4; ch++) {
        ESP_LOGI(TAG, "Validando canal %d...", ch);
        
        // Resetear estado previo
        edrumulus_adc_validation_reset(ch);
        
        // Ejecutar validación rápida
        ret = edrumulus_adc_validation_test_precision(ch, 0.5f, 2.5f, 30);
        if (ret == ESP_OK) {
            bool passed = edrumulus_adc_validation_passed(ch);
            ESP_LOGI(TAG, "Canal %d: %s", ch, passed ? "✅ OK" : "❌ FAIL");
        }
        
        vTaskDelay(pdMS_TO_TICKS(500));
    }
    
    ESP_LOGI(TAG, "\n🎉 Ejemplos de validación Phase 1 completados");
    ESP_LOGI(TAG, "💡 Tip: Revisa los logs para ver métricas detalladas");
    ESP_LOGI(TAG, "📊 Usa las funciones de validación en tu aplicación principal");
    
    // Limpiar recursos
    edrumulus_adc_validation_deinit();
    
    vTaskDelete(NULL);
}

/**
 * @brief Función principal del ejemplo
 */
void app_main(void)
{
    ESP_LOGI(TAG, "=== ESP32 E-Drum Phase 1 Validation Example ===");
    
    // Crear tarea de validación
    xTaskCreate(phase1_validation_task, "phase1_validation", 8192, NULL, 5, NULL);
}

/**
 * @brief Función de utilidad para monitoreo continuo de validación
 * 
 * Esta función puede ser llamada periódicamente para verificar
 * el estado de los canales ADC en tiempo real.
 */
void monitor_adc_health(void)
{
    static uint32_t last_check = 0;
    uint32_t now = esp_timer_get_time() / 1000; // ms
    
    // Verificar cada 10 segundos
    if (now - last_check < 10000) {
        return;
    }
    
    last_check = now;
    
    ESP_LOGI(TAG, "🔍 Verificación de salud ADC...");
    
    for (uint8_t ch = 0; ch < 4; ch++) {
        // Verificar si hay validación en progreso
        if (edrumulus_adc_validation_is_running(ch)) {
            ESP_LOGI(TAG, "Canal %d: Validación en progreso...", ch);
            continue;
        }
        
        // Ejecutar test rápido de SNR
        esp_err_t ret = edrumulus_adc_validation_measure_snr(ch, 50, 50);
        if (ret == ESP_OK) {
            edrumulus_adc_validation_t *results;
            if (edrumulus_adc_validation_get_results(ch, &results) == ESP_OK) {
                if (results->snr_calculator.snr_valid) {
                    ESP_LOGI(TAG, "Canal %d: SNR=%.1fdB %s", ch, 
                             results->snr_calculator.snr_db,
                             results->snr_calculator.snr_acceptable ? "✅" : "⚠️");
                }
            }
        }
    }
}