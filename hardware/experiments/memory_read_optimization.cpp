#include "common.hpp"

// ==================== ЭКСПЕРИМЕНТ: ОПТИМИЗАЦИЯ ЧТЕНИЯ ПАМЯТИ ====================

/**
 * @brief Эксперимент оптимизации чтения оперативной памяти
 * 
 * Параметры:
 * - param1: 1..4 (МБ) - Размер массива
 * - param2: 1..128 - Количество потоков данных
 */
std::string memoryReadOptimizationExperiment(const std::string& params) {
    int param1 = SimpleJsonParser::getInt(params, "param1", 1);  // МБ
    int param2 = SimpleJsonParser::getInt(params, "param2", 32); // потоков
    
    if (param1 < 1) param1 = 1;
    if (param1 > 4) param1 = 4;
    if (param2 < 1) param2 = 1;
    if (param2 > 128) param2 = 128;
    
    size_t arraySize = static_cast<size_t>(param1) * 1024 * 1024;
    int maxStreams = param2;
    
    // Выделяем отдельные массивы
    std::vector<int*> separateArrays(maxStreams);
    for (int i = 0; i < maxStreams; i++) {
        separateArrays[i] = static_cast<int*>(malloc64(arraySize));
        if (!separateArrays[i]) {
            for (int j = 0; j < i; j++) free(separateArrays[j]);
            return "{\"error\":\"Failed to allocate memory\"}";
        }
        memset(separateArrays[i], 0, arraySize);
    }
    
    // Оптимизированный массив (чередующиеся данные)
    int* optimizedArray = static_cast<int*>(malloc64(arraySize * maxStreams));
    if (!optimizedArray) {
        for (int i = 0; i < maxStreams; i++) free(separateArrays[i]);
        return "{\"error\":\"Failed to allocate memory\"}";
    }
    memset(optimizedArray, 0, arraySize * maxStreams);
    
    // PMU счётчики
    PerfCounters perfCounters;
    PmuMetrics separatePmu, optimizedPmu;
    
    struct DataPoint {
        int streams;
        double separate_time_us;
        double optimized_time_us;
    };
    std::vector<DataPoint> results;
    
    setCancelExperiment(false);
    prepareForMeasurement();
    
    printf("\n[EXP4] ========== memory_read_optimization ==========\n");
    printf("[EXP4] Параметры: param1=%d МБ, param2=%d потоков\n", param1, param2);
    printf("[EXP4] arraySize=%zu байт, maxStreams=%d\n", arraySize, maxStreams);
    fflush(stdout);
    
    // Тестируем для разного количества потоков
    for (int streams = 1; streams <= maxStreams; streams++) {
        if (isCancelled()) {
            for (int i = 0; i < maxStreams; i++) free(separateArrays[i]);
            free(optimizedArray);
            return "{\"error\":\"Experiment cancelled\",\"cancelled\":true}";
        }
        
        // Несколько отдельных массивов
        if (perfCounters.isAvailable()) {
            perfCounters.start();
        }
        
        auto start1 = std::chrono::high_resolution_clock::now();
        volatile int x1 = 0;
        for (size_t a = 0; a < arraySize; a += sizeof(int)) {
            for (int s = 0; s < streams; s++) {
                x1 += separateArrays[s][a / sizeof(int)];
            }
        }
        auto end1 = std::chrono::high_resolution_clock::now();
        double separateTime = std::chrono::duration_cast<std::chrono::nanoseconds>(end1 - start1).count() / 1000.0;
        
        if (perfCounters.isAvailable()) {
            perfCounters.stop();
            separatePmu += perfCounters.read();
        }
        
        // Один оптимизированный массив
        if (perfCounters.isAvailable()) {
            perfCounters.start();
        }
        
        auto start2 = std::chrono::high_resolution_clock::now();
        volatile int x2 = 0;
        for (size_t a = 0; a < arraySize * streams; a += sizeof(int) * streams) {
            for (int s = 0; s < streams; s++) {
                x2 += optimizedArray[(a / sizeof(int)) + s];
            }
        }
        auto end2 = std::chrono::high_resolution_clock::now();
        double optimizedTime = std::chrono::duration_cast<std::chrono::nanoseconds>(end2 - start2).count() / 1000.0;
        
        if (perfCounters.isAvailable()) {
            perfCounters.stop();
            optimizedPmu += perfCounters.read();
        }
        
        results.push_back({streams, separateTime, optimizedTime});
    }
    
    for (int i = 0; i < maxStreams; i++) free(separateArrays[i]);
    free(optimizedArray);
    
    // Расчёт выводов
    double totalSeparateTime = 0, totalOptimizedTime = 0;
    for (size_t i = 0; i < results.size(); i++) {
        totalSeparateTime += results[i].separate_time_us;
        totalOptimizedTime += results[i].optimized_time_us;
    }
    double separateToOptimizedRatio = (totalOptimizedTime > 0) ? totalSeparateTime / totalOptimizedTime : 0;
    
    // Формирование JSON
    std::ostringstream json;
    json << "{";
    json << "\"experiment\":\"memory_read_optimization\",";
    json << "\"parameters\":{";
    json << "\"param1_mb\":" << param1 << ",";
    json << "\"param2_streams\":" << param2;
    json << "},";
    json << "\"conclusions\":{";
    json << "\"total_separate_time_us\":" << totalSeparateTime << ",";
    json << "\"total_optimized_time_us\":" << totalOptimizedTime << ",";
    json << "\"separate_to_optimized_ratio\":" << separateToOptimizedRatio;
    json << "},";
    json << "\"dataPoints\":[";
    for (size_t i = 0; i < results.size(); i++) {
        if (i > 0) json << ",";
        json << "{\"streams\":" << results[i].streams 
             << ",\"separate_time_us\":" << results[i].separate_time_us
             << ",\"optimized_time_us\":" << results[i].optimized_time_us << "}";
    }
    json << "],";
    // Итоговые PMU метрики
    json << "\"pmu_summary\":{";
    json << "\"separate\":" << separatePmu.toJson() << ",";
    json << "\"optimized\":" << optimizedPmu.toJson();
    json << "}}";
    
    return json.str();
}

