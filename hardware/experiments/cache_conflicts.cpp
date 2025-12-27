#include "common.hpp"

// ==================== ЭКСПЕРИМЕНТ: КОНФЛИКТЫ В КЭШ-ПАМЯТИ ====================
// 
// Суть эксперимента:
// - Чтение с шагом = размер банка вызывает конфликты (все данные попадают в один набор)
// - Чтение с шагом = размер банка + линейка избегает конфликтов
// По графику должны получиться две горизонтальные линии:
// - Красная (с конфликтами) выше
// - Зелёная (без конфликтов) ниже

/**
 * @brief Эксперимент исследования конфликтов в кэш-памяти
 * 
 * Параметры:
 * - param1: 0 (автоопределение) или 1..256 (КБ) - Размер банка кэш-памяти
 * - param2: 0 (автоопределение) или 1..128 (Б) - Размер линейки кэш-памяти
 * - param3: 2..512 - Количество читаемых линеек
 */
std::string cacheConflictsExperiment(const std::string& params) {
    int param1 = SimpleJsonParser::getInt(params, "param1", 0);  // КБ размер банка (0 = автоопределение)
    int param2 = SimpleJsonParser::getInt(params, "param2", 0);  // Б линейка (0 = автоопределение)
    int param3 = SimpleJsonParser::getInt(params, "param3", 64);  // количество линеек
    
    // Автоопределение размера банка кэша (L1 cache size / 1024 для получения в КБ)
    if (param1 <= 0) {
        param1 = static_cast<int>(getL1CacheSize() / 1024);
    }
    
    // Автоопределение размера линейки кэша
    if (param2 <= 0) {
        param2 = static_cast<int>(getCacheLineSize());
    }
    
    if (param1 < 1) param1 = 1;
    if (param1 > 256) param1 = 256;
    if (param2 < 1) param2 = 1;
    if (param2 > 128) param2 = 128;
    if (param3 < 2) param3 = 2;
    if (param3 > 512) param3 = 512;
    
    size_t bankSize = static_cast<size_t>(param1) * 1024;
    size_t lineSize = static_cast<size_t>(param2);
    int maxLines = param3;
    
    // Выделяем достаточно памяти для обоих случаев
    // Для доступа без конфликтов: (bankSize + lineSize) * maxLines
    size_t totalSize = (bankSize + lineSize) * maxLines + bankSize;
    int* p = static_cast<int*>(malloc64(totalSize));
    if (!p) {
        return "{\"error\":\"Failed to allocate memory\"}";
    }
    memset(p, 0, totalSize);
    
    // PMU счётчики
    PerfCounters perfCounters;
    PmuMetrics conflictPmu, noConflictPmu;
    
    struct DataPoint {
        int line_index;
        size_t offset_conflict;      // Смещение для чтения с конфликтами
        size_t offset_no_conflict;   // Смещение для чтения без конфликтов
        double conflict_time_us;     // Время доступа с конфликтами
        double no_conflict_time_us;  // Время доступа без конфликтов
    };
    std::vector<DataPoint> results;
    
    setCancelExperiment(false);
    prepareForMeasurement();
    
    printf("\n[EXP5] ========== cache_conflicts ==========\n");
    printf("[EXP5] Параметры: param1=%d КБ (банк), param2=%d Б (линейка), param3=%d линеек\n", param1, param2, param3);
    printf("[EXP5] bankSize=%zu байт, lineSize=%zu байт, totalSize=%zu байт\n", bankSize, lineSize, totalSize);
    fflush(stdout);
    
    const int NUM_ITERATIONS = 1000;  // Много повторений для точного измерения одиночного доступа
    
    // Прогрев кэша - заполняем данные
    for (int a = 0; a < maxLines; a++) {
        volatile int x = 0;
        // Прогрев с конфликтами
        x += p[(a * bankSize) / sizeof(int)];
        // Прогрев без конфликтов
        x += p[(a * (bankSize + lineSize)) / sizeof(int)];
    }
    
    // ===== ЧТЕНИЕ С КОНФЛИКТАМИ =====
    // Шаг = размер банка, все данные попадают в один набор кэша
    printf("[EXP5] Измерение чтения С КОНФЛИКТАМИ (шаг = bankSize = %zu)...\n", bankSize);
    fflush(stdout);
    
    std::vector<double> conflictTimes(maxLines);
    
    if (perfCounters.isAvailable()) {
        perfCounters.start();
    }
    
    for (int a = 0; a < maxLines; a++) {
        if (isCancelled()) {
            free(p);
            return "{\"error\":\"Experiment cancelled\",\"cancelled\":true}";
        }
        
        size_t offset = a * bankSize;
        volatile int x = 0;
        
        auto start = std::chrono::high_resolution_clock::now();
        for (int iter = 0; iter < NUM_ITERATIONS; iter++) {
            x += *(int*)((char*)p + offset);
        }
        auto end = std::chrono::high_resolution_clock::now();
        
        conflictTimes[a] = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count() / 1000.0 / NUM_ITERATIONS;
    }
    
    if (perfCounters.isAvailable()) {
        perfCounters.stop();
        conflictPmu = perfCounters.read();
    }
    
    // ===== ЧТЕНИЕ БЕЗ КОНФЛИКТОВ =====
    // Шаг = размер банка + линейка, данные распределяются по разным наборам
    printf("[EXP5] Измерение чтения БЕЗ КОНФЛИКТОВ (шаг = bankSize + lineSize = %zu)...\n", bankSize + lineSize);
    fflush(stdout);
    
    std::vector<double> noConflictTimes(maxLines);
    
    if (perfCounters.isAvailable()) {
        perfCounters.start();
    }
    
    for (int a = 0; a < maxLines; a++) {
        if (isCancelled()) {
            free(p);
            return "{\"error\":\"Experiment cancelled\",\"cancelled\":true}";
        }
        
        size_t offset = a * (bankSize + lineSize);
        volatile int x = 0;
        
        auto start = std::chrono::high_resolution_clock::now();
        for (int iter = 0; iter < NUM_ITERATIONS; iter++) {
            x += *(int*)((char*)p + offset);
        }
        auto end = std::chrono::high_resolution_clock::now();
        
        noConflictTimes[a] = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count() / 1000.0 / NUM_ITERATIONS;
    }
    
    if (perfCounters.isAvailable()) {
        perfCounters.stop();
        noConflictPmu = perfCounters.read();
    }
    
    // Собираем результаты
    for (int a = 0; a < maxLines; a++) {
        results.push_back({
            a,
            a * bankSize,
            a * (bankSize + lineSize),
            conflictTimes[a],
            noConflictTimes[a]
        });
    }
    
    free(p);
    
    // Расчёт выводов
    double totalConflictTime = 0, totalNoConflictTime = 0;
    for (const auto& r : results) {
        totalConflictTime += r.conflict_time_us;
        totalNoConflictTime += r.no_conflict_time_us;
    }
    double avgConflictTime = totalConflictTime / results.size();
    double avgNoConflictTime = totalNoConflictTime / results.size();
    double conflictToNoConflictRatio = (avgNoConflictTime > 0) ? avgConflictTime / avgNoConflictTime : 0;
    
    printf("[EXP5] Среднее время с конфликтами: %.4f мкс\n", avgConflictTime);
    printf("[EXP5] Среднее время без конфликтов: %.4f мкс\n", avgNoConflictTime);
    printf("[EXP5] Отношение (с конфликтами / без): %.2fx\n", conflictToNoConflictRatio);
    fflush(stdout);
    
    // Формирование JSON
    std::ostringstream json;
    json << "{";
    json << "\"experiment\":\"cache_conflicts\",";
    json << "\"parameters\":{";
    json << "\"param1_kb\":" << param1 << ",";
    json << "\"param2_b\":" << param2 << ",";
    json << "\"param3_lines\":" << param3;
    json << "},";
    json << "\"conclusions\":{";
    json << "\"avg_conflict_time_us\":" << avgConflictTime << ",";
    json << "\"avg_no_conflict_time_us\":" << avgNoConflictTime << ",";
    json << "\"conflict_to_no_conflict_ratio\":" << conflictToNoConflictRatio;
    json << "},";
    json << "\"dataPoints\":[";
    for (size_t i = 0; i < results.size(); i++) {
        if (i > 0) json << ",";
        json << "{\"line\":" << results[i].line_index 
             << ",\"offset_conflict\":" << results[i].offset_conflict
             << ",\"offset_no_conflict\":" << results[i].offset_no_conflict
             << ",\"conflict_time_us\":" << results[i].conflict_time_us
             << ",\"no_conflict_time_us\":" << results[i].no_conflict_time_us << "}";
    }
    json << "],";
    // Итоговые PMU метрики
    json << "\"pmu_summary\":{";
    json << "\"conflict\":" << conflictPmu.toJson() << ",";
    json << "\"no_conflict\":" << noConflictPmu.toJson();
    json << "}}";
    
    return json.str();
}

