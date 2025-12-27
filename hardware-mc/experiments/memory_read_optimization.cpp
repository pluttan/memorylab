/**
 * @file memory_read_optimization.cpp
 * @brief Эксперимент 4: Оптимизация чтения памяти
 * 
 * Сравнивает различные способы чтения памяти:
 * - Побайтовое чтение
 * - Чтение 32-битными словами
 * - Чтение с разворачиванием цикла
 */

#include "common.hpp"

/**
 * @brief Проводит эксперимент оптимизации чтения памяти
 * 
 * @param size_kb Размер буфера (КБ)
 * @param iterations Количество итераций
 */
void memoryReadOptimizationExperiment(uint16_t size_kb, uint16_t iterations) {
    uart_println("\n[EXP4] Memory Read Optimization");
    uart_println("================================");
    
    if (size_kb * 1024 > AVAILABLE_RAM) {
        size_kb = AVAILABLE_RAM / 1024;
    }
    
    if (iterations < 1) iterations = 100;
    
    size_t size = (size_t)size_kb * 1024;
    
    // Выделяем с выравниванием по 4 байтам
    uint8_t* buffer = (uint8_t*)mc_malloc(size);
    if (!buffer) {
        uart_println("[EXP4] Failed to allocate memory");
        return;
    }
    
    // Инициализация
    for (size_t i = 0; i < size; i++) {
        buffer[i] = (uint8_t)(i & 0xFF);
    }
    
    volatile uint32_t sum = 0;
    
    // Прогрев
    for (size_t i = 0; i < size; i++) {
        sum += buffer[i];
    }
    
    uart_print("[EXP4] Size: ");
    uart_print_uint(size_kb);
    uart_print("KB, Iterations: ");
    uart_print_uint(iterations);
    uart_println("");
    
    // === Чтение побайтово ===
    ticks_t byteStart = get_ticks();
    for (uint16_t iter = 0; iter < iterations; iter++) {
        sum = 0;
        for (size_t i = 0; i < size; i++) {
            sum += buffer[i];
        }
    }
    ticks_t byteEnd = get_ticks();
    ticks_t byteTicks = byteEnd - byteStart;
    
    // === Чтение 32-битными словами ===
    uint32_t* buf32 = (uint32_t*)buffer;
    size_t numWords = size / 4;
    
    ticks_t wordStart = get_ticks();
    for (uint16_t iter = 0; iter < iterations; iter++) {
        sum = 0;
        for (size_t i = 0; i < numWords; i++) {
            // Суммируем все байты слова
            uint32_t word = buf32[i];
            sum += (word & 0xFF) + ((word >> 8) & 0xFF) + 
                   ((word >> 16) & 0xFF) + ((word >> 24) & 0xFF);
        }
    }
    ticks_t wordEnd = get_ticks();
    ticks_t wordTicks = wordEnd - wordStart;
    
    // === Чтение с разворачиванием цикла (4x unroll) ===
    ticks_t unrollStart = get_ticks();
    for (uint16_t iter = 0; iter < iterations; iter++) {
        sum = 0;
        size_t i = 0;
        for (; i + 3 < size; i += 4) {
            sum += buffer[i] + buffer[i+1] + buffer[i+2] + buffer[i+3];
        }
        for (; i < size; i++) {
            sum += buffer[i];
        }
    }
    ticks_t unrollEnd = get_ticks();
    ticks_t unrollTicks = unrollEnd - unrollStart;
    
    mc_free(buffer);
    
    float byteTimeUs = (float)byteTicks / (float)CPU_FREQ_MHZ;
    float wordTimeUs = (float)wordTicks / (float)CPU_FREQ_MHZ;
    float unrollTimeUs = (float)unrollTicks / (float)CPU_FREQ_MHZ;
    
    json_start();
    json_key_string("experiment", "memory_read_optimization", true);
    json_key_uint("size_kb", size_kb, true);
    json_key_uint("iterations", iterations, true);
    json_key_float("byte_read_time_us", byteTimeUs, 2, true);
    json_key_float("word_read_time_us", wordTimeUs, 2, true);
    json_key_float("unroll_read_time_us", unrollTimeUs, 2, true);
    json_key_float("word_speedup", byteTimeUs / wordTimeUs, 3, true);
    json_key_float("unroll_speedup", byteTimeUs / unrollTimeUs, 3, false);
    json_end();
    
    uart_println("[EXP4] Results:");
    uart_print("  Byte read:   ");
    uart_print_float(byteTimeUs, 2);
    uart_println(" us");
    uart_print("  Word read:   ");
    uart_print_float(wordTimeUs, 2);
    uart_print(" us (");
    uart_print_float(byteTimeUs / wordTimeUs, 2);
    uart_println("x faster)");
    uart_print("  Unroll read: ");
    uart_print_float(unrollTimeUs, 2);
    uart_print(" us (");
    uart_print_float(byteTimeUs / unrollTimeUs, 2);
    uart_println("x faster)");
}
