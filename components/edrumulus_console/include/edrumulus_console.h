/**
 * @file edrumulus_console.h
 * @brief Sistema de comandos por consola para ajuste dinámico de parámetros Phase 3
 * 
 * Este componente permite ajustar parámetros de validación en tiempo real
 * sin necesidad de recompilar el firmware.
 */

#ifndef EDRUMULUS_CONSOLE_H
#define EDRUMULUS_CONSOLE_H

#include "esp_err.h"
#include "esp_console.h"
#include "edrumulus_detection.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Estructura para almacenar configuración de parámetros ajustables
 */
typedef struct {
    // Edge Detector parameters
    float edge_threshold;
    float edge_sensitivity;
    float rise_rate_threshold;
    
    // Decay Analyzer parameters
    float tau_min;
    float tau_max;
    float r_squared_threshold;
    
    // Velocity Validator parameters
    float linearity_threshold;
    float repeatability_threshold;
    float dynamic_range_min;
    float dynamic_range_max;
    
    // Adaptive Threshold parameters
    float snr_target;
    float adaptation_time;
    float stability_threshold;
    
    // Test configuration
    bool auto_save_enabled;
    char config_name[32];
} edrumulus_console_config_t;

/**
 * @brief Inicializar el sistema de comandos por consola
 * @return ESP_OK si la inicialización fue exitosa
 */
esp_err_t edrumulus_console_init(void);

/**
 * @brief Procesar comando de entrada simple
 * @param input Cadena de texto con el comando a procesar
 */
void edrumulus_console_process_command(const char* input);

/**
 * @brief Obtener la configuración actual de parámetros
 * @param config Puntero a estructura donde almacenar la configuración
 * @return ESP_OK si la operación fue exitosa
 */
esp_err_t edrumulus_console_get_config(edrumulus_console_config_t *config);

/**
 * @brief Establecer la configuración de parámetros
 * @param config Puntero a estructura con la nueva configuración
 * @return ESP_OK si la operación fue exitosa
 */
esp_err_t edrumulus_console_set_config(const edrumulus_console_config_t *config);

/**
 * @brief Guardar configuración actual en NVS
 * @param name Nombre de la configuración a guardar
 * @return ESP_OK si la operación fue exitosa
 */
esp_err_t edrumulus_console_save_config(const char *name);

/**
 * @brief Cargar configuración desde NVS
 * @param name Nombre de la configuración a cargar
 * @return ESP_OK si la operación fue exitosa
 */
esp_err_t edrumulus_console_load_config(const char *name);

/**
 * @brief Ejecutar prueba individual del Edge Detector
 * @return ESP_OK si la prueba fue exitosa
 */
esp_err_t edrumulus_console_test_edge_detector(void);

/**
 * @brief Ejecutar prueba individual del Decay Analyzer
 * @return ESP_OK si la prueba fue exitosa
 */
esp_err_t edrumulus_console_test_decay_analyzer(void);

/**
 * @brief Ejecutar prueba individual del Velocity Validator
 * @return ESP_OK si la prueba fue exitosa
 */
esp_err_t edrumulus_console_test_velocity_validator(void);

/**
 * @brief Ejecutar prueba individual del Adaptive Threshold
 * @return ESP_OK si la prueba fue exitosa
 */
esp_err_t edrumulus_console_test_adaptive_threshold(void);

/**
 * @brief Ejecutar todas las pruebas Phase 3
 * @return ESP_OK si todas las pruebas fueron exitosas
 */
esp_err_t edrumulus_console_test_all(void);

/**
 * @brief Mostrar valores actuales de todos los parámetros
 */
void edrumulus_console_show_values(void);

/**
 * @brief Mostrar ayuda de comandos disponibles
 */
void edrumulus_console_show_help(void);

/**
 * @brief Resetear todos los parámetros a valores por defecto
 * @return ESP_OK si la operación fue exitosa
 */
esp_err_t edrumulus_console_reset_defaults(void);

#ifdef __cplusplus
}
#endif

#endif // EDRUMULUS_CONSOLE_H