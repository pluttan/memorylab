#include "common.hpp"

// ==================== ЭКСПЕРИМЕНТ: КОНФЛИКТЫ В КЭШ-ПАМЯТИ ====================

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
    int param3 = SimpleJsonParser::getInt(params, "param3", 64);  // линеек
    
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
    size_t totalSize = (bankSize + lineSize) * maxLines * 2;
    int* p = static_cast<int*>(malloc64(totalSize));
    if (!p) {
        return "{\"error\":\"Failed to allocate memory\"}";
    }
    memset(p, 0, totalSize);
    
    std::vector<std::tuple<int, double, double>> results;
    setCancelExperiment(false);
    prepareForMeasurement();
    
    printf("\n[EXP5] ========== cache_conflicts ==========\n");
    printf("[EXP5] Параметры: param1=%d КБ, param2=%d Б, param3=%d линеек\n", param1, param2, param3);
    printf("[EXP5] bankSize=%zu байт, lineSize=%zu байт, totalSize=%zu байт\n", bankSize, lineSize, totalSize);
    fflush(stdout);
    
    const int NUM_ITERATIONS = 100;  // Повторений для точного измерения
    
    // Тестируем для разного количества линеек
    for (int numLines = 2; numLines <= maxLines; numLines += 2) {
        if (isCancelled()) {
            free(p);
            return "{\"error\":\"Experiment cancelled\",\"cancelled\":true}";
        }
        
        // ===== С КОНФЛИКТАМИ (шаг = размер банка) =====
        // Все линейки попадают в один и тот же набор кэша -> вытесняют друг друга
        double conflictTime = 0;
        for (int iter = 0; iter < NUM_ITERATIONS; iter++) {
            auto start = std::chrono::high_resolution_clock::now();
            volatile int sum = 0;
            for (int line = 0; line < numLines; line++) {
                size_t offset = line * bankSize;
                sum += p[offset / sizeof(int)];
            }
            auto end = std::chrono::high_resolution_clock::now();
            conflictTime += std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count() / 1000.0;
        }
        conflictTime /= NUM_ITERATIONS;
        
        // ===== БЕЗ КОНФЛИКТОВ (шаг = размер банка + линейка) =====
        // Линейки попадают в разные наборы кэша -> не вытесняют друг друга
        double noConflictTime = 0;
        for (int iter = 0; iter < NUM_ITERATIONS; iter++) {
            auto start = std::chrono::high_resolution_clock::now();
            volatile int sum = 0;
            for (int line = 0; line < numLines; line++) {
                size_t offset = line * (bankSize + lineSize);
                sum += p[offset / sizeof(int)];
            }
            auto end = std::chrono::high_resolution_clock::now();
            noConflictTime += std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count() / 1000.0;
        }
        noConflictTime /= NUM_ITERATIONS;
        
        results.push_back({numLines, conflictTime, noConflictTime});
    }
    
    free(p);
    
    // Расчёт выводов
    double totalConflictTime = 0, totalNoConflictTime = 0;
    for (size_t i = 0; i < results.size(); i++) {
        totalConflictTime += std::get<1>(results[i]);
        totalNoConflictTime += std::get<2>(results[i]);
    }
    double conflictToNoConflictRatio = (totalNoConflictTime > 0) ? totalConflictTime / totalNoConflictTime : 0;
    
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
    json << "\"total_conflict_time_us\":" << totalConflictTime << ",";
    json << "\"total_no_conflict_time_us\":" << totalNoConflictTime << ",";
    json << "\"conflict_to_no_conflict_ratio\":" << conflictToNoConflictRatio;
    json << "},";
    json << "\"dataPoints\":[";
    for (size_t i = 0; i < results.size(); i++) {
        if (i > 0) json << ",";
        json << "{\"line\":" << std::get<0>(results[i]) 
             << ",\"conflict_time_us\":" << std::get<1>(results[i])
             << ",\"no_conflict_time_us\":" << std::get<2>(results[i]) << "}";
    }
    json << "]}";
    
    return json.str();
}
