#include "common.hpp"
#include <numeric>
#include <random>

// ==================== ЭКСПЕРИМЕНТ: КОНФЛИКТЫ В КЭШ-ПАМЯТИ ====================
//
// Суть эксперимента:
// - Чтение с шагом = размер банка вызывает конфликты (все данные попадают в один набор)
// - Чтение с шагом = размер банка + линейка избегает конфликтов
// По графику должны получиться две линии:
// - Красная (с конфликтами) выше
// - Зелёная (без конфликтов) ниже
//
// Конфликт проявляется только при повторном чтении: если читаемых линеек больше,
// чем степень ассоциативности, линейки одного набора вытесняют друг друга.
// Поэтому для каждого количества линеек n = 1..param3 линейки многократно читаются
// по кругу и замеряется среднее время одного обращения. Красная линия поднимается,
// как только n превышает степень ассоциативности кэша.
//
// Замер одиночного обращения (как в методичке) на современных ЭВМ не работает:
// например, у AMD Zen счётчик TSC меняется шагами ~10 нс, что больше задержки L1/L2.
// Линейки связаны в кольцо в случайном порядке (каждая хранит адрес следующей):
// загрузки зависят друг от друга, поэтому время равно задержке кэш-памяти,
// а аппаратная предвыборка не может угадать следующий адрес.

/**
 * @brief Эксперимент исследования конфликтов в кэш-памяти
 *
 * Параметры:
 * - param1: 0 (автоопределение) или 1..256 (КБ) - Размер банка кэш-памяти
 *           (банк — один путь наборно-ассоциативного кэша: объём L1 / степень ассоциативности)
 * - param2: 0 (автоопределение) или 1..128 (Б) - Размер линейки кэш-памяти
 * - param3: 2..512 - Количество читаемых линеек
 */
std::string cacheConflictsExperiment(const std::string& params) {
    int param1 = SimpleJsonParser::getInt(params, "param1", 0);  // КБ размер банка (0 = автоопределение)
    int param2 = SimpleJsonParser::getInt(params, "param2", 0);  // Б линейка (0 = автоопределение)
    int param3 = SimpleJsonParser::getInt(params, "param3", 64);  // количество линеек
    
    // Автоопределение размера банка кэша: объём L1 данных / степень ассоциативности
    if (param1 <= 0) {
        size_t ways = getL1Associativity();
        size_t bankSize = getL1CacheSize() / (ways > 0 ? ways : 1);
        param1 = static_cast<int>(bankSize / 1024);
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
    // Большие страницы: набор кэша L2/L3 выбирается по физическому адресу
    PageBuffer buffer(totalSize, PageMode::Huge);
    if (!buffer.ok()) {
        return allocationError(totalSize);
    }
    char* base = buffer.bytes();
    memset(base, 0, totalSize);
    
    // PMU счётчики
    PerfCounters perfCounters;
    PmuMetrics conflictPmu, noConflictPmu;
    
    struct DataPoint {
        int lines;                   // Количество читаемых линеек
        size_t offset_conflict;      // Смещение последней линейки при чтении с конфликтами
        size_t offset_no_conflict;   // Смещение последней линейки при чтении без конфликтов
        double conflict_time_ns;     // Среднее время обращения с конфликтами
        double no_conflict_time_ns;  // Среднее время обращения без конфликтов
    };
    std::vector<DataPoint> results;
    
    prepareForMeasurement();
    
    printf("\n[EXP5] ========== cache_conflicts ==========\n");
    printf("[EXP5] Параметры: param1=%d КБ (банк), param2=%d Б (линейка), param3=%d линеек\n", param1, param2, param3);
    printf("[EXP5] bankSize=%zu байт, lineSize=%zu байт, totalSize=%zu байт\n", bankSize, lineSize, totalSize);
    fflush(stdout);
    
    const int NUM_REPEATS = 3;           // Повторы замера (берём минимум)
    const size_t MIN_HOPS = 32768;       // Обращений в одном замере: интервал заметно больше разрешения таймера
    
    // Среднее время одного обращения при циклическом чтении n линеек с шагом stride (нс)
    auto chaseLatencyNs = [&](size_t stride, int n) -> double {
        std::vector<int> order(n);
        std::iota(order.begin(), order.end(), 0);
        std::shuffle(order.begin(), order.end(), std::mt19937(12345 + n));
        for (int k = 0; k < n; k++) {
            char* next = base + static_cast<size_t>(order[(k + 1) % n]) * stride;
            memcpy(base + static_cast<size_t>(order[k]) * stride, &next, sizeof(next));
        }
        
        size_t hops = std::max(MIN_HOPS, static_cast<size_t>(n) * 64);
        double best = std::numeric_limits<double>::max();
        for (int rep = 0; rep < NUM_REPEATS; rep++) {
            char* p = base + static_cast<size_t>(order[0]) * stride;
            for (int k = 0; k < n; k++) {
                p = loadPointer(p);  // Прогрев: один круг
            }
            auto start = std::chrono::steady_clock::now();
            for (size_t h = 0; h < hops; h++) {
                p = loadPointer(p);
            }
            auto end = std::chrono::steady_clock::now();
            keepInRegister(p);
            best = std::min(best, std::chrono::duration<double, std::nano>(end - start).count() / hops);
        }
        return best;
    };
    
    std::vector<double> conflictTimes(maxLines + 1), noConflictTimes(maxLines + 1);
    
    // ===== ЧТЕНИЕ С КОНФЛИКТАМИ =====
    // Шаг = размер банка, все данные попадают в один набор кэша
    printf("[EXP5] Измерение чтения С КОНФЛИКТАМИ (шаг = bankSize = %zu)...\n", bankSize);
    fflush(stdout);
    
    perfCounters.start();
    for (int n = 1; n <= maxLines; n++) {
        if (isCancelled()) {
            return "{\"error\":\"Experiment cancelled\",\"cancelled\":true}";
        }
        conflictTimes[n] = chaseLatencyNs(bankSize, n);
    }
    perfCounters.stop();
    conflictPmu = perfCounters.read();
    
    // ===== ЧТЕНИЕ БЕЗ КОНФЛИКТОВ =====
    // Шаг = размер банка + линейка, данные распределяются по разным наборам
    printf("[EXP5] Измерение чтения БЕЗ КОНФЛИКТОВ (шаг = bankSize + lineSize = %zu)...\n", bankSize + lineSize);
    fflush(stdout);
    
    perfCounters.start();
    for (int n = 1; n <= maxLines; n++) {
        if (isCancelled()) {
            return "{\"error\":\"Experiment cancelled\",\"cancelled\":true}";
        }
        noConflictTimes[n] = chaseLatencyNs(bankSize + lineSize, n);
    }
    perfCounters.stop();
    noConflictPmu = perfCounters.read();
    
    // Собираем результаты
    for (int n = 1; n <= maxLines; n++) {
        results.push_back({
            n,
            (n - 1) * bankSize,
            (n - 1) * (bankSize + lineSize),
            conflictTimes[n],
            noConflictTimes[n]
        });
    }
    
    // Расчёт выводов для заданного количества линеек (param3)
    double avgConflictTime = conflictTimes[maxLines];
    double avgNoConflictTime = noConflictTimes[maxLines];
    double totalConflictTime = avgConflictTime * maxLines;      // Чтение всех линеек один раз
    double totalNoConflictTime = avgNoConflictTime * maxLines;
    double conflictToNoConflictRatio = (avgNoConflictTime > 0) ? avgConflictTime / avgNoConflictTime : 0;
    
    // Наименьшее количество линеек, при котором чтение с конфликтами заметно (в 1.5 раза)
    // медленнее: линейки перестали помещаться в один набор, т.е. степень ассоциативности
    // примерно на единицу меньше
    int conflictThreshold = 0;
    for (int n = 1; n <= maxLines; n++) {
        if (conflictTimes[n] > 1.5 * noConflictTimes[n]) {
            conflictThreshold = n;
            break;
        }
    }
    
    printf("[EXP5] Среднее время с конфликтами: %.2f нс\n", avgConflictTime);
    printf("[EXP5] Среднее время без конфликтов: %.2f нс\n", avgNoConflictTime);
    printf("[EXP5] Отношение (с конфликтами / без): %.2fx\n", conflictToNoConflictRatio);
    printf("[EXP5] Конфликты начинаются с %d линеек\n", conflictThreshold);
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
    json << "\"total_conflict_time_ns\":" << totalConflictTime << ",";
    json << "\"total_no_conflict_time_ns\":" << totalNoConflictTime << ",";
    json << "\"avg_conflict_time_ns\":" << avgConflictTime << ",";
    json << "\"avg_no_conflict_time_ns\":" << avgNoConflictTime << ",";
    json << "\"conflict_to_no_conflict_ratio\":" << conflictToNoConflictRatio << ",";
    json << "\"conflict_threshold_lines\":" << conflictThreshold;
    json << "},";
    json << "\"dataPoints\":[";
    for (size_t i = 0; i < results.size(); i++) {
        if (i > 0) json << ",";
        json << "{\"line\":" << results[i].lines
             << ",\"offset_conflict\":" << results[i].offset_conflict
             << ",\"offset_no_conflict\":" << results[i].offset_no_conflict
             << ",\"conflict_time_ns\":" << results[i].conflict_time_ns
             << ",\"no_conflict_time_ns\":" << results[i].no_conflict_time_ns << "}";
    }
    json << "],";
    // Итоговые PMU метрики
    json << "\"pmu_summary\":{";
    json << "\"conflict\":" << conflictPmu.toJson() << ",";
    json << "\"no_conflict\":" << noConflictPmu.toJson();
    json << "}}";
    
    return json.str();
}
