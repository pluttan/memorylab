#include "common.hpp"

// ==================== ЭКСПЕРИМЕНТ: ИССЛЕДОВАНИЕ ПРЕДВЫБОРКИ ====================

/**
 * @brief Эксперимент исследования эффективности программной предвыборки
 * 
 * Параметры:
 * - param1: 1..4096 (Б) - Шаг увеличения расстояния между читаемыми данными
 * - param2: 4..8192 (КБ) - Размер массива
 */
std::string prefetchExperiment(const std::string& params) {
    int param1 = SimpleJsonParser::getInt(params, "param1", 64); // Б шаг
    int param2 = SimpleJsonParser::getInt(params, "param2", 64); // КБ размер
    
    if (param1 < 1) param1 = 1;
    if (param1 > 4096) param1 = 4096;
    if (param2 < 4) param2 = 4;
    if (param2 > 8192) param2 = 8192;
    
    size_t stepSize = static_cast<size_t>(param1);
    size_t arraySize = static_cast<size_t>(param2) * 1024;
    
    // Ограничиваем количество точек данных (максимум 2000 для детального графика)
    // Фиксируем шаг 64 байта чтобы видеть каждую кэш-линейку
    stepSize = 64; 
    size_t maxPoints = 2000;
    
    // Два массива для тестирования
    int* p1 = static_cast<int*>(malloc64(arraySize));
    int* p2 = static_cast<int*>(malloc64(arraySize));
    if (!p1 || !p2) {
        if (p1) free(p1);
        if (p2) free(p2);
        return "{\"error\":\"Failed to allocate memory\"}";
    }
    
    // Инициализация (записываем чтобы выделить физические страницы)
    memset(p1, 1, arraySize);
    memset(p2, 2, arraySize);
    
    std::vector<std::tuple<size_t, double, double>> results;
    setCancelExperiment(false);
    prepareForMeasurement();
    
    printf("\n[EXP3] ========== prefetch ==========\n");
    printf("[EXP3] Параметры: param1=%d Б, param2=%d КБ\n", param1, param2);
    printf("[EXP3] stepSize=%zu байт, arraySize=%zu байт\n", stepSize, arraySize);
    printf("[EXP3] Ограничение точек: %zu\n", maxPoints);
    fflush(stdout);
    
    // === Измерение БЕЗ ПРЕДВЫБОРКИ ===
    // Сбрасываем кэш один раз перед началом
    volatile int sink = 0;
    for (size_t i = 0; i < arraySize; i += 64) {
        // Читаем p2 чтобы вытеснить p1 из кэша
        sink += p2[i / sizeof(int)];
    }
    
    // Последовательный доступ к p1 - пики появятся при промахах кэша/TLB
    std::vector<double> noPrefetchTimes;
    size_t currentPoint = 0;
    for (size_t a = 0; a < arraySize && currentPoint < maxPoints; a += stepSize, currentPoint++) {
        if (isCancelled()) {
            free(p1); free(p2);
            return "{\"error\":\"Experiment cancelled\",\"cancelled\":true}";
        }
        auto start = std::chrono::high_resolution_clock::now();
        sink += p1[a / sizeof(int)];
        auto end = std::chrono::high_resolution_clock::now();
        noPrefetchTimes.push_back(std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count());
    }
    
    // === Измерение С ПРЕДВЫБОРКОЙ ===
    // Снова сбрасываем кэш
    for (size_t i = 0; i < arraySize; i += 64) {
        sink += p2[i / sizeof(int)];
    }
    
    // Доступ с предвыборкой следующего блока
    std::vector<double> prefetchTimes;
    currentPoint = 0;
    for (size_t a = 0; a < arraySize && currentPoint < maxPoints; a += stepSize, currentPoint++) {
        // Предвыбираем следующий блок заранее
        if (a + stepSize < arraySize) {
            __builtin_prefetch(&p1[(a + stepSize) / sizeof(int)], 0, 3);
        }
        
        auto start = std::chrono::high_resolution_clock::now();
        sink += p1[a / sizeof(int)];
        auto end = std::chrono::high_resolution_clock::now();
        prefetchTimes.push_back(std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count());
    }
    
    free(p1);
    free(p2);
    
    printf("[EXP3] Завершено, получено %zu точек данных\n", noPrefetchTimes.size());
    fflush(stdout);
    
    // Формируем результаты
    for (size_t i = 0; i < noPrefetchTimes.size(); i++) {
        results.push_back({i * stepSize, noPrefetchTimes[i], prefetchTimes[i]});
    }
    
    // Расчёт выводов
    double totalNoPrefetch = 0, totalPrefetch = 0;
    for (const auto& r : results) {
        totalNoPrefetch += std::get<1>(r);
        totalPrefetch += std::get<2>(r);
    }
    double noPrefetchToPrefetchRatio = (totalPrefetch > 0) ? totalNoPrefetch / totalPrefetch : 0;
    
    // Формирование JSON
    std::ostringstream json;
    json << "{";
    json << "\"experiment\":\"prefetch\",";
    json << "\"parameters\":{";
    json << "\"param1_b\":" << param1 << ",";
    json << "\"param2_kb\":" << param2;
    json << "},";
    json << "\"conclusions\":{";
    json << "\"total_no_prefetch_ns\":" << totalNoPrefetch << ",";
    json << "\"total_prefetch_ns\":" << totalPrefetch << ",";
    json << "\"no_prefetch_to_prefetch_ratio\":" << noPrefetchToPrefetchRatio;
    json << "},";
    json << "\"dataPoints\":[";
    for (size_t i = 0; i < results.size(); i++) {
        if (i > 0) json << ",";
        json << "{\"offset\":" << std::get<0>(results[i]) 
             << ",\"no_prefetch_ns\":" << std::get<1>(results[i])
             << ",\"prefetch_ns\":" << std::get<2>(results[i]) << "}";
    }
    json << "]}";
    
    return json.str();
}
