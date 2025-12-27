/**
 * @file cache_conflicts.cpp
 * @brief Эксперимент 5: Исследование конфликтов в кэш-памяти
 * 
 * Исследует влияние конфликтов в кэш-памяти на время доступа.
 * Доступ с шагом = размер банка вызывает конфликты.
 * Доступ с шагом = размер банка + линейка избегает конфликтов.
 */

#include "common.hpp"

/**
 * @brief Размер линейки кэша (для большинства ARM - 32 байта)
 */
#ifndef CACHE_LINE_SIZE
    #if defined(PLATFORM_STM32) || defined(PLATFORM_ARM_GENERIC)
        #define CACHE_LINE_SIZE 32
    #else
        #define CACHE_LINE_SIZE 64
    #endif
#endif

/**
 * @brief Размер банка кэша L1 (типично 4-16 КБ)
 */
#ifndef CACHE_BANK_SIZE
    #if defined(STM32F7) || defined(STM32H7)
        #define CACHE_BANK_SIZE (16 * 1024)  // F7/H7 имеют кэш
    #elif defined(PLATFORM_ESP)
        #define CACHE_BANK_SIZE (16 * 1024)
    #else
        #define CACHE_BANK_SIZE (8 * 1024)   // Условный размер для демонстрации
    #endif
#endif

/**
 * @brief Проводит эксперимент конфликтов кэш-памяти
 * 
 * @param bank_size_kb Размер банка кэш-памяти (КБ), 0 = авто
 * @param line_size Размер линейки (байт), 0 = авто
 * @param num_lines Количество тестируемых линеек
 */
void cacheConflictsExperiment(uint16_t bank_size_kb, uint8_t line_size, uint16_t num_lines) {
    uart_println("\n[EXP5] Cache Conflicts");
    uart_println("======================");
    
    // Автоопределение параметров
    if (bank_size_kb == 0) bank_size_kb = CACHE_BANK_SIZE / 1024;
    if (line_size == 0) line_size = CACHE_LINE_SIZE;
    if (num_lines < 2) num_lines = 16;
    if (num_lines > 64) num_lines = 64;
    
    size_t bankSize = (size_t)bank_size_kb * 1024;
    size_t totalSize = (bankSize + line_size) * num_lines;
    
    // Проверка доступной памяти
    if (totalSize > AVAILABLE_RAM) {
        num_lines = AVAILABLE_RAM / (bankSize + line_size);
        totalSize = (bankSize + line_size) * num_lines;
        uart_print("[EXP5] Limited to ");
        uart_print_uint(num_lines);
        uart_println(" lines");
    }
    
    uint8_t* buffer = (uint8_t*)mc_malloc(totalSize);
    if (!buffer) {
        uart_println("[EXP5] Failed to allocate memory");
        json_start();
        json_key_string("error", "Memory allocation failed", false);
        json_end();
        return;
    }
    
    // Инициализация
    memset(buffer, 0xAA, totalSize);
    
    uart_print("[EXP5] Bank: ");
    uart_print_uint(bank_size_kb);
    uart_print("KB, Line: ");
    uart_print_uint(line_size);
    uart_print("B, Lines: ");
    uart_print_uint(num_lines);
    uart_println("");
    
    const uint16_t NUM_ITERATIONS = 1000;
    
    json_start();
    json_key_string("experiment", "cache_conflicts", true);
    json_key_uint("bank_size_kb", bank_size_kb, true);
    json_key_uint("line_size_b", line_size, true);
    json_key_uint("num_lines", num_lines, true);
    json_array_start("dataPoints");
    
    bool firstPoint = true;
    
    for (uint16_t lineIdx = 0; lineIdx < num_lines; lineIdx++) {
        if (isCancelled()) {
            uart_print("],\"cancelled\":true}");
            mc_free(buffer);
            return;
        }
        
        // Смещение с конфликтами (шаг = bankSize)
        size_t offsetConflict = (size_t)lineIdx * bankSize;
        
        // Смещение без конфликтов (шаг = bankSize + lineSize)
        size_t offsetNoConflict = (size_t)lineIdx * (bankSize + line_size);
        
        // Защита от выхода за границы
        if (offsetConflict >= totalSize) offsetConflict = totalSize - 1;
        if (offsetNoConflict >= totalSize) offsetNoConflict = totalSize - 1;
        
        volatile uint8_t sum = 0;
        
        // Измерение доступа С конфликтами
        ticks_t conflictStart = get_ticks();
        for (uint16_t iter = 0; iter < NUM_ITERATIONS; iter++) {
            sum += buffer[offsetConflict];
        }
        ticks_t conflictEnd = get_ticks();
        ticks_t conflictTicks = conflictEnd - conflictStart;
        
        // Измерение доступа БЕЗ конфликтов
        ticks_t noConflictStart = get_ticks();
        for (uint16_t iter = 0; iter < NUM_ITERATIONS; iter++) {
            sum += buffer[offsetNoConflict];
        }
        ticks_t noConflictEnd = get_ticks();
        ticks_t noConflictTicks = noConflictEnd - noConflictStart;
        
        // Время в наносекундах за один доступ
        float conflictTimeNs = (float)conflictTicks * 1000.0f / (float)CPU_FREQ_MHZ / (float)NUM_ITERATIONS;
        float noConflictTimeNs = (float)noConflictTicks * 1000.0f / (float)CPU_FREQ_MHZ / (float)NUM_ITERATIONS;
        
        // Вывод точки данных
        if (!firstPoint) uart_print(",");
        firstPoint = false;
        
        uart_print("{\"line\":");
        uart_print_uint(lineIdx);
        uart_print(",\"conflict_ns\":");
        uart_print_float(conflictTimeNs, 2);
        uart_print(",\"no_conflict_ns\":");
        uart_print_float(noConflictTimeNs, 2);
        uart_print("}");
    }
    
    mc_free(buffer);
    
    json_array_end(false);
    json_end();
}
