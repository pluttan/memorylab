#include "common.hpp"

// ==================== ЭКСПЕРИМЕНТ: ПРОИЗВОДИТЕЛЬНОСТЬ КЭШ-ПАМЯТИ ====================
//
// Эксперимент 6 программы PCLAB («CashReadWrite»). Блок памяти растёт, и для каждого
// размера блока замеряется время обработки одного килобайта в четырёх режимах:
// чтение, запись, чтение с записью, запись с последующим чтением.
// Пока блок помещается в кэш очередного уровня, время на килобайт почти постоянно.
// Когда блок превышает этот кэш, время скачком растёт. По ступеням графика
// определяются объёмы кэш-памяти L1, L2 и L3.
//
// Отличия от PCLAB:
// - Одно обращение на линейку кэша, а не к каждому 4-байтовому слову. Современный
//   процессор выполняет 2–3 чтения за такт, и при обращении к каждому слову скорость
//   упирается в число команд, а не в кэш: ступени L1 и L2 на графике пропадают.
// - Размер блока растёт на param2 байт, пока этот шаг больше 1/8 октавы, дальше по
//   8 точек на каждое удвоение. Иначе для блоков в десятки мегабайт (L3) точек были бы
//   миллионы, а у малых блоков не хватило бы точек для ступени L1.
// - Верхняя граница блока расширена до 1 ГБ: у современных ЭВМ L3 бывает до 96 МБ.
// - Режим «запись и чтение» в PCLAB по ошибке повторял «чтение и запись». Здесь, как в
//   описании эксперимента, блок сначала целиком записывается, потом целиком читается.

constexpr double CACHE_PERF_GROWTH = 1.0905;      // 2^(1/8): 8 точек на удвоение размера
constexpr double CACHE_PERF_JUMP = 1.3;           // Ступень: время выше плато в 1.3 раза
constexpr size_t CACHE_PERF_MIN_BYTES = 32u << 20; // Объём обработки в одном замере

enum class CacheAccessMode { Read, Write, ReadWrite, WriteThenRead };

/**
 * @brief Не даёт компилятору выбросить или объединить проходы по памяти
 */
inline void memoryBarrier() {
    asm volatile("" ::: "memory");
}

/**
 * @brief Чтение блока: одно слово на линейку, четыре независимых суммы
 * Независимые суммы убирают зависимость сложений, и скорость определяет кэш
 */
static uint64_t readPass(const uint64_t* words, size_t count, size_t stride) {
    uint64_t s0 = 0, s1 = 0, s2 = 0, s3 = 0;
    size_t i = 0;
    for (; i + 3 * stride < count; i += 4 * stride) {
        s0 += words[i];
        s1 += words[i + stride];
        s2 += words[i + 2 * stride];
        s3 += words[i + 3 * stride];
    }
    for (; i < count; i += stride) s0 += words[i];
    uint64_t sum = s0 + s1 + s2 + s3;
    keepInRegister(sum);
    return sum;
}

static void writePass(uint64_t* words, size_t count, size_t stride, uint64_t value) {
    for (size_t i = 0; i < count; i += stride) words[i] = value;
}

static void readWritePass(uint64_t* words, size_t count, size_t stride) {
    for (size_t i = 0; i < count; i += stride) words[i] += 1;
}

/**
 * @brief Выполняет passes проходов по блоку в заданном режиме
 */
static uint64_t runPasses(CacheAccessMode mode, uint64_t* words, size_t count, size_t stride, size_t passes) {
    uint64_t sink = 0;
    for (size_t pass = 0; pass < passes; pass++) {
        switch (mode) {
            case CacheAccessMode::Read:
                sink += readPass(words, count, stride);
                break;
            case CacheAccessMode::Write:
                writePass(words, count, stride, pass);
                break;
            case CacheAccessMode::ReadWrite:
                readWritePass(words, count, stride);
                break;
            case CacheAccessMode::WriteThenRead:
                writePass(words, count, stride, pass);
                memoryBarrier();
                sink += readPass(words, count, stride);
                break;
        }
        memoryBarrier();
    }
    return sink;
}

/**
 * @brief Размеры блоков: от param2 с шагом param2, затем 8 точек на удвоение
 * Все размеры кратны линейке кэша
 */
static std::vector<size_t> cacheBlockSizes(size_t step, size_t maxBytes, size_t lineSize) {
    auto roundUp = [lineSize](size_t value) { return (value + lineSize - 1) / lineSize * lineSize; };
    std::vector<size_t> sizes;
    for (size_t size = roundUp(std::max(step, lineSize)); size <= maxBytes;) {
        sizes.push_back(size);
        size_t geometric = static_cast<size_t>(static_cast<double>(size) * CACHE_PERF_GROWTH);
        size_t next = roundUp(std::max(size + step, geometric));
        size = next > size ? next : size + lineSize;
    }
    return sizes;
}

/**
 * @brief Ищет ступени на графике чтения
 *
 * Подъём в точке i — отношение времени через полоктавы (4 точки) к времени в точке i:
 * размытая ступень помещается в это окно целиком. В каждой октаве остаётся только
 * наибольший подъём, из подъёмов выше CACHE_PERF_JUMP берутся три наибольших.
 * Граница кэша — размер блока перед самым крутым шагом внутри окна подъёма.
 * Сравнение с плато здесь не годится: у Apple M и у кэша L3 время растёт плавно,
 * и порог над плато срабатывает в середине подъёма несколько раз.
 * @return До трёх размеров по возрастанию: оценки объёмов L1, L2 и L3
 */
static std::vector<size_t> findCacheBoundaries(const std::vector<size_t>& sizes, const std::vector<double>& time) {
    const size_t span = 4;     // Полоктавы при 8 точках на удвоение
    const size_t octave = 8;
    const size_t n = time.size();
    if (n <= span) return {};

    std::vector<double> rise(n, 0.0);
    for (size_t i = 0; i + span < n; i++) rise[i] = time[i + span] / time[i];

    std::vector<std::pair<double, size_t>> steps;  // (подъём, граница кэша)
    for (size_t i = 0; i + span < n; i++) {
        if (rise[i] < CACHE_PERF_JUMP) continue;
        bool largest = true;
        size_t from = i >= octave ? i - octave : 0;
        for (size_t j = from; j <= std::min(n - 1, i + octave) && largest; j++) {
            largest = !(rise[j] > rise[i] || (rise[j] == rise[i] && j < i));
        }
        if (!largest) continue;
        size_t steepest = i;
        for (size_t j = i; j < i + span; j++) {
            if (time[j + 1] / time[j] > time[steepest + 1] / time[steepest]) steepest = j;
        }
        steps.push_back({rise[i], sizes[steepest]});
    }

    std::sort(steps.begin(), steps.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
    std::vector<size_t> bounds;
    for (size_t k = 0; k < steps.size() && k < 3; k++) bounds.push_back(steps[k].second);
    std::sort(bounds.begin(), bounds.end());
    return bounds;
}

/**
 * @brief Эксперимент исследования производительности кэш-памяти
 *
 * Параметры:
 * - param1: 1..1048576 (КБ) - Наибольший размер обрабатываемого блока
 *           (в методичке 1..4096 КБ; для ступени L3 нужен блок больше L3)
 * - param2: 1..4096 (Б) - Шаг приращения размера блока
 */
std::string cachePerformanceExperiment(const std::string& params) {
    int param1 = SimpleJsonParser::getInt(params, "param1", 131072);  // КБ
    int param2 = SimpleJsonParser::getInt(params, "param2", 1024);    // Б

    if (param1 < 1) param1 = 1;
    if (param1 > 1048576) param1 = 1048576;
    if (param2 < 1) param2 = 1;
    if (param2 > 4096) param2 = 4096;

    size_t maxBytes = static_cast<size_t>(param1) * 1024;
    size_t step = static_cast<size_t>(param2);
    size_t lineSize = getCacheLineSize();
    size_t stride = std::max<size_t>(1, lineSize / sizeof(uint64_t));
    std::vector<size_t> sizes = cacheBlockSizes(step, maxBytes, lineSize);
    if (sizes.empty()) {
        return "{\"error\":\"Block is smaller than one cache line\"}";
    }

    // Большие страницы: промахи TLB не должны давать своих ступеней на графике
    PageBuffer buffer(sizes.back(), PageMode::Huge);
    if (!buffer.ok()) {
        return allocationError(sizes.back());
    }
    uint64_t* words = buffer.as<uint64_t>();
    memset(words, 0, sizes.back());

    std::vector<CacheDescriptor> caches = collectCaches();
    const CacheAccessMode modes[] = {
        CacheAccessMode::Read, CacheAccessMode::Write,
        CacheAccessMode::ReadWrite, CacheAccessMode::WriteThenRead
    };
    const int NUM_REPEATS = 3;  // Повторы замера (берём минимум)

    prepareForMeasurement();

    printf("\n[EXP6] ========== cache_performance ==========\n");
    printf("[EXP6] Параметры: param1=%d КБ, param2=%d Б, линейка %zu Б, точек: %zu\n",
           param1, param2, lineSize, sizes.size());
    fflush(stdout);

    // Время обработки одного килобайта блока (нс): минимум по повторам
    auto nsPerKb = [&](CacheAccessMode mode, size_t blockBytes) {
        size_t count = blockBytes / sizeof(uint64_t);
        size_t passes = std::max<size_t>(1, CACHE_PERF_MIN_BYTES / blockBytes);
        uint64_t sink = runPasses(mode, words, count, stride, 1);  // Прогрев: блок попадает в кэш
        double best = std::numeric_limits<double>::max();
        for (int rep = 0; rep < NUM_REPEATS; rep++) {
            auto start = std::chrono::steady_clock::now();
            sink += runPasses(mode, words, count, stride, passes);
            auto end = std::chrono::steady_clock::now();
            double ns = std::chrono::duration<double, std::nano>(end - start).count();
            best = std::min(best, ns / passes);
        }
        keepInRegister(sink);
        return best / (static_cast<double>(blockBytes) / 1024.0);
    };

    std::vector<std::array<double, 4>> times;
    for (size_t i = 0; i < sizes.size(); i++) {
        if (isCancelled()) {
            return "{\"error\":\"Experiment cancelled\",\"cancelled\":true}";
        }
        std::array<double, 4> point{};
        for (int m = 0; m < 4; m++) point[m] = nsPerKb(modes[m], sizes[i]);
        times.push_back(point);
        if ((i + 1) % 16 == 0 || i + 1 == sizes.size()) {
            printf("[EXP6] Прогресс: %zu/%zu\n", i + 1, sizes.size());
            fflush(stdout);
        }
    }

    std::vector<double> readTimes;
    for (const auto& point : times) readTimes.push_back(point[0]);
    std::vector<size_t> bounds = findCacheBoundaries(sizes, readTimes);
    auto bound = [&bounds](size_t index) { return index < bounds.size() ? bounds[index] : 0; };
    auto minmax = std::minmax_element(readTimes.begin(), readTimes.end());

    std::ostringstream json;
    json << "{\"experiment\":\"cache_performance\",";
    json << "\"parameters\":{";
    json << "\"param1_kb\":" << param1 << ",";
    json << "\"param2_b\":" << param2 << ",";
    json << "\"cache_line_bytes\":" << lineSize << ",";
    json << "\"points\":" << sizes.size() << ",";
    json << "\"repeats\":" << NUM_REPEATS;
    json << "},";
    json << "\"conclusions\":{";
    json << "\"step1_bytes\":" << bound(0) << ",";
    json << "\"step2_bytes\":" << bound(1) << ",";
    json << "\"step3_bytes\":" << bound(2) << ",";
    json << "\"os_l1_bytes\":" << dataCacheSize(caches, 1) << ",";
    json << "\"os_l2_bytes\":" << dataCacheSize(caches, 2) << ",";
    json << "\"os_l3_bytes\":" << dataCacheSize(caches, 3) << ",";
    json << "\"min_read_ns_per_kb\":" << *minmax.first << ",";
    json << "\"max_read_ns_per_kb\":" << *minmax.second;
    json << "},";
    json << "\"dataPoints\":[";
    for (size_t i = 0; i < sizes.size(); i++) {
        json << (i ? "," : "") << "{\"block_bytes\":" << sizes[i]
             << ",\"read_ns_per_kb\":" << times[i][0]
             << ",\"write_ns_per_kb\":" << times[i][1]
             << ",\"read_write_ns_per_kb\":" << times[i][2]
             << ",\"write_read_ns_per_kb\":" << times[i][3] << "}";
    }
    json << "]}";
    return json.str();
}
