/**
 * @file main.cpp
 * @brief Главный файл для запуска экспериментов на микроконтроллерах
 * 
 * Поддерживаемые платформы:
 * - STM32 (с CubeMX/HAL)
 * - Arduino (AVR/ESP)
 * 
 * Управление через UART:
 * - '1' - Эксперимент расслоения памяти
 * - '2' - Список vs Массив
 * - '3' - Предвыборка (последовательный vs случайный доступ)
 * - '4' - Оптимизация чтения памяти
 * - '5' - Конфликты кэш-памяти
 * - '6' - Алгоритмы сортировки
 * - 'a' - Запустить все эксперименты
 * - 'h' - Справка
 */

#include "experiments/common.hpp"
#include "experiments/memory_stratification.cpp"
#include "experiments/list_vs_array.cpp"
#include "experiments/prefetch.cpp"
#include "experiments/memory_read_optimization.cpp"
#include "experiments/cache_conflicts.cpp"
#include "experiments/sorting_algorithms.cpp"

// ==================== КОНФИГУРАЦИЯ ====================

/**
 * @brief Конфигурация экспериментов по умолчанию
 * Настройте под возможности вашего МК
 */
struct ExperimentConfig {
    // Эксперимент 1: Расслоение памяти
    uint16_t exp1_max_size_kb = 8;      // Ограничено RAM
    uint16_t exp1_step_kb = 1;
    uint16_t exp1_iterations = 100;
    
    // Эксперимент 2: Список vs Массив
    uint16_t exp2_elements = 500;
    uint16_t exp2_iterations = 100;
    
    // Эксперимент 3: Предвыборка
    uint16_t exp3_size_kb = 4;
    uint16_t exp3_step = 64;
    uint16_t exp3_iterations = 100;
    
    // Эксперимент 4: Оптимизация чтения
    uint16_t exp4_size_kb = 2;
    uint16_t exp4_iterations = 100;
    
    // Эксперимент 5: Конфликты кэша
    uint16_t exp5_bank_size_kb = 0;     // 0 = авто
    uint8_t exp5_line_size = 0;         // 0 = авто
    uint16_t exp5_num_lines = 32;
    
    // Эксперимент 6: Сортировка
    uint16_t exp6_elements = 200;
};

static ExperimentConfig config;

// ==================== UART ПРИЁМ ====================

/**
 * @brief Читает символ из UART (платформозависимо)
 * @return Прочитанный символ или 0 если нет данных
 */
char uart_read_char(void) {
#if defined(PLATFORM_STM32)
    extern UART_HandleTypeDef huart2;
    uint8_t ch = 0;
    if (HAL_UART_Receive(&huart2, &ch, 1, 10) == HAL_OK) {
        return (char)ch;
    }
    return 0;
#elif defined(PLATFORM_AVR) || defined(PLATFORM_ESP)
    if (Serial.available()) {
        return (char)Serial.read();
    }
    return 0;
#else
    return 0;
#endif
}

// ==================== МЕНЮ ====================

void printHelp(void) {
    uart_println("");
    uart_println("=================================");
    uart_println(" Memory Lab for Microcontrollers");
    uart_println("=================================");
    uart_println("");
    uart_println("Commands:");
    uart_println("  1 - Memory Stratification");
    uart_println("  2 - List vs Array");
    uart_println("  3 - Prefetch / Access Pattern");
    uart_println("  4 - Memory Read Optimization");
    uart_println("  5 - Cache Conflicts");
    uart_println("  6 - Sorting Algorithms");
    uart_println("  a - Run All Experiments");
    uart_println("  h - Show this help");
    uart_println("");
    uart_print("Available RAM: ");
    uart_print_uint(AVAILABLE_RAM);
    uart_println(" bytes");
    uart_print("CPU Frequency: ");
    uart_print_uint(CPU_FREQ_MHZ);
    uart_println(" MHz");
    uart_println("");
}

void runAllExperiments(void) {
    uart_println("\n*** Running All Experiments ***\n");
    
    memoryStratificationExperiment(config.exp1_max_size_kb, 
                                    config.exp1_step_kb, 
                                    config.exp1_iterations);
    
    listVsArrayExperiment(config.exp2_elements, config.exp2_iterations);
    
    prefetchExperiment(config.exp3_size_kb, config.exp3_step, config.exp3_iterations);
    
    memoryReadOptimizationExperiment(config.exp4_size_kb, config.exp4_iterations);
    
    cacheConflictsExperiment(config.exp5_bank_size_kb, 
                              config.exp5_line_size, 
                              config.exp5_num_lines);
    
    sortingAlgorithmsExperiment(config.exp6_elements);
    
    uart_println("\n*** All Experiments Complete ***\n");
}

void processCommand(char cmd) {
    switch (cmd) {
        case '1':
            memoryStratificationExperiment(config.exp1_max_size_kb, 
                                            config.exp1_step_kb, 
                                            config.exp1_iterations);
            break;
            
        case '2':
            listVsArrayExperiment(config.exp2_elements, config.exp2_iterations);
            break;
            
        case '3':
            prefetchExperiment(config.exp3_size_kb, config.exp3_step, config.exp3_iterations);
            break;
            
        case '4':
            memoryReadOptimizationExperiment(config.exp4_size_kb, config.exp4_iterations);
            break;
            
        case '5':
            cacheConflictsExperiment(config.exp5_bank_size_kb, 
                                      config.exp5_line_size, 
                                      config.exp5_num_lines);
            break;
            
        case '6':
            sortingAlgorithmsExperiment(config.exp6_elements);
            break;
            
        case 'a':
        case 'A':
            runAllExperiments();
            break;
            
        case 'h':
        case 'H':
        case '?':
            printHelp();
            break;
            
        default:
            // Игнорируем неизвестные команды
            break;
    }
}

// ==================== MAIN ====================

#if defined(PLATFORM_AVR) || defined(PLATFORM_ESP)

// Arduino-стиль main
void setup() {
    Serial.begin(115200);
    while (!Serial) { ; }  // Ждём подключения USB
    
    timer_init();
    delay_ms(100);
    
    printHelp();
}

void loop() {
    char cmd = uart_read_char();
    if (cmd != 0) {
        processCommand(cmd);
    }
    delay_ms(10);
}

#else

// STM32 / Generic ARM стиль
// Предполагается, что HAL уже инициализирован в main() проекта CubeMX

/**
 * @brief Точка входа для STM32
 * Вызывайте эту функцию из main() после HAL_Init() и SystemClock_Config()
 */
void memoryLabMain(void) {
    timer_init();
    delay_ms(100);
    
    printHelp();
    
    while (1) {
        char cmd = uart_read_char();
        if (cmd != 0) {
            processCommand(cmd);
        }
        delay_ms(10);
    }
}

// Для standalone компиляции (не CubeMX)
#ifndef CUBEMX_PROJECT
int main(void) {
    // Здесь должна быть инициализация HAL
    // HAL_Init();
    // SystemClock_Config();
    // MX_USART2_UART_Init();
    
    memoryLabMain();
    return 0;
}
#endif

#endif // Platform selection
