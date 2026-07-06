/**
 * @file edrumulus_console.c
 * @brief Implementación del sistema de comandos por consola para Phase 3
 */

#include "edrumulus_console.h"
#include "esp_log.h"
#include "esp_console.h"
#include "esp_vfs_dev.h"
#include "driver/uart.h"
#include "driver/uart_vfs.h"
#include "linenoise/linenoise.h"
#include "argtable3/argtable3.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "edrumulus_config.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "edrumulus_console";

// Configuración global de parámetros ajustables
static edrumulus_console_config_t g_console_config = {
    // Edge Detector defaults
    .edge_threshold = 100.0f,
    .edge_sensitivity = 0.8f,
    .rise_rate_threshold = 50.0f,
    
    // Decay Analyzer defaults
    .tau_min = 0.001f,
    .tau_max = 0.1f,
    .r_squared_threshold = 0.95f,
    
    // Velocity Validator defaults
    .linearity_threshold = 0.95f,
    .repeatability_threshold = 0.05f,
    .dynamic_range_min = 1.0f,
    .dynamic_range_max = 127.0f,
    
    // Adaptive Threshold defaults
    .snr_target = 20.0f,
    .adaptation_time = 1.0f,
    .stability_threshold = 0.1f,
    
    // Test configuration
    .auto_save_enabled = false,
    .config_name = "default"
};

// ============================================================================
// FUNCIONES DE UTILIDAD
// ============================================================================

/**
 * @brief Aplicar configuración actual al sistema de validación
 * @note Los parámetros se aplicarán cuando se ejecuten las pruebas
 */
static esp_err_t apply_config_to_validation_system(void) {
    // Los parámetros de configuración se aplicarán directamente
    // cuando se ejecuten las funciones de prueba individuales
    ESP_LOGI(TAG, "Configuración lista para aplicar en próximas pruebas");
    return ESP_OK;
}

// ============================================================================
// COMANDOS DE CONFIGURACIÓN
// ============================================================================

/**
 * @brief Comando 'set' para ajustar parámetros individuales
 */
static struct {
    struct arg_str *param;
    struct arg_dbl *value;
    struct arg_end *end;
} set_args;

static int cmd_set(int argc, char **argv) {
    int nerrors = arg_parse(argc, argv, (void **) &set_args);
    if (nerrors != 0) {
        arg_print_errors(stderr, set_args.end, argv[0]);
        return 1;
    }
    
    const char *param = set_args.param->sval[0];
    double value = set_args.value->dval[0];
    
    // Edge Detector parameters
    if (strcmp(param, "edge_threshold") == 0) {
        g_console_config.edge_threshold = (float)value;
        printf("Edge threshold establecido en: %.2f\n", g_console_config.edge_threshold);
    }
    else if (strcmp(param, "edge_sensitivity") == 0) {
        g_console_config.edge_sensitivity = (float)value;
        printf("Edge sensitivity establecido en: %.3f\n", g_console_config.edge_sensitivity);
    }
    else if (strcmp(param, "rise_rate") == 0) {
        g_console_config.rise_rate_threshold = (float)value;
        printf("Rise rate threshold establecido en: %.2f\n", g_console_config.rise_rate_threshold);
    }
    // Decay Analyzer parameters
    else if (strcmp(param, "tau_min") == 0) {
        g_console_config.tau_min = (float)value;
        printf("Tau min establecido en: %.6f\n", g_console_config.tau_min);
    }
    else if (strcmp(param, "tau_max") == 0) {
        g_console_config.tau_max = (float)value;
        printf("Tau max establecido en: %.6f\n", g_console_config.tau_max);
    }
    else if (strcmp(param, "r_squared") == 0) {
        g_console_config.r_squared_threshold = (float)value;
        printf("R² threshold establecido en: %.3f\n", g_console_config.r_squared_threshold);
    }
    // Velocity Validator parameters
    else if (strcmp(param, "linearity") == 0) {
        g_console_config.linearity_threshold = (float)value;
        printf("Linearity threshold establecido en: %.3f\n", g_console_config.linearity_threshold);
    }
    else if (strcmp(param, "repeatability") == 0) {
        g_console_config.repeatability_threshold = (float)value;
        printf("Repeatability threshold establecido en: %.3f\n", g_console_config.repeatability_threshold);
    }
    else if (strcmp(param, "range_min") == 0) {
        g_console_config.dynamic_range_min = (float)value;
        printf("Dynamic range min establecido en: %.1f\n", g_console_config.dynamic_range_min);
    }
    else if (strcmp(param, "range_max") == 0) {
        g_console_config.dynamic_range_max = (float)value;
        printf("Dynamic range max establecido en: %.1f\n", g_console_config.dynamic_range_max);
    }
    // Adaptive Threshold parameters
    else if (strcmp(param, "snr_target") == 0) {
        g_console_config.snr_target = (float)value;
        printf("SNR target establecido en: %.1f dB\n", g_console_config.snr_target);
    }
    else if (strcmp(param, "adaptation_time") == 0) {
        g_console_config.adaptation_time = (float)value;
        printf("Adaptation time establecido en: %.3f s\n", g_console_config.adaptation_time);
    }
    else if (strcmp(param, "stability") == 0) {
        g_console_config.stability_threshold = (float)value;
        printf("Stability threshold establecido en: %.3f\n", g_console_config.stability_threshold);
    }
    else {
        printf("Parámetro desconocido: %s\n", param);
        printf("Parámetros disponibles:\n");
        printf("  Edge Detector: edge_threshold, edge_sensitivity, rise_rate\n");
        printf("  Decay Analyzer: tau_min, tau_max, r_squared\n");
        printf("  Velocity Validator: linearity, repeatability, range_min, range_max\n");
        printf("  Adaptive Threshold: snr_target, adaptation_time, stability\n");
        return 1;
    }
    
    // Los cambios se aplicarán en la próxima ejecución de pruebas
    apply_config_to_validation_system();
    
    // Auto-guardar si está habilitado
    if (g_console_config.auto_save_enabled) {
        edrumulus_console_save_config(g_console_config.config_name);
        printf("Configuración auto-guardada como '%s'\n", g_console_config.config_name);
    }
    
    return 0;
}

static void register_set_command(void) {
    set_args.param = arg_str1(NULL, NULL, "<param>", "Nombre del parámetro");
    set_args.value = arg_dbl1(NULL, NULL, "<value>", "Valor a establecer");
    set_args.end = arg_end(2);
    
    const esp_console_cmd_t cmd = {
        .command = "set",
        .help = "Establecer valor de parámetro",
        .hint = NULL,
        .func = &cmd_set,
        .argtable = &set_args
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));
}

// ============================================================================
// COMANDOS DE PRUEBA
// ============================================================================

/**
 * @brief Comando 'test' para ejecutar pruebas individuales
 */
static struct {
    struct arg_str *component;
    struct arg_end *end;
} test_args;

static int cmd_test(int argc, char **argv) {
    int nerrors = arg_parse(argc, argv, (void **) &test_args);
    if (nerrors != 0) {
        arg_print_errors(stderr, test_args.end, argv[0]);
        return 1;
    }
    
    const char *component = test_args.component->sval[0];
    
    printf("\n=== Ejecutando prueba: %s ===\n", component);
    
    if (strcmp(component, "edge") == 0) {
        esp_err_t ret = edrumulus_console_test_edge_detector();
        printf("Resultado: %s\n", ret == ESP_OK ? "EXITOSO" : "FALLIDO");
    }
    else if (strcmp(component, "decay") == 0) {
        esp_err_t ret = edrumulus_console_test_decay_analyzer();
        printf("Resultado: %s\n", ret == ESP_OK ? "EXITOSO" : "FALLIDO");
    }
    else if (strcmp(component, "velocity") == 0) {
        esp_err_t ret = edrumulus_console_test_velocity_validator();
        printf("Resultado: %s\n", ret == ESP_OK ? "EXITOSO" : "FALLIDO");
    }
    else if (strcmp(component, "adaptive") == 0) {
        esp_err_t ret = edrumulus_console_test_adaptive_threshold();
        printf("Resultado: %s\n", ret == ESP_OK ? "EXITOSO" : "FALLIDO");
    }
    else if (strcmp(component, "all") == 0) {
        esp_err_t ret = edrumulus_console_test_all();
        printf("Resultado general: %s\n", ret == ESP_OK ? "EXITOSO" : "FALLIDO");
    }
    else {
        printf("Componente desconocido: %s\n", component);
        printf("Componentes disponibles: edge, decay, velocity, adaptive, all\n");
        return 1;
    }
    
    printf("=== Prueba completada ===\n\n");
    return 0;
}

static void register_test_command(void) {
    test_args.component = arg_str1(NULL, NULL, "<component>", "Componente a probar");
    test_args.end = arg_end(1);
    
    const esp_console_cmd_t cmd = {
        .command = "test",
        .help = "Ejecutar prueba de componente específico",
        .hint = NULL,
        .func = &cmd_test,
        .argtable = &test_args
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));
}

// ============================================================================
// COMANDOS DE CONFIGURACIÓN PERSISTENTE
// ============================================================================

/**
 * @brief Comando 'save' para guardar configuración
 */
static struct {
    struct arg_str *name;
    struct arg_end *end;
} save_args;

static int cmd_save(int argc, char **argv) {
    int nerrors = arg_parse(argc, argv, (void **) &save_args);
    if (nerrors != 0) {
        arg_print_errors(stderr, save_args.end, argv[0]);
        return 1;
    }
    
    const char *name = save_args.name->sval[0];
    esp_err_t ret = edrumulus_console_save_config(name);
    
    if (ret == ESP_OK) {
        printf("Configuración guardada como '%s'\n", name);
        strncpy(g_console_config.config_name, name, sizeof(g_console_config.config_name) - 1);
    } else {
        printf("Error al guardar configuración: %s\n", esp_err_to_name(ret));
    }
    
    return ret == ESP_OK ? 0 : 1;
}

static void register_save_command(void) {
    save_args.name = arg_str1(NULL, NULL, "<name>", "Nombre de la configuración");
    save_args.end = arg_end(1);
    
    const esp_console_cmd_t cmd = {
        .command = "save",
        .help = "Guardar configuración actual",
        .hint = NULL,
        .func = &cmd_save,
        .argtable = &save_args
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));
}

/**
 * @brief Comando 'load' para cargar configuración
 */
static struct {
    struct arg_str *name;
    struct arg_end *end;
} load_args;

static int cmd_load(int argc, char **argv) {
    int nerrors = arg_parse(argc, argv, (void **) &load_args);
    if (nerrors != 0) {
        arg_print_errors(stderr, load_args.end, argv[0]);
        return 1;
    }
    
    const char *name = load_args.name->sval[0];
    esp_err_t ret = edrumulus_console_load_config(name);
    
    if (ret == ESP_OK) {
        printf("Configuración '%s' cargada exitosamente\n", name);
        apply_config_to_validation_system();
        strncpy(g_console_config.config_name, name, sizeof(g_console_config.config_name) - 1);
    } else {
        printf("Error al cargar configuración '%s': %s\n", name, esp_err_to_name(ret));
    }
    
    return ret == ESP_OK ? 0 : 1;
}

static void register_load_command(void) {
    load_args.name = arg_str1(NULL, NULL, "<name>", "Nombre de la configuración");
    load_args.end = arg_end(1);
    
    const esp_console_cmd_t cmd = {
        .command = "load",
        .help = "Cargar configuración guardada",
        .hint = NULL,
        .func = &cmd_load,
        .argtable = &load_args
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));
}

// ============================================================================
// COMANDOS DE INFORMACIÓN
// ============================================================================

/**
 * @brief Comando 'show' para mostrar valores actuales
 */
static int cmd_show(int argc, char **argv) {
    edrumulus_console_show_values();
    return 0;
}

static void register_show_command(void) {
    const esp_console_cmd_t cmd = {
        .command = "show",
        .help = "Mostrar valores actuales de todos los parámetros",
        .hint = NULL,
        .func = &cmd_show,
        .argtable = NULL
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));
}

/**
 * @brief Comando 'help' personalizado
 */
static int cmd_help(int argc, char **argv) {
    edrumulus_console_show_help();
    return 0;
}

static void register_help_command(void) {
    const esp_console_cmd_t cmd = {
        .command = "help",
        .help = "Mostrar ayuda de comandos disponibles",
        .hint = NULL,
        .func = &cmd_help,
        .argtable = NULL
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));
}

/**
 * @brief Comando 'reset' para valores por defecto
 */
static int cmd_reset(int argc, char **argv) {
    esp_err_t ret = edrumulus_console_reset_defaults();
    if (ret == ESP_OK) {
        printf("Parámetros restablecidos a valores por defecto\n");
        apply_config_to_validation_system();
    } else {
        printf("Error al restablecer parámetros: %s\n", esp_err_to_name(ret));
    }
    return ret == ESP_OK ? 0 : 1;
}

static void register_reset_command(void) {
    const esp_console_cmd_t cmd = {
        .command = "reset",
        .help = "Restablecer todos los parámetros a valores por defecto",
        .hint = NULL,
        .func = &cmd_reset,
        .argtable = NULL
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));
}

// ============================================================================
// FUNCIONES PÚBLICAS
// ============================================================================

// Función para procesar comandos simples
void edrumulus_console_process_command(const char* input) {
    char command[32];
    char param[64];
    float value;
    
    // Parsear comando
    int args = sscanf(input, "%31s %63s %f", command, param, &value);
    
    if (strcmp(command, "help") == 0) {
        printf("\n=== Sistema de Comandos Phase 3 ===\n");
        printf("\nComandos disponibles:\n");
        printf("\n• show\n");
        printf("  Mostrar configuración actual de todos los módulos\n");
        
        printf("\n• test <tipo>\n");
        printf("  Ejecutar pruebas individuales o todas\n");
        printf("  Tipos: edge, decay, velocity, adaptive, all\n");
        
        printf("\n• set <parámetro> <valor>\n");
        printf("  Establecer parámetros de configuración\n");
        printf("  Edge Detector: edge_threshold, edge_sensitivity, rise_rate\n");
        printf("  Decay Analyzer: tau_min, tau_max, r_squared\n");
        printf("  Velocity Validator: linearity, repeatability, dynamic_min, dynamic_max\n");
        printf("  Adaptive Threshold: snr_target, adaptation_time, stability\n");
        
        printf("\n• reset\n");
        printf("  Restablecer todos los parámetros a valores por defecto\n");
        
        printf("\n• save [nombre]\n");
        printf("  Guardar configuración actual en NVS\n");
        printf("  Si no se especifica nombre, usa 'default'\n");
        
        printf("\n• load [nombre]\n");
        printf("  Cargar configuración desde NVS\n");
        printf("  Si no se especifica nombre, usa 'default'\n");
        
        printf("\n• help\n");
        printf("  Mostrar esta ayuda\n");
        
        printf("\nEjemplos:\n");
        printf("  set edge_threshold 200\n");
        printf("  save mi_config\n");
        printf("  load mi_config\n");
        printf("  test edge\n");
        printf("  show\n");
        printf("\n===================================\n\n");
    }
    else if (strcmp(command, "show") == 0) {
        // Mostrar configuración actual
        printf("\n=== Configuración Phase 3 Actual ===\n");
        printf("\n[Edge Detector]\n");
        printf("  Threshold: %.0f\n", g_console_config.edge_threshold);
        printf("  Sensitivity: %.2f\n", g_console_config.edge_sensitivity);
        printf("  Rise Rate: %.2f\n", g_console_config.rise_rate_threshold);
        
        printf("\n[Decay Analyzer]\n");
        printf("  Tau Min: %.4f\n", g_console_config.tau_min);
        printf("  Tau Max: %.4f\n", g_console_config.tau_max);
        printf("  R² Threshold: %.3f\n", g_console_config.r_squared_threshold);
        
        printf("\n[Velocity Validator]\n");
        printf("  Linearity: %.3f\n", g_console_config.linearity_threshold);
        printf("  Repeatability: %.3f\n", g_console_config.repeatability_threshold);
        printf("  Dynamic Min: %.2f\n", g_console_config.dynamic_range_min);
        printf("  Dynamic Max: %.2f\n", g_console_config.dynamic_range_max);
        
        printf("\n[Adaptive Threshold]\n");
        printf("  SNR Target: %.2f\n", g_console_config.snr_target);
        printf("  Adaptation Time: %.3f\n", g_console_config.adaptation_time);
        printf("  Stability: %.3f\n", g_console_config.stability_threshold);
        printf("\n====================================\n\n");
    }
    else if (strcmp(command, "test") == 0 && args >= 2) {
        if (strcmp(param, "edge") == 0) {
            printf("Ejecutando test Edge Detector...\n");
            esp_err_t ret = edrumulus_console_test_edge_detector();
            printf("Test Edge Detector: %s\n", ret == ESP_OK ? "PASSED" : "FAILED");
        } else if (strcmp(param, "decay") == 0) {
            printf("Ejecutando test Decay Analyzer...\n");
            esp_err_t ret = edrumulus_console_test_decay_analyzer();
            printf("Test Decay Analyzer: %s\n", ret == ESP_OK ? "PASSED" : "FAILED");
        } else if (strcmp(param, "velocity") == 0) {
            printf("Ejecutando test Velocity Validator...\n");
            esp_err_t ret = edrumulus_console_test_velocity_validator();
            printf("Test Velocity Validator: %s\n", ret == ESP_OK ? "PASSED" : "FAILED");
        } else if (strcmp(param, "adaptive") == 0) {
            printf("Ejecutando test Adaptive Threshold...\n");
            esp_err_t ret = edrumulus_console_test_adaptive_threshold();
            printf("Test Adaptive Threshold: %s\n", ret == ESP_OK ? "PASSED" : "FAILED");
        } else if (strcmp(param, "all") == 0) {
            printf("Ejecutando todas las pruebas Phase 3...\n");
            esp_err_t ret1 = edrumulus_console_test_edge_detector();
            esp_err_t ret2 = edrumulus_console_test_decay_analyzer();
            esp_err_t ret3 = edrumulus_console_test_velocity_validator();
            esp_err_t ret4 = edrumulus_console_test_adaptive_threshold();
            printf("\nResultados:\n");
            printf("  Edge Detector: %s\n", ret1 == ESP_OK ? "PASSED" : "FAILED");
            printf("  Decay Analyzer: %s\n", ret2 == ESP_OK ? "PASSED" : "FAILED");
            printf("  Velocity Validator: %s\n", ret3 == ESP_OK ? "PASSED" : "FAILED");
            printf("  Adaptive Threshold: %s\n", ret4 == ESP_OK ? "PASSED" : "FAILED");
        } else {
            printf("Tipo de prueba no válido.\n");
            printf("Opciones: edge, decay, velocity, adaptive, all\n");
        }
    }
    else if (strcmp(command, "set") == 0 && args >= 3) {
        // Edge Detector parameters
        if (strcmp(param, "edge_threshold") == 0) {
            g_console_config.edge_threshold = value;
            printf("Edge threshold establecido a: %.0f\n", value);
        } else if (strcmp(param, "edge_sensitivity") == 0) {
            g_console_config.edge_sensitivity = value;
            printf("Edge sensitivity establecido a: %.2f\n", value);
        } else if (strcmp(param, "rise_rate") == 0) {
            g_console_config.rise_rate_threshold = value;
            printf("Rise rate threshold establecido a: %.2f\n", value);
        }
        // Decay Analyzer parameters
        else if (strcmp(param, "tau_min") == 0) {
            g_console_config.tau_min = value;
            printf("Tau min establecido a: %.4f\n", value);
        } else if (strcmp(param, "tau_max") == 0) {
            g_console_config.tau_max = value;
            printf("Tau max establecido a: %.4f\n", value);
        } else if (strcmp(param, "r_squared") == 0) {
            g_console_config.r_squared_threshold = value;
            printf("R² threshold establecido a: %.3f\n", value);
        }
        // Velocity Validator parameters
        else if (strcmp(param, "linearity") == 0) {
            g_console_config.linearity_threshold = value;
            printf("Linearity threshold establecido a: %.3f\n", value);
        } else if (strcmp(param, "repeatability") == 0) {
            g_console_config.repeatability_threshold = value;
            printf("Repeatability threshold establecido a: %.3f\n", value);
        } else if (strcmp(param, "dynamic_min") == 0) {
            g_console_config.dynamic_range_min = value;
            printf("Dynamic range min establecido a: %.2f\n", value);
        } else if (strcmp(param, "dynamic_max") == 0) {
            g_console_config.dynamic_range_max = value;
            printf("Dynamic range max establecido a: %.2f\n", value);
        }
        // Adaptive Threshold parameters
        else if (strcmp(param, "snr_target") == 0) {
            g_console_config.snr_target = value;
            printf("SNR target establecido a: %.2f\n", value);
        } else if (strcmp(param, "adaptation_time") == 0) {
            g_console_config.adaptation_time = value;
            printf("Adaptation time establecido a: %.3f\n", value);
        } else if (strcmp(param, "stability") == 0) {
            g_console_config.stability_threshold = value;
            printf("Stability threshold establecido a: %.3f\n", value);
        } else {
            printf("Parámetro no válido: %s\n", param);
            printf("\nParámetros disponibles:\n");
            printf("Edge Detector: edge_threshold, edge_sensitivity, rise_rate\n");
            printf("Decay Analyzer: tau_min, tau_max, r_squared\n");
            printf("Velocity Validator: linearity, repeatability, dynamic_min, dynamic_max\n");
            printf("Adaptive Threshold: snr_target, adaptation_time, stability\n");
        }
        apply_config_to_validation_system();
    }
    else if (strcmp(command, "reset") == 0) {
        // Restablecer valores por defecto para todos los módulos Phase 3
        printf("Restableciendo configuración Phase 3 a valores por defecto...\n");
        
        // Edge Detector defaults
        g_console_config.edge_threshold = 150.0f;
        g_console_config.edge_sensitivity = 1.0f;
        g_console_config.rise_rate_threshold = 0.5f;
        
        // Decay Analyzer defaults
        g_console_config.tau_min = 0.001f;
        g_console_config.tau_max = 0.1f;
        g_console_config.r_squared_threshold = 0.95f;
        
        // Velocity Validator defaults
        g_console_config.linearity_threshold = 0.85f;
        g_console_config.repeatability_threshold = 0.90f;
        g_console_config.dynamic_range_min = 10.0f;
        g_console_config.dynamic_range_max = 127.0f;
        
        // Adaptive Threshold defaults
        g_console_config.snr_target = 20.0f;
        g_console_config.adaptation_time = 1.0f;
        g_console_config.stability_threshold = 0.95f;
        
        // Auto-save configuration
        g_console_config.auto_save_enabled = true;
        strncpy(g_console_config.config_name, "default", sizeof(g_console_config.config_name) - 1);
        
        apply_config_to_validation_system();
        printf("Configuración Phase 3 restablecida exitosamente\n");
        printf("Use 'show' para ver los nuevos valores\n");
    }
    else if (strncmp(command, "save", 4) == 0) {
        char config_name[32] = "default";
        
        // Extraer nombre de configuración si se proporciona
        char *space_pos = strchr(command, ' ');
        if (space_pos != NULL) {
            space_pos++; // Saltar el espacio
            strncpy(config_name, space_pos, sizeof(config_name) - 1);
            config_name[sizeof(config_name) - 1] = '\0';
        }
        
        printf("Guardando configuración '%s' en NVS...\n", config_name);
        
        // Actualizar nombre en la configuración
        strncpy(g_console_config.config_name, config_name, sizeof(g_console_config.config_name) - 1);
        
        // Guardar usando la función existente
        esp_err_t ret = edrumulus_console_save_config(config_name);
        if (ret == ESP_OK) {
            printf("Configuración '%s' guardada exitosamente\n", config_name);
        } else {
            printf("Error al guardar configuración: %s\n", esp_err_to_name(ret));
        }
    }
    else if (strncmp(command, "load", 4) == 0) {
        char config_name[32] = "default";
        
        // Extraer nombre de configuración si se proporciona
        char *space_pos = strchr(command, ' ');
        if (space_pos != NULL) {
            space_pos++; // Saltar el espacio
            strncpy(config_name, space_pos, sizeof(config_name) - 1);
            config_name[sizeof(config_name) - 1] = '\0';
        }
        
        printf("Cargando configuración '%s' desde NVS...\n", config_name);
        
        // Cargar usando la función existente
        esp_err_t ret = edrumulus_console_load_config(config_name);
        if (ret == ESP_OK) {
            printf("Configuración '%s' cargada exitosamente\n", config_name);
            apply_config_to_validation_system();
            printf("Use 'show' para ver los valores cargados\n");
        } else {
            printf("Error al cargar configuración: %s\n", esp_err_to_name(ret));
            printf("Usando configuración por defecto\n");
        }
    }
    else {
        printf("Comando no reconocido: %s\n", command);
        printf("Escriba 'help' para ver comandos disponibles\n");
    }
}

esp_err_t edrumulus_console_init(void) {
    ESP_LOGI(TAG, "Inicializando sistema de comandos por consola simplificado");
    
    // Aplicar configuración inicial
    apply_config_to_validation_system();
    
    ESP_LOGI(TAG, "Sistema de comandos simplificado inicializado exitosamente");
    printf("\n=== Sistema de Comandos Phase 3 Activo (Simplificado) ===\n");
    printf("Comandos disponibles:\n");
    printf("- show: Mostrar valores actuales\n");
    printf("- test <tipo>: Ejecutar pruebas (edge, decay, velocity, adaptive, all)\n");
    printf("- set <param> <value>: Establecer parámetro\n");
    printf("- reset: Restablecer valores por defecto\n");
    printf("- help: Mostrar esta ayuda\n\n");
    
    return ESP_OK;
}

esp_err_t edrumulus_console_get_config(edrumulus_console_config_t *config) {
    if (config == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    memcpy(config, &g_console_config, sizeof(edrumulus_console_config_t));
    return ESP_OK;
}

esp_err_t edrumulus_console_set_config(const edrumulus_console_config_t *config) {
    if (config == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    memcpy(&g_console_config, config, sizeof(edrumulus_console_config_t));
    apply_config_to_validation_system();
    
    return ESP_OK;
}

esp_err_t edrumulus_console_save_config(const char *name) {
    if (name == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    nvs_handle_t nvs_handle;
    esp_err_t ret = nvs_open("edrumulus_cfg", NVS_READWRITE, &nvs_handle);
    if (ret != ESP_OK) {
        return ret;
    }
    
    ret = nvs_set_blob(nvs_handle, name, &g_console_config, sizeof(edrumulus_console_config_t));
    if (ret == ESP_OK) {
        ret = nvs_commit(nvs_handle);
    }
    
    nvs_close(nvs_handle);
    return ret;
}

esp_err_t edrumulus_console_load_config(const char *name) {
    if (name == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    nvs_handle_t nvs_handle;
    esp_err_t ret = nvs_open("edrumulus_cfg", NVS_READONLY, &nvs_handle);
    if (ret != ESP_OK) {
        return ret;
    }
    
    size_t required_size = sizeof(edrumulus_console_config_t);
    ret = nvs_get_blob(nvs_handle, name, &g_console_config, &required_size);
    
    nvs_close(nvs_handle);
    return ret;
}

void edrumulus_console_show_values(void) {
    printf("\n=== Configuración Actual de Parámetros Phase 3 ===\n");
    printf("\nEdge Detector:\n");
    printf("  edge_threshold:    %.2f\n", g_console_config.edge_threshold);
    printf("  edge_sensitivity:  %.3f\n", g_console_config.edge_sensitivity);
    printf("  rise_rate:         %.2f\n", g_console_config.rise_rate_threshold);
    
    printf("\nDecay Analyzer:\n");
    printf("  tau_min:           %.6f\n", g_console_config.tau_min);
    printf("  tau_max:           %.6f\n", g_console_config.tau_max);
    printf("  r_squared:         %.3f\n", g_console_config.r_squared_threshold);
    
    printf("\nVelocity Validator:\n");
    printf("  linearity:         %.3f\n", g_console_config.linearity_threshold);
    printf("  repeatability:     %.3f\n", g_console_config.repeatability_threshold);
    printf("  range_min:         %.1f\n", g_console_config.dynamic_range_min);
    printf("  range_max:         %.1f\n", g_console_config.dynamic_range_max);
    
    printf("\nAdaptive Threshold:\n");
    printf("  snr_target:        %.1f dB\n", g_console_config.snr_target);
    printf("  adaptation_time:   %.3f s\n", g_console_config.adaptation_time);
    printf("  stability:         %.3f\n", g_console_config.stability_threshold);
    
    printf("\nConfiguración:\n");
    printf("  config_name:       %s\n", g_console_config.config_name);
    printf("  auto_save:         %s\n", g_console_config.auto_save_enabled ? "habilitado" : "deshabilitado");
    printf("\n================================================\n\n");
}

void edrumulus_console_show_help(void) {
    printf("\n=== Comandos Disponibles ===\n");
    printf("\nConfiguración de Parámetros:\n");
    printf("  set <param> <value>  - Establecer valor de parámetro\n");
    printf("  show                 - Mostrar valores actuales\n");
    printf("  reset                - Restablecer valores por defecto\n");
    
    printf("\nPruebas:\n");
    printf("  test edge            - Probar Edge Detector\n");
    printf("  test decay           - Probar Decay Analyzer\n");
    printf("  test velocity        - Probar Velocity Validator\n");
    printf("  test adaptive        - Probar Adaptive Threshold\n");
    printf("  test all             - Ejecutar todas las pruebas\n");
    
    printf("\nConfiguración Persistente:\n");
    printf("  save <name>          - Guardar configuración actual\n");
    printf("  load <name>          - Cargar configuración guardada\n");
    
    printf("\nParámetros Disponibles para 'set':\n");
    printf("  Edge Detector:       edge_threshold, edge_sensitivity, rise_rate\n");
    printf("  Decay Analyzer:      tau_min, tau_max, r_squared\n");
    printf("  Velocity Validator:  linearity, repeatability, range_min, range_max\n");
    printf("  Adaptive Threshold:  snr_target, adaptation_time, stability\n");
    
    printf("\nEjemplos:\n");
    printf("  set edge_threshold 150\n");
    printf("  set snr_target 25.0\n");
    printf("  test edge\n");
    printf("  save mi_config\n");
    printf("  load mi_config\n");
    printf("\n============================\n\n");
}

esp_err_t edrumulus_console_reset_defaults(void) {
    // Restaurar valores por defecto
    g_console_config = (edrumulus_console_config_t) {
        .edge_threshold = 100.0f,
        .edge_sensitivity = 0.8f,
        .rise_rate_threshold = 50.0f,
        .tau_min = 0.001f,
        .tau_max = 0.1f,
        .r_squared_threshold = 0.95f,
        .linearity_threshold = 0.95f,
        .repeatability_threshold = 0.05f,
        .dynamic_range_min = 1.0f,
        .dynamic_range_max = 127.0f,
        .snr_target = 20.0f,
        .adaptation_time = 1.0f,
        .stability_threshold = 0.1f,
        .auto_save_enabled = false,
        .config_name = "default"
    };
    
    return ESP_OK;
}

// ============================================================================
// FUNCIONES DE PRUEBA (implementaciones básicas)
// ============================================================================

esp_err_t edrumulus_console_test_edge_detector(void) {
    ESP_LOGI(TAG, "Ejecutando prueba Edge Detector con parámetros actuales");
    
    // Llamar a la función de prueba del sistema de validación (canal 0)
    esp_err_t ret = edrumulus_phase3_test_edge_detector(0);
    
    if (ret == ESP_OK) {
        edrumulus_phase3_validation_t *results;
        if (edrumulus_phase3_get_validation_results(0, &results) == ESP_OK) {
            printf("Edge Detector - Sensibilidad: %.1f%%, Especificidad: %.1f%%\n",
                   results->edge_detector.sensitivity * 100.0f,
                   results->edge_detector.specificity * 100.0f);
        }
    }
    
    return ret;
}

esp_err_t edrumulus_console_test_decay_analyzer(void) {
    ESP_LOGI(TAG, "Ejecutando prueba Decay Analyzer con parámetros actuales");
    
    esp_err_t ret = edrumulus_phase3_test_decay_analyzer(0);
    
    if (ret == ESP_OK) {
        edrumulus_phase3_validation_t *results;
        if (edrumulus_phase3_get_validation_results(0, &results) == ESP_OK) {
            printf("Decay Analyzer - R²: %.3f, Error Tau: %.3f\n",
                   results->decay_analyzer.r_squared,
                   results->decay_analyzer.tau_error);
        }
    }
    
    return ret;
}

esp_err_t edrumulus_console_test_velocity_validator(void) {
    ESP_LOGI(TAG, "Ejecutando prueba Velocity Validator con parámetros actuales");
    
    esp_err_t ret = edrumulus_phase3_test_velocity_validator(0);
    
    if (ret == ESP_OK) {
        edrumulus_phase3_validation_t *results;
        if (edrumulus_phase3_get_validation_results(0, &results) == ESP_OK) {
            printf("Velocity Validator - Linealidad: %.3f, Repetibilidad: %.3f\n",
                   results->velocity_validator.velocity_linearity,
                   results->velocity_validator.velocity_repeatability);
        }
    }
    
    return ret;
}

esp_err_t edrumulus_console_test_adaptive_threshold(void) {
    ESP_LOGI(TAG, "Ejecutando prueba Adaptive Threshold con parámetros actuales");
    
    esp_err_t ret = edrumulus_phase3_test_adaptive_threshold(0);
    
    if (ret == ESP_OK) {
        edrumulus_phase3_validation_t *results;
        if (edrumulus_phase3_get_validation_results(0, &results) == ESP_OK) {
            printf("Adaptive Threshold - SNR: %.1f dB, Tiempo adaptación: %.1f ms\n",
                   results->adaptive_threshold.measured_snr_db,
                   results->adaptive_threshold.adaptation_time_ms);
        }
    }
    
    return ret;
}

esp_err_t edrumulus_console_test_all(void) {
    ESP_LOGI(TAG, "Ejecutando todas las pruebas Phase 3");
    
    printf("\n=== Ejecutando Suite Completa de Pruebas Phase 3 ===\n");
    
    esp_err_t ret_edge = edrumulus_console_test_edge_detector();
    esp_err_t ret_decay = edrumulus_console_test_decay_analyzer();
    esp_err_t ret_velocity = edrumulus_console_test_velocity_validator();
    esp_err_t ret_adaptive = edrumulus_console_test_adaptive_threshold();
    
    bool all_passed = (ret_edge == ESP_OK) && (ret_decay == ESP_OK) && 
                      (ret_velocity == ESP_OK) && (ret_adaptive == ESP_OK);
    
    printf("\n=== Resumen de Resultados ===\n");
    printf("Edge Detector:      %s\n", ret_edge == ESP_OK ? "PASS" : "FAIL");
    printf("Decay Analyzer:     %s\n", ret_decay == ESP_OK ? "PASS" : "FAIL");
    printf("Velocity Validator: %s\n", ret_velocity == ESP_OK ? "PASS" : "FAIL");
    printf("Adaptive Threshold: %s\n", ret_adaptive == ESP_OK ? "PASS" : "FAIL");
    printf("\nResultado General:  %s\n", all_passed ? "EXITOSO" : "REQUIERE AJUSTES");
    printf("=============================\n\n");
    
    return all_passed ? ESP_OK : ESP_FAIL;
}