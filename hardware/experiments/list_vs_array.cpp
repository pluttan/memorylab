#include "common.hpp"

// ==================== ЭКСПЕРИМЕНТ: СРАВНЕНИЕ ССЫЛОЧНЫХ И ВЕКТОРНЫХ СТРУКТУР ====================

/**
 * @brief Эксперимент сравнения ссылочных и векторных структур данных
 *
 * Параметры:
 * - param1: 1..20 (М) - Количество элементов в списке (в миллионах, 1 М = 1048576)
 * - param2: 4..500 (КБ) - Максимальная фрагментация списка (расстояние в памяти между соседними элементами)
 * - param3: 1..10 (КБ) - Шаг увеличения фрагментации
 *
 * Как в методичке, фрагментация FF меняется от 1 элемента (список лежит подряд)
 * до param2 с шагом param3. На ось X выводится расстояние между соседними
 * элементами списка в байтах: FF * sizeof(ListNode).
 */
std::string listVsArrayExperiment(const std::string& params) {
    int param1 = SimpleJsonParser::getInt(params, "param1", 1);   // М элементов
    int param2 = SimpleJsonParser::getInt(params, "param2", 100); // КБ макс. фрагментация
    int param3 = SimpleJsonParser::getInt(params, "param3", 10);  // КБ шаг
    
    // Ограничения
    if (param1 < 1) param1 = 1;
    if (param1 > 20) param1 = 20;
    if (param2 < 4) param2 = 4;
    if (param2 > 500) param2 = 500;
    if (param3 < 1) param3 = 1;
    if (param3 > 10) param3 = 10;
    
    // Структура элемента списка
    struct ListNode {
        ListNode* next;
        int val;
    };
    
    size_t numElements = static_cast<size_t>(param1) * 1024 * 1024;
    // Фрагментация в элементах списка: расстояние в байтах / размер элемента
    size_t maxFrag = static_cast<size_t>(param2) * 1024 / sizeof(ListNode);
    size_t fragStep = static_cast<size_t>(param3) * 1024 / sizeof(ListNode);
    if (fragStep < 1) fragStep = 1;
    
    // Ограничиваем количество точек данных (максимум 500)
    size_t estimatedPoints = maxFrag / fragStep + 1;
    if (estimatedPoints > 500) {
        fragStep = (maxFrag + 499) / 500;
    }
    
    // Выделение памяти (большие страницы — чтобы результат не зависел от настроек ОС)
    PageBuffer listBuffer(numElements * sizeof(ListNode), PageMode::Huge);
    PageBuffer arrBuffer(numElements * sizeof(int), PageMode::Huge);
    if (!listBuffer.ok() || !arrBuffer.ok()) {
        return allocationError(numElements * (sizeof(ListNode) + sizeof(int)));
    }
    ListNode* list = listBuffer.as<ListNode>();
    int* arr = arrBuffer.as<int>();
    
    // Отметки занятых элементов при построении списка
    std::vector<uint8_t> used(numElements);
    
    size_t llcSize = getLastLevelCacheSize();
    std::string warning = llcWarning(numElements * sizeof(ListNode), llcSize);
    
    // PMU счётчики
    PerfCounters perfCounters;
    PmuMetrics listPmu, arrayPmu;
    
    struct DataPoint {
        size_t fragmentation;
        double list_time_us;
        double array_time_us;
        PmuMetrics pmu;
    };
    std::vector<DataPoint> results;
    
    // Повторы каждого замера (берём минимум)
    const int NUM_REPEATS = 3;
    
    prepareForMeasurement();
    
    printf("\n[EXP2] ========== linked_vs_array ==========\n");
    printf("[EXP2] Параметры: param1=%d М, param2=%d К, param3=%d К\n", param1, param2, param3);
    printf("[EXP2] numElements=%zu, maxFrag=%zu эл., fragStep=%zu эл., sizeof(ListNode)=%zu Б\n",
           numElements, maxFrag, fragStep, sizeof(ListNode));
    if (!warning.empty()) {
        printf("[EXP2] ВНИМАНИЕ: %s\n", warning.c_str());
    }
    printf("[EXP2] Всего шагов: %zu\n", maxFrag / fragStep + 1);
    fflush(stdout);
    
    // Цикл по различным фрагментациям: FF = 1, 1 + шаг, ... (как в методичке)
    for (size_t frag = 1; frag <= maxFrag + 1; frag += fragStep) {
        if (isCancelled()) {
            return "{\"error\":\"Experiment cancelled\",\"cancelled\":true}";
        }
        
        // Построение списка с заданной фрагментацией: следующий элемент через frag
        // позиций от текущего, при занятой позиции — ближайшая свободная за ней
        std::fill(used.begin(), used.end(), 0);
        size_t prev = 0;
        used[0] = 1;
        for (size_t i = 1; i < numElements; i++) {
            size_t cur = (prev + frag) % numElements;
            while (used[cur]) {
                if (++cur == numElements) cur = 0;
            }
            used[cur] = 1;
            list[prev].next = &list[cur];
            list[prev].val = static_cast<int>(i - 1);
            prev = cur;
        }
        list[prev].next = nullptr;
        list[prev].val = static_cast<int>(numElements - 1);
        
        // Измеряем время работы со списком
        double listTime = std::numeric_limits<double>::max();
        PmuMetrics pointPmu;
        for (int rep = 0; rep < NUM_REPEATS; rep++) {
            perfCounters.start();
            auto start = std::chrono::steady_clock::now();
            volatile int maxList = list[0].val;
            ListNode* node = &list[0];
            while (node->next) {
                node = node->next;
                if (node->val > maxList) maxList = node->val;
            }
            auto end = std::chrono::steady_clock::now();
            perfCounters.stop();
            double time = std::chrono::duration<double, std::micro>(end - start).count();
            if (time < listTime) {
                listTime = time;
                pointPmu = perfCounters.read();
            }
        }
        listPmu += pointPmu;
        
        // Измеряем время работы с массивом (как в методичке — на каждом шаге)
        for (size_t i = 0; i < numElements; i++) {
            arr[i] = static_cast<int>(i);
        }
        double arrayTime = std::numeric_limits<double>::max();
        PmuMetrics pointArrayPmu;
        for (int rep = 0; rep < NUM_REPEATS; rep++) {
            perfCounters.start();
            auto start = std::chrono::steady_clock::now();
            volatile int maxArr = arr[0];
            for (size_t i = 0; i < numElements; i++) {
                if (arr[i] > maxArr) maxArr = arr[i];
            }
            auto end = std::chrono::steady_clock::now();
            perfCounters.stop();
            double time = std::chrono::duration<double, std::micro>(end - start).count();
            if (time < arrayTime) {
                arrayTime = time;
                pointArrayPmu = perfCounters.read();
            }
        }
        arrayPmu += pointArrayPmu;
        
        results.push_back({frag * sizeof(ListNode), listTime, arrayTime, pointPmu});
    }
    
    // Расчёт выводов
    double totalListTime = 0, totalArrayTime = 0;
    for (const auto& r : results) {
        totalListTime += r.list_time_us;
        totalArrayTime += r.array_time_us;
    }
    double listToArrayRatio = (totalArrayTime > 0) ? totalListTime / totalArrayTime : 0;
    
    // Формирование JSON
    std::ostringstream json;
    json << "{";
    json << "\"experiment\":\"list_vs_array\",";
    if (!warning.empty()) {
        json << "\"warning\":\"" << jsonEscape(warning) << "\",";
    }
    json << "\"parameters\":{";
    json << "\"param1_m\":" << param1 << ",";
    json << "\"param2_kb\":" << param2 << ",";
    json << "\"param3_kb\":" << param3 << ",";
    json << "\"num_elements\":" << numElements << ",";
    json << "\"node_size_bytes\":" << sizeof(ListNode) << ",";
    json << "\"llc_bytes\":" << llcSize;
    json << "},";
    json << "\"conclusions\":{";
    json << "\"total_list_time_us\":" << totalListTime << ",";
    json << "\"total_array_time_us\":" << totalArrayTime << ",";
    json << "\"list_to_array_ratio\":" << listToArrayRatio;
    json << "},";
    json << "\"dataPoints\":[";
    for (size_t i = 0; i < results.size(); i++) {
        if (i > 0) json << ",";
        json << "{\"fragmentation\":" << results[i].fragmentation
             << ",\"list_time_us\":" << results[i].list_time_us
             << ",\"array_time_us\":" << results[i].array_time_us
             << ",\"cache_misses\":" << results[i].pmu.json(PmuMetrics::CACHE_MISSES)
             << ",\"branch_misses\":" << results[i].pmu.json(PmuMetrics::BRANCH_MISSES) << "}";
    }
    json << "],";
    // Итоговые PMU метрики
    json << "\"pmu_summary\":{";
    json << "\"list\":" << listPmu.toJson() << ",";
    json << "\"array\":" << arrayPmu.toJson();
    json << "}}";
    
    return json.str();
}
