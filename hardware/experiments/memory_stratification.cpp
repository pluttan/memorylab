#include "common.hpp"

// ==================== ЭКСПЕРИМЕНТ: ИССЛЕДОВАНИЕ РАССЛОЕНИЯ ПАМЯТИ ====================

/**
 * @brief Эксперимент исследования расслоения динамической памяти
 * 
 * Параметры (в JSON):
 * - param1: 1..128 (КБ) - Максимальное расстояние между читаемыми блоками
 * - param2: 4..64 (Б) - Шаг увеличения расстояния между 4-х байтовыми ячейками
 * - param3: 1..16 (МБ) - Размер массива
 * - cacheLine: размер линейки кэш-памяти (автоопределение если 0 или не указан)
 * 
 * @param params JSON строка с параметрами
 * @return JSON строка с результатами эксперимента
 */
std::string memoryStratificationExperiment(const std::string& params) {
    // Парсинг параметров
    int param1_kb = SimpleJsonParser::getInt(params, "param1", 64);   // КБ (макс. расстояние)
    int param2_b = SimpleJsonParser::getInt(params, "param2", 4);      // Б (шаг)
    int param3_mb = SimpleJsonParser::getInt(params, "param3", 8);     // МБ (размер массива)
    int cacheLine = SimpleJsonParser::getInt(params, "cacheLine", 0);  // Размер линейки кэша (0 = автоопределение)
    
    // Автоопределение размера линейки кэша если не указан
    if (cacheLine <= 0) {
        cacheLine = static_cast<int>(getCacheLineSize());
    }
    
    // Применяем ограничения из методички
    if (param1_kb < 1) param1_kb = 1;
    if (param1_kb > 128) param1_kb = 128;
    
    if (param2_b < 4) param2_b = 4;
    if (param2_b > 64) param2_b = 64;
    
    if (param3_mb < 1) param3_mb = 1;
    if (param3_mb > 16) param3_mb = 16;
    
    // Конвертация в байты
    size_t maxDistance = static_cast<size_t>(param1_kb) * 1024;  // param1 в байтах
    size_t stepSize = static_cast<size_t>(param2_b);              // param2 в байтах
    size_t arraySize = static_cast<size_t>(param3_mb) * 1024 * 1024; // param3 в байтах
    
    // Выделение памяти с выравниванием по 64 байтам
    int* p = static_cast<int*>(malloc64(arraySize));
    if (p == nullptr) {
        return "{\"error\":\"Failed to allocate memory\",\"requestedSize\":" + 
               std::to_string(arraySize) + "}";
    }
    
    // Инициализация массива для предотвращения ленивого выделения страниц
    memset(p, 0, arraySize);
    
    // Структура для хранения результатов с PMU метриками
    struct DataPoint {
        size_t step;
        double time_us;
        PmuMetrics pmu;
    };
    std::vector<DataPoint> results;
    PmuMetrics totalPmu; // Суммарные метрики
    PerfCounters perfCounters;
    
    // Количество повторений для каждого измерения (берём минимум для исключения влияния ОС)
    const int NUM_ITERATIONS = 3;
    
    // Попытка установить высокий приоритет потока (может потребовать root)
    #ifdef __APPLE__
    {
        pthread_t self = pthread_self();
        struct sched_param param;
        param.sched_priority = sched_get_priority_max(SCHED_FIFO);
        pthread_setschedparam(self, SCHED_FIFO, &param);
    }
    #endif
    
    // Основной цикл эксперимента
    setCancelExperiment(false);  // Сброс флага отмены
    prepareForMeasurement();    // Изоляция ядра CPU для точных измерений
    
    printf("\n[EXP1] ========== memory_stratification ==========\n");
    printf("[EXP1] Параметры: param1=%d КБ, param2=%d Б, param3=%d МБ, cacheLine=%d Б\n", 
           param1_kb, param2_b, param3_mb, cacheLine);
    printf("[EXP1] maxDistance=%zu байт, stepSize=%zu байт, arraySize=%zu байт\n", 
           maxDistance, stepSize, arraySize);
    printf("[EXP1] Всего шагов: %zu\n", maxDistance / stepSize);
    fflush(stdout);
    
    size_t stepCount = 0;
    size_t totalSteps = maxDistance / stepSize;
    
    for (size_t pg_size = stepSize; pg_size <= maxDistance; pg_size += stepSize) {
        // Проверка на отмену эксперимента
        if (isCancelled()) {
            free(p);
            return "{\"error\":\"Experiment cancelled\",\"cancelled\":true}";
        }
        
        // Массив для результатов из каждого потока
        std::vector<double> threadTimes(NUM_ITERATIONS, std::numeric_limits<double>::max());
        std::vector<std::thread> threads;
        threads.reserve(NUM_ITERATIONS);
        
        // Запускаем все итерации параллельно в отдельных потоках
        for (int iter = 0; iter < NUM_ITERATIONS; iter++) {
            threads.emplace_back([&, iter, pg_size]() {
                auto start = std::chrono::high_resolution_clock::now();
                
                volatile int x = 0;
                for (size_t b = 0; b < pg_size; b += stepSize) {
                    for (size_t a = b; a + sizeof(int) <= arraySize; a += pg_size) {
                        x += p[a / sizeof(int)];
                    }
                }
                
                auto end = std::chrono::high_resolution_clock::now();
                threadTimes[iter] = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count() / 1000.0;
            });
        }
        
        // Ждём завершения всех потоков
        for (auto& t : threads) {
            t.join();
        }
        
        // Находим минимальное время среди всех потоков
        double minTimeUs = *std::min_element(threadTimes.begin(), threadTimes.end());
        
        // Сбираем PMU метрики для этой точки
        PmuMetrics pointPmu;
        if (perfCounters.isAvailable()) {
            volatile int dummy = 0;
            pointPmu = perfCounters.measure([&]() {
                for (size_t b = 0; b < pg_size; b += stepSize) {
                    for (size_t a = b; a + sizeof(int) <= arraySize; a += pg_size) {
                        dummy += p[a / sizeof(int)];
                    }
                }
            });
            totalPmu += pointPmu;
        }
        
        results.push_back({pg_size, minTimeUs, pointPmu});
        
        stepCount++;
        if (stepCount % 10 == 0 || stepCount == totalSteps) {
            printf("[EXP1] Прогресс: %zu/%zu (%.1f%%)\n", stepCount, totalSteps, 100.0 * stepCount / totalSteps);
            fflush(stdout);
        }
    }
    
    printf("[EXP1] Завершено, получено %zu точек данных\n", results.size());
    fflush(stdout);
    
    // Освобождение памяти
    free(p);
    
    // Анализ результатов для определения характерных точек
    double maxTime = 0;
    size_t maxTimeStep = 0;
    double firstLocalMax = 0;
    size_t firstLocalMaxStep = 0;
    bool foundFirstMax = false;
    
    for (size_t i = 1; i < results.size() - 1; i++) {
        // Поиск глобального максимума
        if (results[i].time_us > maxTime) {
            maxTime = results[i].time_us;
            maxTimeStep = results[i].step;
        }
        
        // Поиск первого локального максимума (T1)
        if (!foundFirstMax && 
            results[i].time_us > results[i-1].time_us && 
            results[i].time_us > results[i+1].time_us &&
            results[i].step >= static_cast<size_t>(cacheLine)) {
            firstLocalMax = results[i].time_us;
            firstLocalMaxStep = results[i].step;
            foundFirstMax = true;
        }
    }
    
    // Расчёт параметров памяти
    int numBanks = 1;
    size_t pageSize = 0;
    
    if (foundFirstMax && firstLocalMaxStep > 0) {
        numBanks = static_cast<int>(firstLocalMaxStep / cacheLine);
        if (numBanks < 1) numBanks = 1;
    }
    
    if (maxTimeStep > 0 && numBanks > 0) {
        pageSize = maxTimeStep / numBanks;
    }
    
    // Формирование JSON результата
    std::ostringstream json;
    json << "{";
    json << "\"experiment\":\"memory_stratification\",";
    json << "\"parameters\":{";
    json << "\"param1_kb\":" << param1_kb << ",";
    json << "\"param2_b\":" << param2_b << ",";
    json << "\"param3_mb\":" << param3_mb << ",";
    json << "\"cacheLine\":" << cacheLine << ",";
    json << "\"maxDistance_bytes\":" << maxDistance << ",";
    json << "\"stepSize_bytes\":" << stepSize << ",";
    json << "\"arraySize_bytes\":" << arraySize;
    json << "},";
    
    // Результаты анализа
    json << "\"analysis\":{";
    json << "\"T1_step_bytes\":" << firstLocalMaxStep << ",";
    json << "\"T1_time_us\":" << firstLocalMax << ",";
    json << "\"T2_step_bytes\":" << maxTimeStep << ",";
    json << "\"T2_time_us\":" << maxTime << ",";
    json << "\"estimated_banks\":" << numBanks << ",";
    json << "\"estimated_page_size_bytes\":" << pageSize;
    json << "},";
    
    // Данные для графика
    json << "\"dataPoints\":[";
    for (size_t i = 0; i < results.size(); i++) {
        if (i > 0) json << ",";
        json << "{\"step\":" << results[i].step << ",\"time_us\":" << results[i].time_us;
        // PMU метрики для графика (реальное время)
        json << ",\"cache_misses\":" << results[i].pmu.cache_misses;
        json << ",\"branch_misses\":" << results[i].pmu.branch_misses;
        json << ",\"dtlb_load_misses\":" << results[i].pmu.dtlb_load_misses;
        json << "}";
    }
    json << "],";
    
    // Итоговые PMU метрики
    json << "\"pmu_summary\":" << totalPmu.toJson();
    
    json << "}";
    
    return json.str();
}
