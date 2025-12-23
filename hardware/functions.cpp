#ifndef FUNCTIONS_HPP
#define FUNCTIONS_HPP

#include "experiments/common.hpp"
#include "experiments/memory_stratification.cpp"
#include "experiments/list_vs_array.cpp"
#include "experiments/prefetch.cpp"
#include "experiments/memory_read_optimization.cpp"
#include "experiments/cache_conflicts.cpp"
#include "experiments/sorting_algorithms.cpp"

/**
 * @brief Инициализация функций
 * Вызывается при старте сервера для регистрации всех доступных функций
 */
void initializeFunctions() {
    // Регистрация эксперимента исследования расслоения памяти
    functionRegistry.registerFunction(
        "memory_stratification",
        "Исследование расслоения динамической памяти. Параметры: param1 (1-128 КБ), param2 (4-64 Б), param3 (1-16 МБ)",
        memoryStratificationExperiment
    );
    
    // Сравнение ссылочных и векторных структур
    functionRegistry.registerFunction(
        "list_vs_array",
        "Сравнение эффективности ссылочных и векторных структур. Параметры: param1 (1-20 М элементов), param2 (4-500 КБ фрагментация), param3 (1-10 КБ шаг)",
        listVsArrayExperiment
    );
    
    // Исследование предвыборки
    functionRegistry.registerFunction(
        "prefetch",
        "Исследование эффективности программной предвыборки. Параметры: param1 (1-4096 Б шаг), param2 (4-8192 КБ размер)",
        prefetchExperiment
    );
    
    // Оптимизация чтения памяти
    functionRegistry.registerFunction(
        "memory_read_optimization",
        "Исследование оптимизации чтения оперативной памяти. Параметры: param1 (1-4 МБ), param2 (1-128 потоков)",
        memoryReadOptimizationExperiment
    );
    
    // Конфликты в кэш-памяти
    functionRegistry.registerFunction(
        "cache_conflicts",
        "Исследование конфликтов в кэш-памяти. Параметры: param1 (0=авто или 1-256 КБ банк), param2 (0=авто или 1-128 Б линейка), param3 (2-512 линеек)",
        cacheConflictsExperiment
    );
    
    // Алгоритмы сортировки
    functionRegistry.registerFunction(
        "sorting_algorithms",
        "Сравнение алгоритмов сортировки. Параметры: param1 (1-20 М элементов), param2 (4-1024 К шаг)",
        sortingAlgorithmsExperiment
    );
}

#endif // FUNCTIONS_HPP
