/**
 * @file common.hpp
 * @brief Общие утилиты для экспериментов на микроконтроллерах
 * 
 * Поддерживаемые платформы:
 * - STM32 (с HAL)
 * - AVR (Arduino)
 * - ESP32
 * - Общие ARM Cortex-M
 */

#ifndef EXPERIMENTS_COMMON_HPP
#define EXPERIMENTS_COMMON_HPP

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

// ==================== ОПРЕДЕЛЕНИЕ ПЛАТФОРМЫ ====================

#if defined(STM32F4) || defined(STM32F1) || defined(STM32F7) || defined(STM32H7)
    #define PLATFORM_STM32
    #include "stm32_hal.h"
#elif defined(__AVR__)
    #define PLATFORM_AVR
    #include <Arduino.h>
#elif defined(ESP32) || defined(ESP8266)
    #define PLATFORM_ESP
    #include <Arduino.h>
#elif defined(__arm__) || defined(__ARM_ARCH)
    #define PLATFORM_ARM_GENERIC
#else
    #define PLATFORM_UNKNOWN
    #warning "Unknown platform, using generic implementation"
#endif

// ==================== ТИПЫ ДАННЫХ ====================

typedef uint32_t ticks_t;
typedef uint32_t time_us_t;

// ==================== UART ВЫВОД ====================

/**
 * @brief Отправляет строку через UART (платформозависимо)
 */
inline void uart_print(const char* str) {
#if defined(PLATFORM_STM32)
    // STM32 HAL UART (предполагается huart2)
    extern UART_HandleTypeDef huart2;
    HAL_UART_Transmit(&huart2, (uint8_t*)str, strlen(str), HAL_MAX_DELAY);
#elif defined(PLATFORM_AVR) || defined(PLATFORM_ESP)
    Serial.print(str);
#else
    // Заглушка для неизвестных платформ
    (void)str;
#endif
}

inline void uart_println(const char* str) {
    uart_print(str);
    uart_print("\r\n");
}

inline void uart_print_int(int32_t value) {
    char buf[16];
#if defined(PLATFORM_AVR) || defined(PLATFORM_ESP)
    Serial.print(value);
#else
    snprintf(buf, sizeof(buf), "%ld", (long)value);
    uart_print(buf);
#endif
}

inline void uart_print_uint(uint32_t value) {
    char buf[16];
#if defined(PLATFORM_AVR) || defined(PLATFORM_ESP)
    Serial.print(value);
#else
    snprintf(buf, sizeof(buf), "%lu", (unsigned long)value);
    uart_print(buf);
#endif
}

inline void uart_print_float(float value, int decimals) {
    char buf[32];
#if defined(PLATFORM_AVR) || defined(PLATFORM_ESP)
    Serial.print(value, decimals);
#else
    // Простое преобразование float в строку
    int32_t intPart = (int32_t)value;
    float fracPart = value - intPart;
    if (fracPart < 0) fracPart = -fracPart;
    
    int32_t fracInt = 1;
    for (int i = 0; i < decimals; i++) fracInt *= 10;
    fracInt = (int32_t)(fracPart * fracInt);
    
    snprintf(buf, sizeof(buf), "%ld.%0*ld", (long)intPart, decimals, (long)fracInt);
    uart_print(buf);
#endif
}

// ==================== ТАЙМЕРЫ / СЧЁТЧИКИ ТАКТОВ ====================

/**
 * @brief Инициализация системы измерения времени
 */
inline void timer_init(void) {
#if defined(PLATFORM_STM32)
    // Включаем DWT (Data Watchpoint and Trace) для счётчика тактов
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
#elif defined(PLATFORM_AVR) || defined(PLATFORM_ESP)
    // Arduino использует micros()
#endif
}

/**
 * @brief Получает текущее значение счётчика тактов/времени
 */
inline ticks_t get_ticks(void) {
#if defined(PLATFORM_STM32)
    return DWT->CYCCNT;
#elif defined(PLATFORM_AVR) || defined(PLATFORM_ESP)
    return micros();
#else
    return 0;
#endif
}

/**
 * @brief Преобразует такты в микросекунды
 * @param ticks Количество тактов
 * @param cpu_freq_mhz Частота CPU в МГц (например, 72 для STM32F1, 168 для STM32F4)
 */
inline time_us_t ticks_to_us(ticks_t ticks, uint32_t cpu_freq_mhz) {
#if defined(PLATFORM_STM32) || defined(PLATFORM_ARM_GENERIC)
    return ticks / cpu_freq_mhz;
#elif defined(PLATFORM_AVR) || defined(PLATFORM_ESP)
    // Уже в микросекундах
    (void)cpu_freq_mhz;
    return ticks;
#else
    (void)cpu_freq_mhz;
    return ticks;
#endif
}

/**
 * @brief Получает текущее время в микросекундах
 */
inline time_us_t get_time_us(void) {
#if defined(PLATFORM_STM32)
    extern uint32_t SystemCoreClock;
    return DWT->CYCCNT / (SystemCoreClock / 1000000);
#elif defined(PLATFORM_AVR) || defined(PLATFORM_ESP)
    return micros();
#else
    return 0;
#endif
}

// ==================== ВЫДЕЛЕНИЕ ПАМЯТИ ====================

/**
 * @brief Выделяет память с выравниванием (или обычную для МК)
 * На микроконтроллерах без MMU выравнивание менее критично
 */
inline void* mc_malloc(size_t size) {
#if defined(PLATFORM_AVR)
    return malloc(size);
#elif defined(PLATFORM_ESP)
    return heap_caps_malloc(size, MALLOC_CAP_8BIT);
#else
    return malloc(size);
#endif
}

inline void mc_free(void* ptr) {
    free(ptr);
}

// ==================== ЗАДЕРЖКИ ====================

inline void delay_us(uint32_t us) {
#if defined(PLATFORM_STM32)
    uint32_t start = DWT->CYCCNT;
    extern uint32_t SystemCoreClock;
    uint32_t ticks = us * (SystemCoreClock / 1000000);
    while ((DWT->CYCCNT - start) < ticks);
#elif defined(PLATFORM_AVR) || defined(PLATFORM_ESP)
    delayMicroseconds(us);
#endif
}

inline void delay_ms(uint32_t ms) {
#if defined(PLATFORM_STM32)
    HAL_Delay(ms);
#elif defined(PLATFORM_AVR) || defined(PLATFORM_ESP)
    delay(ms);
#endif
}

// ==================== ФЛАГ ОТМЕНЫ ====================

static volatile bool cancelExperiment = false;

inline void setCancelExperiment(bool cancel) {
    cancelExperiment = cancel;
}

inline bool isCancelled(void) {
    return cancelExperiment;
}

// ==================== ВЫВОД РЕЗУЛЬТАТОВ JSON ====================

/**
 * @brief Начало JSON объекта
 */
inline void json_start(void) {
    uart_print("{");
}

inline void json_end(void) {
    uart_println("}");
}

inline void json_key_int(const char* key, int32_t value, bool comma) {
    uart_print("\"");
    uart_print(key);
    uart_print("\":");
    uart_print_int(value);
    if (comma) uart_print(",");
}

inline void json_key_uint(const char* key, uint32_t value, bool comma) {
    uart_print("\"");
    uart_print(key);
    uart_print("\":");
    uart_print_uint(value);
    if (comma) uart_print(",");
}

inline void json_key_float(const char* key, float value, int decimals, bool comma) {
    uart_print("\"");
    uart_print(key);
    uart_print("\":");
    uart_print_float(value, decimals);
    if (comma) uart_print(",");
}

inline void json_key_string(const char* key, const char* value, bool comma) {
    uart_print("\"");
    uart_print(key);
    uart_print("\":\"");
    uart_print(value);
    uart_print("\"");
    if (comma) uart_print(",");
}

inline void json_array_start(const char* key) {
    uart_print("\"");
    uart_print(key);
    uart_print("\":[");
}

inline void json_array_end(bool comma) {
    uart_print("]");
    if (comma) uart_print(",");
}

// ==================== КОНФИГУРАЦИЯ ЭКСПЕРИМЕНТА ====================

/**
 * @brief Частота CPU в МГц (настраивается для каждой платформы)
 */
#if defined(PLATFORM_STM32)
    #define CPU_FREQ_MHZ (SystemCoreClock / 1000000)
#elif defined(PLATFORM_AVR)
    #define CPU_FREQ_MHZ (F_CPU / 1000000)
#elif defined(PLATFORM_ESP)
    #define CPU_FREQ_MHZ 240
#else
    #define CPU_FREQ_MHZ 1
#endif

/**
 * @brief Размер доступной RAM для экспериментов (в байтах)
 * Настраивается под конкретный МК
 */
#if defined(PLATFORM_AVR)
    #define AVAILABLE_RAM 1500      // ATmega328P: ~2KB RAM
#elif defined(PLATFORM_ESP)
    #define AVAILABLE_RAM 200000    // ESP32: ~320KB RAM
#elif defined(STM32F1)
    #define AVAILABLE_RAM 15000     // STM32F103: 20KB RAM
#elif defined(STM32F4)
    #define AVAILABLE_RAM 100000    // STM32F4xx: 128KB+ RAM
#else
    #define AVAILABLE_RAM 10000     // По умолчанию
#endif

#endif // EXPERIMENTS_COMMON_HPP
