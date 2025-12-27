/**
 * @file prefetch.cpp
 * @brief Эксперимент 3: Исследование эффективности предвыборки
 * 
 * Сравнивает последовательный и случайный доступ к памяти.
 * На МК без кэша или с простым кэшем эффект менее выражен.
 */

#include "common.hpp"

/**
 * @brief Простой линейный конгруэнтный генератор для случайного доступа
 */
static uint32_t lcg_state = 12345;

inline uint32_t lcg_random(uint32_t max) {
    lcg_state = lcg_state * 1103515245 + 12345;
    return (lcg_state >> 16) % max;
}

inline void lcg_seed(uint32_t seed) {
    lcg_state = seed;
}

/**
 * @brief Проводит эксперимент предвыборки
 * 
 * @param size_kb Размер буфера (КБ)
 * @param step Шаг чтения (байт)
 * @param iterations Количество итераций
 */
void prefetchExperiment(uint16_t size_kb, uint16_t step, uint16_t iterations) {
    uart_println("\n[EXP3] Prefetch / Access Pattern");
    uart_println("=================================");
    
    if (size_kb * 1024 > AVAILABLE_RAM) {
        size_kb = AVAILABLE_RAM / 1024;
    }
    
    if (step < 1) step = 64;
    if (iterations < 1) iterations = 100;
    
    size_t size = (size_t)size_kb * 1024;
    uint8_t* buffer = (uint8_t*)mc_malloc(size);
    
    if (!buffer) {
        uart_println("[EXP3] Failed to allocate memory");
        return;
    }
    
    // Инициализация
    for (size_t i = 0; i < size; i++) {
        buffer[i] = (uint8_t)(i & 0xFF);
    }
    
    uint32_t numAccesses = size / step;
    
    uart_print("[EXP3] Size: ");
    uart_print_uint(size_kb);
    uart_print("KB, Step: ");
    uart_print_uint(step);
    uart_print("B, Accesses: ");
    uart_print_uint(numAccesses);
    uart_println("");
    
    volatile uint32_t sum = 0;
    
    // Прогрев
    for (size_t i = 0; i < size; i += step) {
        sum += buffer[i];
    }
    
    // Измерение последовательного доступа
    ticks_t seqStart = get_ticks();
    for (uint16_t iter = 0; iter < iterations; iter++) {
        sum = 0;
        for (size_t i = 0; i < size; i += step) {
            sum += buffer[i];
        }
    }
    ticks_t seqEnd = get_ticks();
    ticks_t seqTicks = seqEnd - seqStart;
    
    // Подготовка индексов для случайного доступа
    lcg_seed(42);
    
    // Измерение случайного доступа
    ticks_t rndStart = get_ticks();
    for (uint16_t iter = 0; iter < iterations; iter++) {
        sum = 0;
        for (uint32_t j = 0; j < numAccesses; j++) {
            uint32_t idx = lcg_random(numAccesses) * step;
            sum += buffer[idx];
        }
    }
    ticks_t rndEnd = get_ticks();
    ticks_t rndTicks = rndEnd - rndStart;
    
    mc_free(buffer);
    
    float seqTimeUs = (float)seqTicks / (float)CPU_FREQ_MHZ;
    float rndTimeUs = (float)rndTicks / (float)CPU_FREQ_MHZ;
    float ratio = rndTimeUs / seqTimeUs;
    
    json_start();
    json_key_string("experiment", "prefetch", true);
    json_key_uint("size_kb", size_kb, true);
    json_key_uint("step", step, true);
    json_key_uint("iterations", iterations, true);
    json_key_float("sequential_time_us", seqTimeUs, 2, true);
    json_key_float("random_time_us", rndTimeUs, 2, true);
    json_key_float("random_to_sequential_ratio", ratio, 3, false);
    json_end();
    
    uart_print("[EXP3] Random access is ");
    uart_print_float(ratio, 2);
    uart_println("x slower than sequential");
}
