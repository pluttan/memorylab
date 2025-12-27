/**
 * @file memory_stratification.cpp
 * @brief Эксперимент 1: Исследование расслоения динамической памяти
 * 
 * Для микроконтроллеров - определение границ кэшей L1/L2/L3
 * путём измерения времени доступа к памяти разного размера.
 */

#include "common.hpp"

/**
 * @brief Проводит эксперимент расслоения памяти
 * 
 * @param max_size_kb Максимальный размер тестируемой области (КБ)
 * @param step_kb Шаг увеличения размера (КБ)
 * @param iterations Количество итераций для усреднения
 */
void memoryStratificationExperiment(uint16_t max_size_kb, uint16_t step_kb, uint16_t iterations) {
    uart_println("\n[EXP1] Memory Stratification");
    uart_println("============================");
    
    // Ограничение по доступной памяти
    if (max_size_kb * 1024 > AVAILABLE_RAM) {
        max_size_kb = AVAILABLE_RAM / 1024;
        uart_print("[EXP1] Limited to ");
        uart_print_uint(max_size_kb);
        uart_println(" KB due to RAM constraints");
    }
    
    if (step_kb < 1) step_kb = 1;
    if (iterations < 1) iterations = 100;
    
    json_start();
    json_key_string("experiment", "memory_stratification", true);
    json_key_uint("max_size_kb", max_size_kb, true);
    json_key_uint("step_kb", step_kb, true);
    json_key_uint("iterations", iterations, true);
    json_array_start("dataPoints");
    
    bool firstPoint = true;
    
    for (uint16_t size_kb = step_kb; size_kb <= max_size_kb; size_kb += step_kb) {
        if (isCancelled()) {
            uart_println("\"cancelled\":true}");
            return;
        }
        
        size_t size = (size_t)size_kb * 1024;
        uint8_t* buffer = (uint8_t*)mc_malloc(size);
        
        if (!buffer) {
            uart_print("[EXP1] Failed to allocate ");
            uart_print_uint(size_kb);
            uart_println(" KB");
            break;
        }
        
        // Инициализация буфера
        for (size_t i = 0; i < size; i++) {
            buffer[i] = (uint8_t)(i & 0xFF);
        }
        
        // Прогрев
        volatile uint32_t sum = 0;
        for (size_t i = 0; i < size; i += 64) {
            sum += buffer[i];
        }
        
        // Измерение
        ticks_t start = get_ticks();
        for (uint16_t iter = 0; iter < iterations; iter++) {
            sum = 0;
            for (size_t i = 0; i < size; i += 64) {
                sum += buffer[i];
            }
        }
        ticks_t end = get_ticks();
        
        mc_free(buffer);
        
        ticks_t total_ticks = end - start;
        uint32_t accesses = (size / 64) * iterations;
        float time_per_access_ns = (float)total_ticks * 1000.0f / (float)CPU_FREQ_MHZ / (float)accesses;
        
        // Вывод точки данных
        if (!firstPoint) uart_print(",");
        firstPoint = false;
        
        uart_print("{\"size_kb\":");
        uart_print_uint(size_kb);
        uart_print(",\"time_ns\":");
        uart_print_float(time_per_access_ns, 2);
        uart_print(",\"ticks\":");
        uart_print_uint(total_ticks);
        uart_print("}");
    }
    
    json_array_end(false);
    json_end();
}
