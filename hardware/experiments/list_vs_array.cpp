#include "common.hpp"

// ==================== ЭКСПЕРИМЕНТ: СРАВНЕНИЕ ССЫЛОЧНЫХ И ВЕКТОРНЫХ СТРУКТУР ====================

/**
 * @brief Эксперимент сравнения ссылочных и векторных структур данных
 * 
 * Параметры:
 * - param1: 1..20 (МБ) - Количество элементов (в миллионах)
 * - param2: 4..500 (КБ) - Максимальная фрагментация списка
 * - param3: 1..10 (КБ) - Шаг увеличения фрагментации
 */
std::string listVsArrayExperiment(const std::string& params) {
    int param1 = SimpleJsonParser::getInt(params, "param1", 1);   // МБ элементов
    int param2 = SimpleJsonParser::getInt(params, "param2", 100); // КБ макс. фрагментация
    int param3 = SimpleJsonParser::getInt(params, "param3", 10);  // КБ шаг
    
    // Ограничения
    if (param1 < 1) param1 = 1;
    if (param1 > 20) param1 = 20;
    if (param2 < 4) param2 = 4;
    if (param2 > 500) param2 = 500;
    if (param3 < 1) param3 = 1;
    if (param3 > 10) param3 = 10;
    
    size_t numElements = static_cast<size_t>(param1) * 1024 * 1024 / sizeof(int);
    size_t maxFrag = static_cast<size_t>(param2) * 1024 / sizeof(int);
    size_t fragStep = static_cast<size_t>(param3) * 1024 / sizeof(int);
    
    // Ограничиваем количество точек данных (максимум 500)
    size_t estimatedPoints = maxFrag / fragStep;
    if (estimatedPoints > 500 && fragStep > 0) {
        fragStep = maxFrag / 500;
        if (fragStep < 1) fragStep = 1;
    }
    
    // Структура элемента списка
    struct ListNode {
        ListNode* next;
        int val;
    };
    
    // Выделение памяти
    ListNode* list = static_cast<ListNode*>(malloc64(numElements * sizeof(ListNode)));
    int* arr = static_cast<int*>(malloc64(numElements * sizeof(int)));
    
    if (!list || !arr) {
        if (list) free(list);
        if (arr) free(arr);
        return "{\"error\":\"Failed to allocate memory\"}";
    }
    
    // Инициализация массива
    for (size_t i = 0; i < numElements; i++) {
        arr[i] = static_cast<int>(i);
    }
    
    std::vector<std::tuple<size_t, double, double>> results;
    
    setCancelExperiment(false);
    prepareForMeasurement();
    
    printf("\n[EXP2] ========== linked_vs_array ==========\n");
    printf("[EXP2] Параметры: param1=%d М, param2=%d К, param3=%d К\n", param1, param2, param3);
    printf("[EXP2] numElements=%zu, maxFrag=%zu, fragStep=%zu\n", numElements, maxFrag, fragStep);
    printf("[EXP2] Всего шагов: %zu\n", maxFrag / fragStep);
    fflush(stdout);
    
    // Измеряем время работы с массивом (константное)
    double arrayTime = 0;
    {
        auto start = std::chrono::high_resolution_clock::now();
        volatile int maxArr = arr[0];
        for (size_t i = 0; i < numElements; i++) {
            if (arr[i] > maxArr) maxArr = arr[i];
        }
        auto end = std::chrono::high_resolution_clock::now();
        arrayTime = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count() / 1000.0;
    }
    
    // Цикл по различным фрагментациям
    for (size_t frag = fragStep; frag <= maxFrag; frag += fragStep) {
        if (isCancelled()) {
            free(list);
            free(arr);
            return "{\"error\":\"Experiment cancelled\",\"cancelled\":true}";
        }
        
        // Инициализация списка с заданной фрагментацией
        for (size_t i = 0; i < numElements; i++) {
            list[i].next = nullptr;
            list[i].val = 0;
        }
        
        size_t prev = 0;
        for (size_t i = 0; i < numElements; i++) {
            size_t cur = (prev + frag) % numElements;
            while (list[cur].next != nullptr) {
                cur = (cur + 1) % numElements;
            }
            list[prev].next = &list[cur];
            list[prev].val = static_cast<int>(i);
            prev = cur;
        }
        list[prev].next = nullptr;
        list[prev].val = static_cast<int>(numElements);
        
        // Измеряем время работы со списком
        auto start = std::chrono::high_resolution_clock::now();
        volatile int maxList = list[0].val;
        ListNode* node = &list[0];
        while (node->next) {
            node = node->next;
            if (node->val > maxList) maxList = node->val;
        }
        auto end = std::chrono::high_resolution_clock::now();
        double listTime = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count() / 1000.0;
        
        results.push_back({frag * sizeof(int), listTime, arrayTime});
    }
    
    free(list);
    free(arr);
    
    // Расчёт выводов
    double totalListTime = 0, totalArrayTime = 0;
    for (const auto& r : results) {
        totalListTime += std::get<1>(r);
        totalArrayTime += std::get<2>(r);
    }
    double listToArrayRatio = (totalArrayTime > 0) ? totalListTime / totalArrayTime : 0;
    
    // Формирование JSON
    std::ostringstream json;
    json << "{";
    json << "\"experiment\":\"list_vs_array\",";
    json << "\"parameters\":{";
    json << "\"param1_m\":" << param1 << ",";
    json << "\"param2_kb\":" << param2 << ",";
    json << "\"param3_kb\":" << param3;
    json << "},";
    json << "\"conclusions\":{";
    json << "\"total_list_time_us\":" << totalListTime << ",";
    json << "\"total_array_time_us\":" << totalArrayTime << ",";
    json << "\"list_to_array_ratio\":" << listToArrayRatio;
    json << "},";
    json << "\"dataPoints\":[";
    for (size_t i = 0; i < results.size(); i++) {
        if (i > 0) json << ",";
        json << "{\"fragmentation\":" << std::get<0>(results[i]) 
             << ",\"list_time_us\":" << std::get<1>(results[i])
             << ",\"array_time_us\":" << std::get<2>(results[i]) << "}";
    }
    json << "]}";
    
    return json.str();
}
