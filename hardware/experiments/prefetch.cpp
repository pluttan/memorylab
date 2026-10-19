#include "common.hpp"
#include <random>

// ==================== ЭКСПЕРИМЕНТ: ИССЛЕДОВАНИЕ ПРЕДВЫБОРКИ ====================

// Сколько первых обращений отдавать для графика (статистика считается по всему блоку)
constexpr size_t PREFETCH_MAX_PLOT_POINTS = 8192;

/**
 * @brief Эксперимент исследования эффективности программной предвыборки
 *
 * Параметры:
 * - param1: 1..4096 (Б) - Шаг увеличения расстояния между читаемыми данными
 * - param2: 4..8192 (КБ) - Размер массива
 * - param3: 0 или 1 - Порядок обхода страниц: 0 — последовательно (как в методичке),
 *           1 — в случайном порядке (внутри страницы чтение остаётся последовательным)
 *
 * По методичке: замеряется время каждого обращения при последовательном чтении блока.
 * Без предвыборки первое обращение к каждой странице даёт пик (промах TLB, обход таблицы
 * страниц, открытие страницы DRAM). С предвыборкой перед замером выполняется проход
 * по одному обращению на страницу, загружающий информацию о страницах в TLB.
 * Блок размещается в обычных страницах (4 КБ), иначе при включённых больших страницах
 * границ страниц в блоке почти нет.
 *
 * Современные процессоры (например, AMD Zen 3) при последовательном чтении заранее
 * подгружают следующую страницу аппаратно, и пики пропадают. Случайный порядок
 * страниц (param3 = 1) не даёт аппаратной предвыборке угадать следующую страницу.
 */
std::string prefetchExperiment(const std::string& params) {
    int param1 = SimpleJsonParser::getInt(params, "param1", 64);   // Б шаг
    int param2 = SimpleJsonParser::getInt(params, "param2", 8192); // КБ размер
    int param3 = SimpleJsonParser::getInt(params, "param3", 0);    // порядок обхода страниц
    
    if (param1 < 1) param1 = 1;
    if (param1 > 4096) param1 = 4096;
    if (param2 < 4) param2 = 4;
    if (param2 > 8192) param2 = 8192;
    bool randomPageOrder = param3 != 0;
    
    size_t stepSize = static_cast<size_t>(param1);
    size_t arraySize = static_cast<size_t>(param2) * 1024;
    long sysPageSize = sysconf(_SC_PAGESIZE);
    size_t pageSize = sysPageSize > 0 ? static_cast<size_t>(sysPageSize) : 4096;
    size_t lineSize = getCacheLineSize();
    
    PageBuffer buffer(arraySize, PageMode::Small);
    if (!buffer.ok()) {
        return allocationError(arraySize);
    }
    const char* base = buffer.bytes();
    // Инициализация (записываем чтобы выделить физические страницы)
    memset(buffer.bytes(), 1, arraySize);
    
    // Буфер для вытеснения блока из кэш-памяти и записей о его страницах из TLB:
    // больше кэша последнего уровня, страниц больше, чем записей в TLB
    size_t llcSize = getLastLevelCacheSize();
    size_t evictSize = std::max<size_t>(64u << 20, 2 * llcSize);
    PageBuffer evictBuffer(evictSize, PageMode::Small);
    if (!evictBuffer.ok()) {
        return allocationError(evictSize);
    }
    memset(evictBuffer.bytes(), 2, evictSize);
    
    auto flushCachesAndTlb = [&]() {
#ifdef MEMLAB_X86
        for (size_t i = 0; i < arraySize; i += lineSize) {
            _mm_clflush(base + i);
        }
        _mm_mfence();
#endif
        const int* evict = evictBuffer.as<int>();
        int sum = 0;
        for (size_t i = 0; i < evictSize / sizeof(int); i += lineSize / sizeof(int)) {
            sum += evict[i];
            keepInRegister(sum);
        }
    };
    
    // Смещения читаемых ячеек: a = 0, step, 2*step, ... пока a + 4 <= arraySize
    std::vector<uint32_t> offsets;
    for (size_t a = 0; a + sizeof(int) <= arraySize; a += stepSize) {
        offsets.push_back(static_cast<uint32_t>(a));
    }
    if (randomPageOrder) {
        // Переставляем страницы целиком, сохраняя порядок обращений внутри страницы
        std::vector<std::vector<uint32_t>> pages;
        for (uint32_t a : offsets) {
            if (pages.empty() || a / pageSize != pages.back().front() / pageSize) pages.emplace_back();
            pages.back().push_back(a);
        }
        std::shuffle(pages.begin(), pages.end(), std::mt19937(12345));
        offsets.clear();
        for (const auto& page : pages) offsets.insert(offsets.end(), page.begin(), page.end());
    }
    size_t accesses = offsets.size();
    std::vector<uint32_t> noPrefetchTicks(accesses), prefetchTicks(accesses);
    
    // Один проход по блоку с замером времени каждого обращения
    auto timedPass = [&](std::vector<uint32_t>& ticks) -> bool {
        int x = 0;
        for (size_t i = 0; i < accesses; i++) {
            const char* address = base + offsets[i];
            uint64_t t0 = preciseTicks();
            x += loadInt(address);
            keepInRegister(x);
            uint64_t t1 = preciseTicks();
            ticks[i] = static_cast<uint32_t>(std::min<uint64_t>(t1 - t0, UINT32_MAX));
            if ((i & 4095) == 0 && isCancelled()) return false;
        }
        return true;
    };
    
    // Предвыборка: по одному обращению на каждую страницу блока. Возвращает время (нс)
    auto prefetchPages = [&]() {
        auto start = std::chrono::steady_clock::now();
        int y = 0;
        for (size_t i = 0; i < arraySize; i += pageSize) {
            y += loadInt(base + i);
            keepInRegister(y);
        }
        return std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - start).count();
    };
    
    // Время обработки всего блока одним интервалом (нс). Отношение считается по нему:
    // в сумме времён отдельных обращений преобладали бы накладные расходы и шаг таймера
    auto blockPass = [&]() {
        auto start = std::chrono::steady_clock::now();
        int x = 0;
        for (size_t i = 0; i < accesses; i++) {
            x += loadInt(base + offsets[i]);
            keepInRegister(x);
        }
        return std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - start).count();
    };
    
    // PMU счётчики
    PerfCounters perfCounters;
    PmuMetrics noPrefetchPmu, prefetchPmu;
    
    const int NUM_REPEATS = 3;  // Повторы замера времени обработки блока (берём минимум)
    
    prepareForMeasurement();
    uint64_t overheadTicks = measureTimerOverhead();
    uint64_t resolutionTicks = measureTimerResolution();
    double ticksPerNs = preciseTicksPerNs();
    
    printf("\n[EXP3] ========== prefetch ==========\n");
    printf("[EXP3] Параметры: param1=%d Б, param2=%d КБ, порядок страниц: %s\n",
           param1, param2, randomPageOrder ? "случайный" : "последовательный");
    printf("[EXP3] stepSize=%zu байт, arraySize=%zu байт, pageSize=%zu байт, обращений: %zu\n",
           stepSize, arraySize, pageSize, accesses);
    printf("[EXP3] Таймер: %s, %.3f тиков/нс, накладные расходы %llu тиков, разрешение %llu тиков\n",
           preciseTimerName(), ticksPerNs, static_cast<unsigned long long>(overheadTicks),
           static_cast<unsigned long long>(resolutionTicks));
    fflush(stdout);
    
    // === Время отдельных обращений (для графика и первого обращения к странице) ===
    flushCachesAndTlb();
    if (!timedPass(noPrefetchTicks)) {
        return "{\"error\":\"Experiment cancelled\",\"cancelled\":true}";
    }
    flushCachesAndTlb();
    prefetchPages();
    if (!timedPass(prefetchTicks)) {
        return "{\"error\":\"Experiment cancelled\",\"cancelled\":true}";
    }
    
    // === Время обработки всего блока без предвыборки и с предвыборкой ===
    double totalNoPrefetch = std::numeric_limits<double>::max();
    double totalPrefetch = std::numeric_limits<double>::max();
    double prefetchLoopNs = 0;
    for (int rep = 0; rep < NUM_REPEATS; rep++) {
        if (isCancelled()) {
            return "{\"error\":\"Experiment cancelled\",\"cancelled\":true}";
        }
        flushCachesAndTlb();
        perfCounters.start();
        double timeNo = blockPass();
        perfCounters.stop();
        if (timeNo < totalNoPrefetch) {
            totalNoPrefetch = timeNo;
            noPrefetchPmu = perfCounters.read();
        }
        
        flushCachesAndTlb();
        // Счётчики запускаем до предвыборки: запуск — это системные вызовы, а на
        // процессорах с защитой от Meltdown без PCID каждый из них сбрасывает TLB
        // и стёр бы результат предвыборки. Поэтому в PMU этого варианта входит и цикл предвыборки
        perfCounters.start();
        double loopTime = prefetchPages();
        double timePf = blockPass();
        perfCounters.stop();
        if (timePf < totalPrefetch) {
            totalPrefetch = timePf;
            prefetchLoopNs = loopTime;
            prefetchPmu = perfCounters.read();
        }
    }
    
    printf("[EXP3] Завершено, получено %zu точек данных\n", accesses);
    fflush(stdout);
    
    // Перевод в наносекунды за вычетом накладных расходов таймера
    auto toNs = [&](uint32_t ticks) {
        return ticks > overheadTicks ? (ticks - overheadTicks) / ticksPerNs : 0.0;
    };
    
    // Первое обращение к странице и остальные обращения
    double firstNoPrefetch = 0, firstPrefetch = 0, otherNoPrefetch = 0;
    double firstNoPrefetchTicks = 0;
    size_t firstCount = 0, otherCount = 0;
    for (size_t i = 0; i < accesses; i++) {
        bool firstInPage = (i == 0) || (offsets[i] / pageSize != offsets[i - 1] / pageSize);
        if (firstInPage) {
            firstNoPrefetch += toNs(noPrefetchTicks[i]);
            firstPrefetch += toNs(prefetchTicks[i]);
            firstNoPrefetchTicks += noPrefetchTicks[i] > overheadTicks ? noPrefetchTicks[i] - overheadTicks : 0;
            firstCount++;
        } else {
            otherNoPrefetch += toNs(noPrefetchTicks[i]);
            otherCount++;
        }
    }
    double noPrefetchToPrefetchRatio = (totalPrefetch > 0) ? totalNoPrefetch / totalPrefetch : 0;
    double withLoopRatio = (totalPrefetch + prefetchLoopNs > 0) ? totalNoPrefetch / (totalPrefetch + prefetchLoopNs) : 0;
    double pageFirstAccessNs = firstCount ? firstNoPrefetch / firstCount : 0;
    double pageFirstAccessTicks = firstCount ? firstNoPrefetchTicks / firstCount : 0;
    double pageFirstAccessPrefetchNs = firstCount ? firstPrefetch / firstCount : 0;
    double otherAccessNs = otherCount ? otherNoPrefetch / otherCount : 0;
    size_t plotPoints = std::min(accesses, PREFETCH_MAX_PLOT_POINTS);
    
    // Формирование JSON
    std::ostringstream json;
    json << "{";
    json << "\"experiment\":\"prefetch\",";
    json << "\"parameters\":{";
    json << "\"param1_b\":" << param1 << ",";
    json << "\"param2_kb\":" << param2 << ",";
    json << "\"page_order\":\"" << (randomPageOrder ? "random" : "sequential") << "\",";
    json << "\"page_size_bytes\":" << pageSize << ",";
    json << "\"total_accesses\":" << accesses << ",";
    json << "\"pages\":" << firstCount << ",";
    json << "\"plotted_points\":" << plotPoints << ",";
    json << "\"timer\":\"" << preciseTimerName() << "\",";
    json << "\"timer_overhead_ns\":" << overheadTicks / ticksPerNs << ",";
    json << "\"timer_resolution_ns\":" << resolutionTicks / ticksPerNs;
    json << "},";
    json << "\"conclusions\":{";
    json << "\"total_no_prefetch_ns\":" << totalNoPrefetch << ",";
    json << "\"total_prefetch_ns\":" << totalPrefetch << ",";
    json << "\"no_prefetch_to_prefetch_ratio\":" << noPrefetchToPrefetchRatio << ",";
    json << "\"with_prefetch_loop_ratio\":" << withLoopRatio << ",";
    json << "\"page_first_access_ns\":" << pageFirstAccessNs << ",";
    json << "\"page_first_access_tsc\":" << pageFirstAccessTicks << ",";
    json << "\"other_access_ns\":" << otherAccessNs << ",";
    json << "\"page_first_access_prefetch_ns\":" << pageFirstAccessPrefetchNs << ",";
    json << "\"prefetch_loop_ns\":" << prefetchLoopNs;
    json << "},";
    // offset — позиция в порядке обхода; при последовательном обходе совпадает со смещением в блоке
    json << "\"dataPoints\":[";
    for (size_t i = 0; i < plotPoints; i++) {
        if (i > 0) json << ",";
        json << "{\"offset\":" << i * stepSize
             << ",\"no_prefetch_ns\":" << toNs(noPrefetchTicks[i])
             << ",\"prefetch_ns\":" << toNs(prefetchTicks[i]) << "}";
    }
    json << "],";
    // Итоговые PMU метрики
    json << "\"pmu_summary\":{";
    json << "\"no_prefetch\":" << noPrefetchPmu.toJson() << ",";
    json << "\"prefetch\":" << prefetchPmu.toJson();
    json << "}}";
    
    return json.str();
}
