#ifndef EXPERIMENTS_COMMON_HPP
#define EXPERIMENTS_COMMON_HPP

#include <string>
#include <map>
#include <functional>
#include <sstream>
#include <vector>
#include <cstdlib>
#include <chrono>
#include <cstring>
#include <limits>
#include <pthread.h>
#include <thread>
#include <tuple>
#include <cstdint>
#include <cstdio>
#include <sched.h>
#include <algorithm>
#include <iomanip>
#include <iostream>
#include <array>
#include <atomic>
#include <climits>
#include <sys/mman.h>

// x86 с SSE2 (любой x86-64): доступны rdtsc, lfence, clflush
#if defined(__x86_64__) || (defined(__i386__) && defined(__SSE2__))
#define MEMLAB_X86 1
#include <x86intrin.h>
#endif

#ifdef __APPLE__
#include <sys/sysctl.h>
#include <mach/thread_policy.h>
#include <mach/thread_act.h>
#endif

#ifdef __linux__
#include <unistd.h>
#include <sys/syscall.h>
#include <sys/ioctl.h>
#include <linux/perf_event.h>
#include <asm/unistd.h>
#endif

#include "../tester.cpp"

// ==================== PMU PERFORMANCE COUNTERS ====================

/**
 * @brief Результаты PMU счётчиков для одного измерения
 */
struct PmuMetrics {
    // Порядок совпадает с порядком счётчиков в PerfCounters
    enum Metric { INSTRUCTIONS, CYCLES, CACHE_MISSES, BRANCH_MISSES, DTLB_LOAD_MISSES, COUNT };
    static constexpr const char* NAMES[COUNT] = {
        "instructions", "cycles", "cache_misses", "branch_misses", "dtlb_load_misses"
    };

    std::array<uint64_t, COUNT> values{};
    // Нет данных: счётчик недоступен или почти не работал во время замера.
    // В JSON такая метрика выводится как null, а не как 0
    std::array<bool, COUNT> missing{};

    std::string json(Metric metric) const {
        return missing[metric] ? "null" : std::to_string(values[metric]);
    }

    std::string toJson() const {
        std::ostringstream out;
        out << "{";
        for (int m = 0; m < COUNT; m++) {
            out << "\"" << NAMES[m] << "\":" << json(static_cast<Metric>(m)) << ",";
        }
        out << "\"ipc\":";
        if (missing[INSTRUCTIONS] || missing[CYCLES] || values[CYCLES] == 0) {
            out << "null";
        } else {
            out << std::fixed << std::setprecision(4)
                << static_cast<double>(values[INSTRUCTIONS]) / values[CYCLES];
        }
        out << "}";
        return out.str();
    }

    // Сложение метрик: сумма неизвестна, если неизвестно хотя бы одно слагаемое
    PmuMetrics& operator+=(const PmuMetrics& other) {
        for (int m = 0; m < COUNT; m++) {
            values[m] += other.values[m];
            missing[m] = missing[m] || other.missing[m];
        }
        return *this;
    }
};

/**
 * @brief Класс для сбора PMU метрик через Linux perf_event API
 */
class PerfCounters {
private:
#ifdef __linux__
    static constexpr size_t NUM_COUNTERS = PmuMetrics::COUNT;

    // Событий 5: столько свободных аппаратных счётчиков у AMD Zen 3 (6, один занят
    // NMI watchdog) и у Intel (instructions и cycles на фиксированных счётчиках,
    // остальные — на 4 общих), поэтому обычно все события считаются одновременно.
    // Если счётчиков всё же не хватает, ядро чередует события по времени: вместе со
    // значением читаем, сколько событие было включено и сколько реально считалось.
    struct CounterSample {
        uint64_t value = 0;
        uint64_t timeEnabled = 0;
        uint64_t timeRunning = 0;
    };
    
    std::array<int, NUM_COUNTERS> fds;
    std::array<CounterSample, NUM_COUNTERS> startSamples{};
    bool initialized = false;
    
    static long perf_event_open(struct perf_event_attr *hw_event, pid_t pid,
                                int cpu, int group_fd, unsigned long flags) {
        return syscall(__NR_perf_event_open, hw_event, pid, cpu, group_fd, flags);
    }
    
    int openCounter(uint32_t type, uint64_t config) {
        struct perf_event_attr pe;
        memset(&pe, 0, sizeof(pe));
        pe.type = type;
        pe.size = sizeof(pe);
        pe.config = config;
        pe.disabled = 1;
        pe.exclude_kernel = 1;
        pe.exclude_hv = 1;
        pe.read_format = PERF_FORMAT_TOTAL_TIME_ENABLED | PERF_FORMAT_TOTAL_TIME_RUNNING;
        
        return static_cast<int>(perf_event_open(&pe, 0, -1, -1, 0));
    }
    
    static bool readSample(int fd, CounterSample& sample) {
        return ::read(fd, &sample, sizeof(sample)) == static_cast<ssize_t>(sizeof(sample));
    }
#endif

public:
    PerfCounters() {
#ifdef __linux__
        fds.fill(-1);
        
        // Открываем счётчики: instructions, cycles, cache-misses, branch-misses, dTLB-load-misses
        fds[PmuMetrics::INSTRUCTIONS] = openCounter(PERF_TYPE_HARDWARE, PERF_COUNT_HW_INSTRUCTIONS);
        fds[PmuMetrics::CYCLES] = openCounter(PERF_TYPE_HARDWARE, PERF_COUNT_HW_CPU_CYCLES);
        fds[PmuMetrics::CACHE_MISSES] = openCounter(PERF_TYPE_HARDWARE, PERF_COUNT_HW_CACHE_MISSES);
        fds[PmuMetrics::BRANCH_MISSES] = openCounter(PERF_TYPE_HARDWARE, PERF_COUNT_HW_BRANCH_MISSES);
        fds[PmuMetrics::DTLB_LOAD_MISSES] = openCounter(PERF_TYPE_HW_CACHE,
                            (PERF_COUNT_HW_CACHE_DTLB) |
                            (PERF_COUNT_HW_CACHE_OP_READ << 8) |
                            (PERF_COUNT_HW_CACHE_RESULT_MISS << 16));
        
        // Check how many counters opened successfully
        int successCount = 0;
        for (int fd : fds) {
            if (fd >= 0) successCount++;
        }
        
        // Consider initialized if at least instructions and cycles work
        initialized = (fds[0] >= 0 && fds[1] >= 0);
        
        if (successCount < (int)NUM_COUNTERS) {
            std::cerr << "[PMU] Note: " << successCount << "/" << NUM_COUNTERS 
                      << " counters available. Missing metrics are reported as null." << std::endl;
        }
        if (!initialized) {
            std::cerr << "[PMU] Warning: Core counters (instructions/cycles) failed. "
                      << "Try: echo 0 | sudo tee /proc/sys/kernel/perf_event_paranoid" << std::endl;
        }
#endif
    }
    
    ~PerfCounters() {
#ifdef __linux__
        for (int fd : fds) {
            if (fd >= 0) {
                close(fd);
            }
        }
#endif
    }
    
    bool isAvailable() const {
#ifdef __linux__
        return initialized;
#else
        return false;
#endif
    }
    
    void start() {
#ifdef __linux__
        if (!initialized) return;
        // PERF_EVENT_IOC_RESET не сбрасывает время включения/работы,
        // поэтому запоминаем начальные значения и дальше считаем разности
        for (size_t i = 0; i < NUM_COUNTERS; i++) {
            if (fds[i] >= 0) {
                if (!readSample(fds[i], startSamples[i])) startSamples[i] = CounterSample{};
                ioctl(fds[i], PERF_EVENT_IOC_ENABLE, 0);
            }
        }
#endif
    }
    
    void stop() {
#ifdef __linux__
        if (!initialized) return;
        for (int fd : fds) {
            if (fd >= 0) {
                ioctl(fd, PERF_EVENT_IOC_DISABLE, 0);
            }
        }
#endif
    }
    
    PmuMetrics read() {
        PmuMetrics metrics;
        metrics.missing.fill(true);
#ifdef __linux__
        if (!initialized) return metrics;

        for (size_t i = 0; i < NUM_COUNTERS; i++) {
            CounterSample now;
            if (fds[i] < 0 || !readSample(fds[i], now)) continue;
            uint64_t value = now.value - startSamples[i].value;
            uint64_t enabled = now.timeEnabled - startSamples[i].timeEnabled;
            uint64_t running = now.timeRunning - startSamples[i].timeRunning;
            // Событие не считалось или считалось меньше 10% времени замера: оценка
            // по такой доле ненадёжна, поэтому данных нет (а не ноль)
            if (running == 0 || running * 10 < enabled) continue;
            // Оценка полного значения: событие считалось только running из enabled нс
            if (running < enabled) {
                value = static_cast<uint64_t>(static_cast<double>(value) * enabled / running);
            }
            metrics.values[i] = value;
            metrics.missing[i] = false;
        }
#endif
        return metrics;
    }
    
    // Удобный метод: измерить блок кода
    template<typename Func>
    PmuMetrics measure(Func&& func) {
        start();
        func();
        stop();
        return read();
    }
};

// ==================== ИЗОЛЯЦИЯ ЯДРА CPU ====================

/**
 * @brief Привязывает текущий поток к указанному ядру CPU
 * @param coreId Номер ядра (0-based)
 * @return true если привязка успешна
 */
inline bool pinToCore(int coreId) {
#ifdef __linux__
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(coreId, &cpuset);
    
    pthread_t currentThread = pthread_self();
    int result = pthread_setaffinity_np(currentThread, sizeof(cpu_set_t), &cpuset);
    return (result == 0);
#elif defined(__APPLE__)
    // macOS использует thread affinity через mach API (менее строгая привязка)
    thread_affinity_policy_data_t policy = { coreId };
    thread_port_t mach_thread = pthread_mach_thread_np(pthread_self());
    kern_return_t result = thread_policy_set(mach_thread, THREAD_AFFINITY_POLICY,
                                              (thread_policy_t)&policy, 1);
    return (result == KERN_SUCCESS);
#else
    (void)coreId;
    return false;
#endif
}

/**
 * @brief Устанавливает приоритет реального времени для текущего потока
 * @return true если установка успешна
 */
inline bool setRealtimePriority() {
#ifdef __linux__
    struct sched_param param;
    param.sched_priority = sched_get_priority_max(SCHED_FIFO);
    int result = pthread_setschedparam(pthread_self(), SCHED_FIFO, &param);
    return (result == 0);
#elif defined(__APPLE__)
    struct sched_param param;
    param.sched_priority = sched_get_priority_max(SCHED_FIFO);
    int result = pthread_setschedparam(pthread_self(), SCHED_FIFO, &param);
    return (result == 0);
#else
    return false;
#endif
}

/**
 * @brief Разгоняет ядро до рабочей частоты
 *
 * При энергосберегающем регуляторе частоты (powersave, schedutil) ядро стартует
 * на пониженной частоте, и первые точки графика получаются завышенными.
 */
inline void warmUpCpu(int durationMs = 200) {
    auto start = std::chrono::steady_clock::now();
    volatile uint64_t x = 0;
    while (std::chrono::steady_clock::now() - start < std::chrono::milliseconds(durationMs)) {
        for (int i = 0; i < 1000; i++) x += i;
    }
    (void)x;
}

/**
 * @brief Подготавливает текущий поток для точных измерений
 * Привязывает к последнему ядру, устанавливает RT приоритет и прогревает ядро.
 * Потоки, созданные после вызова, наследуют привязку к этому же ядру,
 * поэтому повторные замеры нужно выполнять последовательно в этом потоке.
 */
inline void prepareForMeasurement() {
#ifdef __linux__
    // Получаем количество ядер
    int numCores = sysconf(_SC_NPROCESSORS_ONLN);
    if (numCores > 1) {
        // Привязываем к последнему ядру (обычно менее загружено)
        pinToCore(numCores - 1);
    }
    setRealtimePriority();
#elif defined(__APPLE__)
    setRealtimePriority();
#endif
    warmUpCpu();
}

// ==================== ТАЙМЕР ДЛЯ ОДИНОЧНЫХ ОБРАЩЕНИЙ ====================
//
// chrono::now() стоит ~20 нс и не упорядочен относительно загрузок из памяти:
// чтение может выполниться до первого или после второго отсчёта. Для замера одного
// обращения используется TSC с барьерами lfence: первый дожидается завершения
// предыдущих команд (в том числе загрузок), второй не даёт следующим командам начаться
// раньше отсчёта. rdtscp не используется: его нет на старых процессорах (до Intel Nehalem)
// и в некоторых виртуальных машинах.
// На других архитектурах — запасной вариант через steady_clock (разрешение хуже).

/**
 * @brief Текущее значение точного таймера в тиках
 */
inline uint64_t preciseTicks() {
#ifdef MEMLAB_X86
    asm volatile("" ::: "memory");  // компилятор не переносит обращения к памяти через отсчёт
    _mm_lfence();
    uint64_t t = __rdtsc();
    _mm_lfence();
    asm volatile("" ::: "memory");
    return t;
#else
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
#endif
}

/**
 * @brief Название источника точного таймера (для вывода в результаты)
 */
inline const char* preciseTimerName() {
#ifdef MEMLAB_X86
    return "rdtsc+lfence";
#else
    return "steady_clock";
#endif
}

/**
 * @brief Количество тиков точного таймера в одной наносекунде (калибруется один раз)
 */
inline double preciseTicksPerNs() {
    static const double ticksPerNs = [] {
#ifdef MEMLAB_X86
        auto c0 = std::chrono::steady_clock::now();
        uint64_t t0 = preciseTicks();
        while (std::chrono::steady_clock::now() - c0 < std::chrono::milliseconds(50)) {}
        uint64_t t1 = preciseTicks();
        double ns = std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - c0).count();
        return ns > 0 ? static_cast<double>(t1 - t0) / ns : 1.0;
#else
        return 1.0;
#endif
    }();
    return ticksPerNs;
}

/**
 * @brief Накладные расходы пары отсчётов таймера в тиках
 * Вызывать после prepareForMeasurement(): в тиках TSC они зависят от частоты ядра.
 */
inline uint64_t measureTimerOverhead() {
    uint64_t best = UINT64_MAX;
    for (int i = 0; i < 10000; i++) {
        uint64_t t0 = preciseTicks();
        uint64_t t1 = preciseTicks();
        best = std::min(best, t1 - t0);
    }
    return best;
}

/**
 * @brief Минимальный различимый интервал точного таймера в тиках
 * Например, у AMD Zen счётчик TSC увеличивается шагами ~34 тика (~10 нс),
 * поэтому более короткие интервалы (попадание в L1/L2) одиночным замером не различить.
 */
inline uint64_t measureTimerResolution() {
#ifdef MEMLAB_X86
    uint64_t best = UINT64_MAX;
    for (int i = 0; i < 100000; i++) {
        uint64_t a = __rdtsc();
        uint64_t b = __rdtsc();
        if (b - a > 1) best = std::min(best, b - a);
    }
    return best == UINT64_MAX ? 1 : best;
#else
    return 1;
#endif
}

/**
 * @brief Читает 4 байта по произвольному (в т.ч. невыровненному) адресу
 */
inline int loadInt(const char* address) {
    int value;
    memcpy(&value, address, sizeof(value));
    return value;
}

/**
 * @brief Читает указатель по произвольному адресу (для обхода связанных линеек)
 */
inline char* loadPointer(const char* address) {
    char* value;
    memcpy(&value, address, sizeof(value));
    return value;
}

/**
 * @brief Держит значение в регистре на каждой итерации цикла
 * Не даёт компилятору векторизовать цикл или выбросить вычисления,
 * не добавляя обращений к памяти (в отличие от volatile-переменной).
 */
template <typename T>
inline void keepInRegister(T& value) {
    asm volatile("" : "+r"(value));
}

/**
 * @brief Регистр функций для выполнения по команде
 */

// Тип функции для регистрации (принимает JSON параметры, возвращает JSON результат)
using TestFunction = std::function<std::string(const std::string&)>;

/**
 * @brief Экранирует строку для вставки в JSON
 */
inline std::string jsonEscape(const std::string& str) {
    std::string escaped;
    for (char c : str) {
        switch (c) {
            case '"': escaped += "\\\""; break;
            case '\\': escaped += "\\\\"; break;
            case '\n': escaped += "\\n"; break;
            case '\r': escaped += "\\r"; break;
            case '\t': escaped += "\\t"; break;
            default: escaped += c; break;
        }
    }
    return escaped;
}

/**
 * @brief Простой парсер JSON для извлечения числовых параметров
 */
class SimpleJsonParser {
public:
    static int getInt(const std::string& json, const std::string& key, int defaultValue = 0) {
        std::string searchKey = "\"" + key + "\"";
        size_t pos = json.find(searchKey);
        if (pos == std::string::npos) return defaultValue;
        
        pos = json.find(":", pos);
        if (pos == std::string::npos) return defaultValue;
        
        pos++;
        while (pos < json.length() && (json[pos] == ' ' || json[pos] == '\t')) pos++;
        
        // strtoll вместо stoi: слишком большое число не бросает исключение
        // (оно уронило бы весь сервер), а ограничивается диапазоном int
        const char* start = json.c_str() + pos;
        char* end = nullptr;
        long long value = strtoll(start, &end, 10);
        if (end == start) return defaultValue;
        if (value > INT_MAX) return INT_MAX;
        if (value < INT_MIN) return INT_MIN;
        return static_cast<int>(value);
    }

    static std::string getString(const std::string& json, const std::string& key, const std::string& defaultValue = "") {
        std::string searchKey = "\"" + key + "\"";
        size_t pos = json.find(searchKey);
        if (pos == std::string::npos) return defaultValue;
        
        pos = json.find(":", pos);
        if (pos == std::string::npos) return defaultValue;
        
        // Find opening quote
        pos = json.find("\"", pos);
        if (pos == std::string::npos) return defaultValue;
        pos++;
        
        size_t endPos = json.find("\"", pos);
        if (endPos == std::string::npos) return defaultValue;
        
        return json.substr(pos, endPos - pos);
    }
};

/**
 * @brief Класс для управления регистрацией и выполнением функций
 */
class FunctionRegistry {
private:
    std::map<std::string, TestFunction> functions;
    std::map<std::string, std::string> descriptions;

public:
    /**
     * @brief Регистрирует функцию по имени
     */
    void registerFunction(const std::string& name, const std::string& description, TestFunction func) {
        functions[name] = func;
        descriptions[name] = description;
    }

    /**
     * @brief Выполняет функцию по имени с параметрами
     */
    std::string execute(const std::string& name, const std::string& params = "{}") {
        auto it = functions.find(name);
        if (it == functions.end()) {
            return "{\"error\":\"Function not found\",\"functionName\":\"" + jsonEscape(name) + "\"}";
        }
        // Исключение внутри эксперимента не должно ронять весь сервер
        try {
            return it->second(params);
        } catch (const std::bad_alloc&) {
            return "{\"error\":\"Not enough memory for experiment\",\"functionName\":\"" + jsonEscape(name) + "\"}";
        } catch (const std::exception& e) {
            return "{\"error\":\"" + jsonEscape(e.what()) + "\",\"functionName\":\"" + jsonEscape(name) + "\"}";
        }
    }

    /**
     * @brief Проверяет существование функции
     */
    bool hasFunction(const std::string& name) const {
        return functions.find(name) != functions.end();
    }

    /**
     * @brief Возвращает список всех функций в JSON
     */
    std::string listFunctionsJson() const {
        std::ostringstream json;
        json << "{\"functions\":[";
        bool first = true;
        for (const auto& pair : functions) {
            if (!first) json << ",";
            json << "{\"name\":\"" << pair.first << "\",";
            json << "\"description\":\"" << descriptions.at(pair.first) << "\"}";
            first = false;
        }
        json << "]}";
        return json.str();
    }
};

// Глобальный экземпляр реестра функций
static FunctionRegistry functionRegistry;

// Флаг для отмены текущего эксперимента (пишется из потока прослушивания сокета).
// Сбрасывает его сервер перед запуском эксперимента, сами эксперименты только проверяют
static std::atomic<bool> cancelExperiment{false};

/**
 * @brief Устанавливает флаг отмены эксперимента
 */
inline void setCancelExperiment(bool cancel) {
    cancelExperiment.store(cancel, std::memory_order_relaxed);
}

/**
 * @brief Проверяет флаг отмены
 */
inline bool isCancelled() {
    return cancelExperiment.load(std::memory_order_relaxed);
}

/**
 * @brief Определяет размер линейки кэша системы
 * @return Размер линейки кэша в байтах (64 по умолчанию)
 */
inline size_t getCacheLineSize() {
#ifdef __linux__
    // Linux: читаем из sysfs
    FILE* f = fopen("/sys/devices/system/cpu/cpu0/cache/index0/coherency_line_size", "r");
    if (f) {
        size_t lineSize = 0;
        if (fscanf(f, "%zu", &lineSize) == 1 && lineSize > 0) {
            fclose(f);
            return lineSize;
        }
        fclose(f);
    }
#elif defined(__APPLE__)
    size_t lineSize = 0;
    size_t sizeOfLineSize = sizeof(lineSize);
    if (sysctlbyname("hw.cachelinesize", &lineSize, &sizeOfLineSize, NULL, 0) == 0 && lineSize > 0) {
        return lineSize;
    }
#endif
    return 64; // Значение по умолчанию для большинных современных процессоров
}

/**
 * @brief Определяет размер кэша L1 данных
 * @return Размер кэша L1 в байтах (32768 по умолчанию)
 */
inline size_t getL1CacheSize() {
#ifdef __linux__
    // Linux: читаем из sysfs (index0 обычно L1 data cache)
    FILE* f = fopen("/sys/devices/system/cpu/cpu0/cache/index0/size", "r");
    if (f) {
        int size = 0;
        char unit = 'K';
        if (fscanf(f, "%d%c", &size, &unit) >= 1 && size > 0) {
            fclose(f);
            if (unit == 'K' || unit == 'k') return static_cast<size_t>(size) * 1024;
            if (unit == 'M' || unit == 'm') return static_cast<size_t>(size) * 1024 * 1024;
            return static_cast<size_t>(size);
        }
        fclose(f);
    }
#elif defined(__APPLE__)
    size_t cacheSize = 0;
    size_t sizeOfCacheSize = sizeof(cacheSize);
    // Пробуем получить размер L1 data cache
    if (sysctlbyname("hw.l1dcachesize", &cacheSize, &sizeOfCacheSize, NULL, 0) == 0 && cacheSize > 0) {
        return cacheSize;
    }
    // Fallback на perflevel L1 cache
    if (sysctlbyname("hw.perflevel0.l1dcachesize", &cacheSize, &sizeOfCacheSize, NULL, 0) == 0 && cacheSize > 0) {
        return cacheSize;
    }
#endif
    return 32768; // 32 КБ - типичное значение для L1 кэша
}

#ifdef __linux__
/**
 * @brief Читает числовой параметр кэша из sysfs (поддерживает суффиксы K и M)
 * @return Значение или 0, если файл недоступен
 */
inline size_t readSysfsCacheValue(int index, const char* name) {
    std::string path = "/sys/devices/system/cpu/cpu0/cache/index" + std::to_string(index) + "/" + name;
    FILE* f = fopen(path.c_str(), "r");
    if (!f) return 0;
    unsigned long long value = 0;
    char unit = 0;
    int read = fscanf(f, "%llu%c", &value, &unit);
    fclose(f);
    if (read < 1) return 0;
    if (unit == 'K' || unit == 'k') value *= 1024;
    if (unit == 'M' || unit == 'm') value *= 1024 * 1024;
    return static_cast<size_t>(value);
}

/**
 * @brief Проверяет, что index в sysfs описывает кэш данных или объединённый кэш
 */
inline bool isDataCacheIndex(int index) {
    std::string path = "/sys/devices/system/cpu/cpu0/cache/index" + std::to_string(index) + "/type";
    FILE* f = fopen(path.c_str(), "r");
    if (!f) return false;
    char type[32] = {0};
    bool ok = fscanf(f, "%31s", type) == 1;
    fclose(f);
    return ok && strcmp(type, "Instruction") != 0;
}
#endif

/**
 * @brief Определяет степень ассоциативности кэша L1 данных
 * @return Количество линеек в наборе или 0, если определить не удалось
 */
inline size_t getL1Associativity() {
#ifdef __linux__
    for (int index = 0; index < 16 && readSysfsCacheValue(index, "level") > 0; index++) {
        if (readSysfsCacheValue(index, "level") == 1 && isDataCacheIndex(index)) {
            return readSysfsCacheValue(index, "ways_of_associativity");
        }
    }
#endif
    return 0;
}

/**
 * @brief Определяет размер кэша последнего уровня (LLC)
 * @return Размер в байтах или 0, если определить не удалось
 */
inline size_t getLastLevelCacheSize() {
#ifdef __linux__
    size_t bestLevel = 0, bestSize = 0;
    for (int index = 0; index < 16; index++) {
        size_t level = readSysfsCacheValue(index, "level");
        if (level == 0) break;
        if (level >= bestLevel && isDataCacheIndex(index)) {
            bestLevel = level;
            bestSize = readSysfsCacheValue(index, "size");
        }
    }
    return bestSize;
#elif defined(__APPLE__)
    const char* names[] = {"hw.l3cachesize", "hw.perflevel0.l2cachesize", "hw.l2cachesize"};
    for (const char* name : names) {
        size_t cacheSize = 0;
        size_t sizeOfCacheSize = sizeof(cacheSize);
        if (sysctlbyname(name, &cacheSize, &sizeOfCacheSize, NULL, 0) == 0 && cacheSize > 0) {
            return cacheSize;
        }
    }
    return 0;
#else
    return 0;
#endif
}

/**
 * @brief Формирует предупреждение, если данные эксперимента помещаются в LLC
 *
 * Чтобы измерять динамическую память, а не кэш, объём читаемых данных должен
 * заметно (хотя бы в 4 раза) превышать кэш последнего уровня.
 * @return Текст предупреждения или пустая строка
 */
inline std::string llcWarning(size_t dataBytes, size_t llcBytes) {
    if (llcBytes == 0 || dataBytes >= 4 * llcBytes) return "";
    std::ostringstream text;
    text << "Объём данных " << dataBytes / (1024 * 1024) << " МБ меньше 4x LLC ("
         << llcBytes / (1024 * 1024) << " МБ): часть или все обращения обслуживает кэш, а не DRAM";
    return text.str();
}

// ==================== ВЫДЕЛЕНИЕ ПАМЯТИ ====================

/**
 * @brief Размер страниц для выделяемой области
 */
enum class PageMode {
    Huge,   // Большие страницы (THP, 2 МБ): физически непрерывные блоки по 2 МБ, мало промахов TLB
    Small   // Обычные страницы 4 КБ: для экспериментов, исследующих TLB
};

/**
 * @brief Область памяти, выровненная по 2 МБ, с явно заданным размером страниц
 *
 * Результаты не должны зависеть от настроек ОС (transparent_hugepage = always/madvise/never)
 * и от порядка экспериментов (glibc меняет порог mmap после free, и повторное выделение
 * может попасть в кучу с другим выравниванием), поэтому память всегда берётся новым mmap,
 * а размер страниц задаётся через madvise до первого обращения.
 * Адрес кратен 2 МБ (а значит, и 64 байтам, как в методичке).
 */
class PageBuffer {
public:
    static constexpr size_t ALIGNMENT = 2u << 20;
    
    PageBuffer(size_t size, PageMode mode) : size_(size) {
        size_t rounded = (size + ALIGNMENT - 1) / ALIGNMENT * ALIGNMENT;
        mapSize_ = rounded + ALIGNMENT;
        void* base = mmap(nullptr, mapSize_, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (base == MAP_FAILED) {
            mapSize_ = 0;
            return;
        }
        mapBase_ = base;
        uintptr_t aligned = (reinterpret_cast<uintptr_t>(base) + ALIGNMENT - 1) & ~static_cast<uintptr_t>(ALIGNMENT - 1);
        ptr_ = reinterpret_cast<char*>(aligned);
#ifdef __linux__
        madvise(ptr_, rounded, mode == PageMode::Huge ? MADV_HUGEPAGE : MADV_NOHUGEPAGE);
#else
        (void)mode;
#endif
    }
    
    ~PageBuffer() {
        if (mapBase_) munmap(mapBase_, mapSize_);
    }
    
    PageBuffer(const PageBuffer&) = delete;
    PageBuffer& operator=(const PageBuffer&) = delete;
    
    bool ok() const { return ptr_ != nullptr; }
    size_t size() const { return size_; }
    char* bytes() const { return ptr_; }
    
    template <typename T>
    T* as() const { return reinterpret_cast<T*>(ptr_); }

private:
    size_t size_ = 0;
    char* ptr_ = nullptr;
    void* mapBase_ = nullptr;
    size_t mapSize_ = 0;
};

/**
 * @brief JSON-ответ об ошибке выделения памяти
 */
inline std::string allocationError(size_t requestedBytes) {
    return "{\"error\":\"Failed to allocate memory\",\"requestedSize\":" + std::to_string(requestedBytes) + "}";
}

#endif // EXPERIMENTS_COMMON_HPP
