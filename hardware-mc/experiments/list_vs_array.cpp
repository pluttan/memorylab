/**
 * @file list_vs_array.cpp
 * @brief Эксперимент 2: Сравнение ссылочных и векторных структур
 * 
 * Сравнивает время обхода связного списка и массива
 * для демонстрации влияния локальности данных.
 */

#include "common.hpp"

/**
 * @brief Узел связного списка
 */
struct ListNode {
    uint32_t data;
    ListNode* next;
};

/**
 * @brief Проводит эксперимент сравнения списка и массива
 * 
 * @param num_elements Количество элементов
 * @param iterations Количество итераций
 */
void listVsArrayExperiment(uint16_t num_elements, uint16_t iterations) {
    uart_println("\n[EXP2] List vs Array");
    uart_println("====================");
    
    // Ограничение по памяти
    size_t required = num_elements * (sizeof(ListNode) + sizeof(uint32_t));
    if (required > AVAILABLE_RAM) {
        num_elements = AVAILABLE_RAM / (sizeof(ListNode) + sizeof(uint32_t));
        uart_print("[EXP2] Limited to ");
        uart_print_uint(num_elements);
        uart_println(" elements");
    }
    
    if (iterations < 1) iterations = 100;
    
    // Выделяем массив
    uint32_t* array = (uint32_t*)mc_malloc(num_elements * sizeof(uint32_t));
    if (!array) {
        uart_println("[EXP2] Failed to allocate array");
        return;
    }
    
    // Выделяем список
    ListNode* listHead = NULL;
    ListNode* nodes = (ListNode*)mc_malloc(num_elements * sizeof(ListNode));
    if (!nodes) {
        mc_free(array);
        uart_println("[EXP2] Failed to allocate list");
        return;
    }
    
    // Инициализация массива
    for (uint16_t i = 0; i < num_elements; i++) {
        array[i] = i;
    }
    
    // Инициализация списка (последовательный порядок для начала)
    listHead = &nodes[0];
    for (uint16_t i = 0; i < num_elements - 1; i++) {
        nodes[i].data = i;
        nodes[i].next = &nodes[i + 1];
    }
    nodes[num_elements - 1].data = num_elements - 1;
    nodes[num_elements - 1].next = NULL;
    
    // Прогрев
    volatile uint32_t sum = 0;
    for (uint16_t i = 0; i < num_elements; i++) {
        sum += array[i];
    }
    ListNode* node = listHead;
    while (node) {
        sum += node->data;
        node = node->next;
    }
    
    // Измерение массива
    ticks_t array_start = get_ticks();
    for (uint16_t iter = 0; iter < iterations; iter++) {
        sum = 0;
        for (uint16_t i = 0; i < num_elements; i++) {
            sum += array[i];
        }
    }
    ticks_t array_end = get_ticks();
    ticks_t array_ticks = array_end - array_start;
    
    // Измерение списка
    ticks_t list_start = get_ticks();
    for (uint16_t iter = 0; iter < iterations; iter++) {
        sum = 0;
        node = listHead;
        while (node) {
            sum += node->data;
            node = node->next;
        }
    }
    ticks_t list_end = get_ticks();
    ticks_t list_ticks = list_end - list_start;
    
    mc_free(array);
    mc_free(nodes);
    
    // Вывод результатов
    float array_time_us = (float)array_ticks / (float)CPU_FREQ_MHZ;
    float list_time_us = (float)list_ticks / (float)CPU_FREQ_MHZ;
    float ratio = list_time_us / array_time_us;
    
    json_start();
    json_key_string("experiment", "list_vs_array", true);
    json_key_uint("elements", num_elements, true);
    json_key_uint("iterations", iterations, true);
    json_key_float("array_time_us", array_time_us, 2, true);
    json_key_float("list_time_us", list_time_us, 2, true);
    json_key_float("list_to_array_ratio", ratio, 3, false);
    json_end();
    
    uart_print("[EXP2] List is ");
    uart_print_float(ratio, 2);
    uart_println("x slower than array");
}
