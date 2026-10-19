#include "common.hpp"

// ==================== ЭКСПЕРИМЕНТ: ОПТИМИЗАЦИЯ ЧТЕНИЯ ПАМЯТИ ====================

/**
 * @brief Эксперимент оптимизации чтения оперативной памяти
 *
 * Параметры:
 * - param1: 1..4 (МБ) - Размер массива
 * - param2: 1..128 - Количество потоков данных
 *
 * Сумма накапливается в обычной переменной, как в методичке. volatile-переменная
 * добавляла бы запись и чтение памяти на каждое обращение и ограничивала скорость
 * цикла сильнее, чем пропускная способность DRAM.
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
    
    // Отдельные массивы: как в методичке, каждый выделен отдельно по адресу, кратному 64.
    // Размещаем их подряд в одной области, чтобы раскладка не зависела от состояния
    // кучи (порог mmap в glibc меняется после предыдущих экспериментов)
    PageBuffer separateBuffer(arraySize * maxStreams, PageMode::Huge);
    if (!separateBuffer.ok()) {
        return allocationError(arraySize * maxStreams);
    }
    memset(separateBuffer.bytes(), 0, arraySize * maxStreams);
    std::vector<int*> separateArrays(maxStreams);
    for (int i = 0; i < maxStreams; i++) {
        separateArrays[i] = reinterpret_cast<int*>(separateBuffer.bytes() + arraySize * i);
    }
    
    // Оптимизированный массив (чередующиеся данные)
    PageBuffer optimizedBuffer(arraySize * maxStreams, PageMode::Huge);
    if (!optimizedBuffer.ok()) {
        return allocationError(arraySize * maxStreams);
    }
    int* optimizedArray = optimizedBuffer.as<int>();
    memset(optimizedArray, 0, arraySize * maxStreams);
    
    // С какого количества потоков читаемые данные перестают помещаться в LLC
    size_t llcSize = getLastLevelCacheSize();
    int dramFromStreams = 0;
    if (llcSize > 0) {
        dramFromStreams = static_cast<int>(llcSize / arraySize) + 1;
        if (dramFromStreams > maxStreams) dramFromStreams = 0;
    }
    
    // PMU счётчики
    PerfCounters perfCounters;
    PmuMetrics separatePmu, optimizedPmu;
    
    struct DataPoint {
        int streams;
        double separate_time_us;
        double optimized_time_us;
    };
    std::vector<DataPoint> results;
    
    // Повторы каждого замера (берём минимум)
    const int NUM_REPEATS = 3;
    
    prepareForMeasurement();
    
    printf("\n[EXP4] ========== memory_read_optimization ==========\n");
    printf("[EXP4] Параметры: param1=%d МБ, param2=%d потоков\n", param1, param2);
    printf("[EXP4] arraySize=%zu байт, maxStreams=%d, LLC=%zu байт\n", arraySize, maxStreams, llcSize);
    if (dramFromStreams > 0) {
        printf("[EXP4] Данные не помещаются в LLC начиная с %d потоков\n", dramFromStreams);
    }
    fflush(stdout);
    
    // Тестируем для разного количества потоков
    for (int streams = 1; streams <= maxStreams; streams++) {
        if (isCancelled()) {
            return "{\"error\":\"Experiment cancelled\",\"cancelled\":true}";
        }
        
        double separateTime = std::numeric_limits<double>::max();
        double optimizedTime = std::numeric_limits<double>::max();
        PmuMetrics bestSeparatePmu, bestOptimizedPmu;
        
        for (int rep = 0; rep < NUM_REPEATS; rep++) {
            // Несколько отдельных массивов
            perfCounters.start();
            auto start1 = std::chrono::steady_clock::now();
            int x1 = 0;
            for (size_t a = 0; a < arraySize; a += sizeof(int)) {
                for (int s = 0; s < streams; s++) {
                    x1 += separateArrays[s][a / sizeof(int)];
                    keepInRegister(x1);
                }
            }
            auto end1 = std::chrono::steady_clock::now();
            perfCounters.stop();
            double time1 = std::chrono::duration<double, std::micro>(end1 - start1).count();
            if (time1 < separateTime) {
                separateTime = time1;
                bestSeparatePmu = perfCounters.read();
            }
            
            // Один оптимизированный массив
            perfCounters.start();
            auto start2 = std::chrono::steady_clock::now();
            int x2 = 0;
            for (size_t a = 0; a < arraySize * streams; a += sizeof(int) * streams) {
                for (int s = 0; s < streams; s++) {
                    x2 += optimizedArray[(a / sizeof(int)) + s];
                    keepInRegister(x2);
                }
            }
            auto end2 = std::chrono::steady_clock::now();
            perfCounters.stop();
            double time2 = std::chrono::duration<double, std::micro>(end2 - start2).count();
            if (time2 < optimizedTime) {
                optimizedTime = time2;
                bestOptimizedPmu = perfCounters.read();
            }
        }
        separatePmu += bestSeparatePmu;
        optimizedPmu += bestOptimizedPmu;
        
        results.push_back({streams, separateTime, optimizedTime});
    }
    
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
    json << "\"param2_streams\":" << param2 << ",";
    json << "\"llc_bytes\":" << llcSize << ",";
    json << "\"dram_from_streams\":" << dramFromStreams;
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
