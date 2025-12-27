/**
 * @file sorting_algorithms.cpp
 * @brief Эксперимент 6: Сравнение алгоритмов сортировки
 * 
 * Сравнивает производительность различных алгоритмов сортировки
 * на микроконтроллерах с ограниченной памятью.
 */

#include "common.hpp"

// ==================== АЛГОРИТМЫ СОРТИРОВКИ ====================

/**
 * @brief Пузырьковая сортировка O(n²)
 */
void bubbleSort(uint16_t* arr, uint16_t n) {
    for (uint16_t i = 0; i < n - 1; i++) {
        for (uint16_t j = 0; j < n - i - 1; j++) {
            if (arr[j] > arr[j + 1]) {
                uint16_t temp = arr[j];
                arr[j] = arr[j + 1];
                arr[j + 1] = temp;
            }
        }
    }
}

/**
 * @brief Сортировка вставками O(n²)
 */
void insertionSort(uint16_t* arr, uint16_t n) {
    for (uint16_t i = 1; i < n; i++) {
        uint16_t key = arr[i];
        int16_t j = i - 1;
        while (j >= 0 && arr[j] > key) {
            arr[j + 1] = arr[j];
            j--;
        }
        arr[j + 1] = key;
    }
}

/**
 * @brief Быстрая сортировка O(n log n) - итеративная версия для экономии стека
 */
void quickSort(uint16_t* arr, int16_t low, int16_t high) {
    // Простой стек для итеративной реализации
    #define STACK_SIZE 32
    int16_t stack[STACK_SIZE];
    int16_t top = -1;
    
    stack[++top] = low;
    stack[++top] = high;
    
    while (top >= 0) {
        high = stack[top--];
        low = stack[top--];
        
        // Partitioning
        uint16_t pivot = arr[high];
        int16_t i = low - 1;
        
        for (int16_t j = low; j < high; j++) {
            if (arr[j] <= pivot) {
                i++;
                uint16_t temp = arr[i];
                arr[i] = arr[j];
                arr[j] = temp;
            }
        }
        
        uint16_t temp = arr[i + 1];
        arr[i + 1] = arr[high];
        arr[high] = temp;
        int16_t pi = i + 1;
        
        if (pi - 1 > low && top < STACK_SIZE - 2) {
            stack[++top] = low;
            stack[++top] = pi - 1;
        }
        
        if (pi + 1 < high && top < STACK_SIZE - 2) {
            stack[++top] = pi + 1;
            stack[++top] = high;
        }
    }
    #undef STACK_SIZE
}

/**
 * @brief Сортировка Шелла O(n^1.5)
 */
void shellSort(uint16_t* arr, uint16_t n) {
    for (uint16_t gap = n / 2; gap > 0; gap /= 2) {
        for (uint16_t i = gap; i < n; i++) {
            uint16_t temp = arr[i];
            uint16_t j;
            for (j = i; j >= gap && arr[j - gap] > temp; j -= gap) {
                arr[j] = arr[j - gap];
            }
            arr[j] = temp;
        }
    }
}

// ==================== ЭКСПЕРИМЕНТ ====================

/**
 * @brief Простой LCG для генерации тестовых данных
 */
static uint32_t sort_lcg_state = 54321;

inline uint16_t sort_random(void) {
    sort_lcg_state = sort_lcg_state * 1103515245 + 12345;
    return (uint16_t)(sort_lcg_state >> 16);
}

inline void sort_seed(uint32_t seed) {
    sort_lcg_state = seed;
}

/**
 * @brief Заполняет массив случайными числами
 */
void fillRandom(uint16_t* arr, uint16_t n, uint32_t seed) {
    sort_seed(seed);
    for (uint16_t i = 0; i < n; i++) {
        arr[i] = sort_random();
    }
}

/**
 * @brief Проводит эксперимент сравнения алгоритмов сортировки
 * 
 * @param num_elements Количество элементов для сортировки
 */
void sortingAlgorithmsExperiment(uint16_t num_elements) {
    uart_println("\n[EXP6] Sorting Algorithms");
    uart_println("=========================");
    
    // Ограничение по памяти (нужно 2 копии массива)
    size_t required = num_elements * sizeof(uint16_t) * 2;
    if (required > AVAILABLE_RAM) {
        num_elements = AVAILABLE_RAM / (sizeof(uint16_t) * 2);
    }
    
    // Для МК с малой памятью ограничиваем количество элементов
    #if defined(PLATFORM_AVR)
    if (num_elements > 200) num_elements = 200;
    #endif
    
    uart_print("[EXP6] Elements: ");
    uart_print_uint(num_elements);
    uart_println("");
    
    uint16_t* original = (uint16_t*)mc_malloc(num_elements * sizeof(uint16_t));
    uint16_t* working = (uint16_t*)mc_malloc(num_elements * sizeof(uint16_t));
    
    if (!original || !working) {
        uart_println("[EXP6] Failed to allocate memory");
        if (original) mc_free(original);
        if (working) mc_free(working);
        return;
    }
    
    // Генерируем тестовые данные
    fillRandom(original, num_elements, 12345);
    
    json_start();
    json_key_string("experiment", "sorting_algorithms", true);
    json_key_uint("elements", num_elements, true);
    
    // === Bubble Sort ===
    memcpy(working, original, num_elements * sizeof(uint16_t));
    ticks_t bubbleStart = get_ticks();
    bubbleSort(working, num_elements);
    ticks_t bubbleEnd = get_ticks();
    ticks_t bubbleTicks = bubbleEnd - bubbleStart;
    float bubbleTimeUs = (float)bubbleTicks / (float)CPU_FREQ_MHZ;
    
    // === Insertion Sort ===
    memcpy(working, original, num_elements * sizeof(uint16_t));
    ticks_t insertStart = get_ticks();
    insertionSort(working, num_elements);
    ticks_t insertEnd = get_ticks();
    ticks_t insertTicks = insertEnd - insertStart;
    float insertTimeUs = (float)insertTicks / (float)CPU_FREQ_MHZ;
    
    // === Shell Sort ===
    memcpy(working, original, num_elements * sizeof(uint16_t));
    ticks_t shellStart = get_ticks();
    shellSort(working, num_elements);
    ticks_t shellEnd = get_ticks();
    ticks_t shellTicks = shellEnd - shellStart;
    float shellTimeUs = (float)shellTicks / (float)CPU_FREQ_MHZ;
    
    // === Quick Sort ===
    memcpy(working, original, num_elements * sizeof(uint16_t));
    ticks_t quickStart = get_ticks();
    quickSort(working, 0, num_elements - 1);
    ticks_t quickEnd = get_ticks();
    ticks_t quickTicks = quickEnd - quickStart;
    float quickTimeUs = (float)quickTicks / (float)CPU_FREQ_MHZ;
    
    mc_free(original);
    mc_free(working);
    
    json_key_float("bubble_sort_us", bubbleTimeUs, 2, true);
    json_key_float("insertion_sort_us", insertTimeUs, 2, true);
    json_key_float("shell_sort_us", shellTimeUs, 2, true);
    json_key_float("quick_sort_us", quickTimeUs, 2, false);
    json_end();
    
    uart_println("[EXP6] Results:");
    uart_print("  Bubble:    ");
    uart_print_float(bubbleTimeUs, 2);
    uart_println(" us");
    uart_print("  Insertion: ");
    uart_print_float(insertTimeUs, 2);
    uart_println(" us");
    uart_print("  Shell:     ");
    uart_print_float(shellTimeUs, 2);
    uart_println(" us");
    uart_print("  Quick:     ");
    uart_print_float(quickTimeUs, 2);
    uart_println(" us");
}
