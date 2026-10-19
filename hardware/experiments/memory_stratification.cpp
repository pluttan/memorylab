#include "common.hpp"

// ==================== ЭКСПЕРИМЕНТ: ИССЛЕДОВАНИЕ РАССЛОЕНИЯ ПАМЯТИ ====================

// Точка считается значимым пиком, если она выше медианы соседних точек в PEAK_FACTOR раз.
// Медиана соседей нечувствительна к самим пикам (они обычно одиночные: шаг, кратный
// 4 КБ, 8 КБ и т.д.), а порог отсекает шум отдельных замеров.
constexpr double STRATIFICATION_PEAK_FACTOR = 1.5;
constexpr size_t STRATIFICATION_PEAK_WINDOW = 8;   // соседей с каждой стороны
constexpr size_t STRATIFICATION_MAX_PEAKS = 32;    // сколько пиков вернуть в JSON

/**
 * @brief Находит значимые локальные максимумы графика
 * @param y Время для каждого шага
 * @param firstIndex Индекс, с которого начинается поиск
 * @return Индексы пиков в порядке возрастания шага
 */
static std::vector<size_t> findSignificantPeaks(const std::vector<double>& y, size_t firstIndex) {
    std::vector<size_t> peaks;
    std::vector<double> neighbours;
    for (size_t i = std::max<size_t>(firstIndex, 1); i < y.size(); i++) {
        // У последней точки сравниваем только с левым соседом: пик часто приходится
        // ровно на максимальное расстояние (например, 4 КБ при param1 = 4)
        bool last = i + 1 == y.size();
        if (y[i] < y[i - 1] || (!last && y[i] < y[i + 1])) continue;
        
        size_t lo = i >= STRATIFICATION_PEAK_WINDOW ? i - STRATIFICATION_PEAK_WINDOW : 0;
        size_t hi = std::min(y.size() - 1, i + STRATIFICATION_PEAK_WINDOW);
        neighbours.clear();
        for (size_t j = lo; j <= hi; j++) {
            if (j != i) neighbours.push_back(y[j]);
        }
        std::nth_element(neighbours.begin(), neighbours.begin() + neighbours.size() / 2, neighbours.end());
        double median = neighbours[neighbours.size() / 2];
        
        if (y[i] > median * STRATIFICATION_PEAK_FACTOR) peaks.push_back(i);
    }
    return peaks;
}

/**
 * @brief Эксперимент исследования расслоения динамической памяти
 *
 * Параметры (в JSON):
 * - param1: 1..128 (КБ) - Максимальное расстояние между читаемыми блоками
 * - param2: 4..64 (Б) - Шаг увеличения расстояния между 4-х байтовыми ячейками
 * - param3: 1..1024 (МБ) - Размер массива. В методичке 1..16 МБ, но массив должен быть
 *           в несколько раз больше кэша последнего уровня (у современных ЭВМ 8..96 МБ),
 *           иначе после первого прохода все данные читаются из кэша, а не из DRAM
 * - cacheLine: размер линейки кэш-памяти (автоопределение если 0 или не указан)
 *
 * @param params JSON строка с параметрами
 * @return JSON строка с результатами эксперимента
 */
std::string memoryStratificationExperiment(const std::string& params) {
    // Парсинг параметров
    int param1_kb = SimpleJsonParser::getInt(params, "param1", 64);   // КБ (макс. расстояние)
    int param2_b = SimpleJsonParser::getInt(params, "param2", 64);     // Б (шаг)
    int param3_mb = SimpleJsonParser::getInt(params, "param3", 256);   // МБ (размер массива)
    int cacheLine = SimpleJsonParser::getInt(params, "cacheLine", 0);  // Размер линейки кэша (0 = автоопределение)
    
    // Автоопределение размера линейки кэша если не указан
    if (cacheLine <= 0) {
        cacheLine = static_cast<int>(getCacheLineSize());
    }
    
    // Применяем ограничения (верхняя граница размера массива расширена, см. выше)
    if (param1_kb < 1) param1_kb = 1;
    if (param1_kb > 128) param1_kb = 128;
    
    if (param2_b < 4) param2_b = 4;
    if (param2_b > 64) param2_b = 64;
    
    if (param3_mb < 1) param3_mb = 1;
    if (param3_mb > 1024) param3_mb = 1024;
    
    // Конвертация в байты
    size_t maxDistance = static_cast<size_t>(param1_kb) * 1024;  // param1 в байтах
    size_t stepSize = static_cast<size_t>(param2_b);              // param2 в байтах
    size_t arraySize = static_cast<size_t>(param3_mb) * 1024 * 1024; // param3 в байтах
    
    size_t llcSize = getLastLevelCacheSize();
    std::string warning = llcWarning(arraySize, llcSize);
    
    // Большие страницы: банк и строка DRAM выбираются по физическому адресу,
    // а внутри 2 МБ страницы физические адреса идут подряд, как виртуальные
    PageBuffer buffer(arraySize, PageMode::Huge);
    if (!buffer.ok()) {
        return allocationError(arraySize);
    }
    int* p = buffer.as<int>();
    
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
    
    // Количество повторений каждого замера (берём минимум для исключения влияния ОС).
    // Повторы выполняются последовательно в одном потоке: параллельные потоки
    // наследуют привязку к тому же ядру и делят его по времени, что завышает замер.
    const int NUM_ITERATIONS = 3;
    
    // Основной цикл эксперимента
    prepareForMeasurement();    // Изоляция ядра CPU для точных измерений
    
    printf("\n[EXP1] ========== memory_stratification ==========\n");
    printf("[EXP1] Параметры: param1=%d КБ, param2=%d Б, param3=%d МБ, cacheLine=%d Б\n",
           param1_kb, param2_b, param3_mb, cacheLine);
    printf("[EXP1] maxDistance=%zu байт, stepSize=%zu байт, arraySize=%zu байт, LLC=%zu байт\n",
           maxDistance, stepSize, arraySize, llcSize);
    if (!warning.empty()) {
        printf("[EXP1] ВНИМАНИЕ: %s\n", warning.c_str());
    }
    printf("[EXP1] Всего шагов: %zu\n", maxDistance / stepSize);
    fflush(stdout);
    
    size_t stepCount = 0;
    size_t totalSteps = maxDistance / stepSize;
    
    for (size_t pg_size = stepSize; pg_size <= maxDistance; pg_size += stepSize) {
        // Проверка на отмену эксперимента
        if (isCancelled()) {
            return "{\"error\":\"Experiment cancelled\",\"cancelled\":true}";
        }
        
        double minTimeUs = std::numeric_limits<double>::max();
        PmuMetrics pointPmu;
        
        for (int iter = 0; iter < NUM_ITERATIONS; iter++) {
            perfCounters.start();
            auto start = std::chrono::steady_clock::now();
            
            volatile int x = 0;
            for (size_t b = 0; b < pg_size; b += stepSize) {
                for (size_t a = b; a + sizeof(int) <= arraySize; a += pg_size) {
                    x += p[a / sizeof(int)];
                }
            }
            
            auto end = std::chrono::steady_clock::now();
            perfCounters.stop();
            
            double timeUs = std::chrono::duration<double, std::micro>(end - start).count();
            if (timeUs < minTimeUs) {
                // PMU метрики берём из того же прогона, что и время
                minTimeUs = timeUs;
                pointPmu = perfCounters.read();
            }
        }
        totalPmu += pointPmu;
        
        results.push_back({pg_size, minTimeUs, pointPmu});
        
        stepCount++;
        if (stepCount % 10 == 0 || stepCount == totalSteps) {
            printf("[EXP1] Прогресс: %zu/%zu (%.1f%%)\n", stepCount, totalSteps, 100.0 * stepCount / totalSteps);
            fflush(stdout);
        }
    }
    
    printf("[EXP1] Завершено, получено %zu точек данных\n", results.size());
    fflush(stdout);
    
    // Анализ результатов для определения характерных точек.
    // Шаги меньше линейки кэша не рассматриваем: там график пилообразный из-за попаданий в кэш
    std::vector<double> times;
    for (const auto& r : results) times.push_back(r.time_us);
    
    size_t firstIndex = 0;
    while (firstIndex < results.size() && results[firstIndex].step < static_cast<size_t>(cacheLine)) {
        firstIndex++;
    }
    
    // T1 — первый значимый локальный максимум
    std::vector<size_t> peaks = findSignificantPeaks(times, firstIndex);
    double firstLocalMax = 0;
    size_t firstLocalMaxStep = 0;
    if (!peaks.empty()) {
        firstLocalMax = results[peaks[0]].time_us;
        firstLocalMaxStep = results[peaks[0]].step;
    }
    
    // T2 — глобальный максимум
    double maxTime = 0;
    size_t maxTimeStep = 0;
    for (size_t i = firstIndex; i < results.size(); i++) {
        if (results[i].time_us > maxTime) {
            maxTime = results[i].time_us;
            maxTimeStep = results[i].step;
        }
    }
    
    // Расчёт параметров памяти.
    // Отсутствие значимых пиков по методичке означает один независимый банк
    int numBanks = 1;
    size_t pageSize = 0;
    
    if (firstLocalMaxStep > 0) {
        numBanks = static_cast<int>(firstLocalMaxStep / cacheLine);
        if (numBanks < 1) numBanks = 1;
    }
    
    if (maxTimeStep > 0) {
        pageSize = maxTimeStep / numBanks;
    }
    
    // Формирование JSON результата
    std::ostringstream json;
    json << "{";
    json << "\"experiment\":\"memory_stratification\",";
    if (!warning.empty()) {
        json << "\"warning\":\"" << jsonEscape(warning) << "\",";
    }
    json << "\"parameters\":{";
    json << "\"param1_kb\":" << param1_kb << ",";
    json << "\"param2_b\":" << param2_b << ",";
    json << "\"param3_mb\":" << param3_mb << ",";
    json << "\"cacheLine\":" << cacheLine << ",";
    json << "\"maxDistance_bytes\":" << maxDistance << ",";
    json << "\"stepSize_bytes\":" << stepSize << ",";
    json << "\"arraySize_bytes\":" << arraySize << ",";
    json << "\"llc_bytes\":" << llcSize << ",";
    json << "\"repeats\":" << NUM_ITERATIONS;
    json << "},";
    
    // Результаты анализа
    json << "\"analysis\":{";
    json << "\"T1_step_bytes\":" << firstLocalMaxStep << ",";
    json << "\"T1_time_us\":" << firstLocalMax << ",";
    json << "\"T2_step_bytes\":" << maxTimeStep << ",";
    json << "\"T2_time_us\":" << maxTime << ",";
    json << "\"estimated_banks\":" << numBanks << ",";
    json << "\"estimated_page_size_bytes\":" << pageSize << ",";
    json << "\"significant_peaks_bytes\":[";
    for (size_t i = 0; i < peaks.size() && i < STRATIFICATION_MAX_PEAKS; i++) {
        if (i > 0) json << ",";
        json << results[peaks[i]].step;
    }
    json << "]";
    json << "},";
    
    // Данные для графика
    json << "\"dataPoints\":[";
    for (size_t i = 0; i < results.size(); i++) {
        if (i > 0) json << ",";
        json << "{\"step\":" << results[i].step << ",\"time_us\":" << results[i].time_us;
        // PMU метрики для графика (реальное время)
        json << ",\"cache_misses\":" << results[i].pmu.json(PmuMetrics::CACHE_MISSES);
        json << ",\"branch_misses\":" << results[i].pmu.json(PmuMetrics::BRANCH_MISSES);
        json << ",\"dtlb_load_misses\":" << results[i].pmu.json(PmuMetrics::DTLB_LOAD_MISSES);
        json << "}";
    }
    json << "],";
    
    // Итоговые PMU метрики
    json << "\"pmu_summary\":" << totalPmu.toJson();
    
    json << "}";
    
    return json.str();
}
