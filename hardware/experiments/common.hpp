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
    uint64_t instructions = 0;
    uint64_t cycles = 0;
    uint64_t cache_misses = 0;
    uint64_t branch_misses = 0;
    uint64_t dtlb_load_misses = 0;
    uint64_t stalled_cycles_backend = 0;
    uint64_t cache_references = 0;
    uint64_t branches = 0;
    
    double getIPC() const {
        return cycles > 0 ? static_cast<double>(instructions) / cycles : 0.0;
    }
    
    std::string toJson() const {
        std::ostringstream json;
        json << "{";
        json << "\"instructions\":" << instructions << ",";
        json << "\"cycles\":" << cycles << ",";
        json << "\"cache_misses\":" << cache_misses << ",";
        json << "\"branch_misses\":" << branch_misses << ",";
        json << "\"dtlb_load_misses\":" << dtlb_load_misses << ",";
        json << "\"stalled_cycles_backend\":" << stalled_cycles_backend << ",";
        json << "\"cache_references\":" << cache_references << ",";
        json << "\"branches\":" << branches << ",";
        json << "\"ipc\":" << std::fixed << std::setprecision(4) << getIPC();
        json << "}";
        return json.str();
    }
    
    // Сложение метрик
    PmuMetrics& operator+=(const PmuMetrics& other) {
        instructions += other.instructions;
        cycles += other.cycles;
        cache_misses += other.cache_misses;
        branch_misses += other.branch_misses;
        dtlb_load_misses += other.dtlb_load_misses;
        stalled_cycles_backend += other.stalled_cycles_backend;
        cache_references += other.cache_references;
        branches += other.branches;
        return *this;
    }
};

/**
 * @brief Класс для сбора PMU метрик через Linux perf_event API
 */
class PerfCounters {
private:
#ifdef __linux__
    static constexpr size_t NUM_COUNTERS = 8;
    std::array<int, NUM_COUNTERS> fds;
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
        
        return static_cast<int>(perf_event_open(&pe, 0, -1, -1, 0));
    }
#endif

public:
    PerfCounters() {
#ifdef __linux__
        fds.fill(-1);
        
        // Открываем счётчики: instructions, cycles, cache-misses, branch-misses,
        // dTLB-load-misses, stalled-cycles-backend, cache-references, branches
        fds[0] = openCounter(PERF_TYPE_HARDWARE, PERF_COUNT_HW_INSTRUCTIONS);
        fds[1] = openCounter(PERF_TYPE_HARDWARE, PERF_COUNT_HW_CPU_CYCLES);
        fds[2] = openCounter(PERF_TYPE_HARDWARE, PERF_COUNT_HW_CACHE_MISSES);
        fds[3] = openCounter(PERF_TYPE_HARDWARE, PERF_COUNT_HW_BRANCH_MISSES);
        fds[4] = openCounter(PERF_TYPE_HW_CACHE, 
                            (PERF_COUNT_HW_CACHE_DTLB) | 
                            (PERF_COUNT_HW_CACHE_OP_READ << 8) | 
                            (PERF_COUNT_HW_CACHE_RESULT_MISS << 16));
        fds[5] = openCounter(PERF_TYPE_HARDWARE, PERF_COUNT_HW_STALLED_CYCLES_BACKEND);
        fds[6] = openCounter(PERF_TYPE_HARDWARE, PERF_COUNT_HW_CACHE_REFERENCES);
        fds[7] = openCounter(PERF_TYPE_HARDWARE, PERF_COUNT_HW_BRANCH_INSTRUCTIONS);
        
        // Check how many counters opened successfully
        int successCount = 0;
        for (int fd : fds) {
            if (fd >= 0) successCount++;
        }
        
        // Consider initialized if at least instructions and cycles work
        initialized = (fds[0] >= 0 && fds[1] >= 0);
        
        if (successCount < (int)NUM_COUNTERS) {
            std::cerr << "[PMU] Note: " << successCount << "/" << NUM_COUNTERS 
                      << " counters available. Some metrics may be zero." << std::endl;
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
        for (int fd : fds) {
            if (fd >= 0) {
                ioctl(fd, PERF_EVENT_IOC_RESET, 0);
                ioctl(fd, PERF_EVENT_IOC_ENABLE, 0);
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
#ifdef __linux__
        if (!initialized) return metrics;
        
        uint64_t values[NUM_COUNTERS] = {0};
        for (size_t i = 0; i < NUM_COUNTERS; i++) {
            if (fds[i] >= 0) {
                ::read(fds[i], &values[i], sizeof(uint64_t));
            }
        }
        
        metrics.instructions = values[0];
        metrics.cycles = values[1];
        metrics.cache_misses = values[2];
        metrics.branch_misses = values[3];
        metrics.dtlb_load_misses = values[4];
        metrics.stalled_cycles_backend = values[5];
        metrics.cache_references = values[6];
        metrics.branches = values[7];
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

// Глобальный экземпляр счётчиков для повторного использования
static PerfCounters globalPerfCounters;

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
 * @brief Подготавливает текущий поток для точных измерений
 * Привязывает к последнему ядру и устанавливает RT приоритет
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
}

/**
 * @brief Регистр функций для выполнения по команде
 */

// Глобальный экземпляр тестера
static Tester globalTester;

// Тип функции для регистрации (принимает JSON параметры, возвращает JSON результат)
using TestFunction = std::function<std::string(const std::string&)>;

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
        
        std::string numStr;
        while (pos < json.length() && (isdigit(json[pos]) || json[pos] == '-')) {
            numStr += json[pos++];
        }
        
        return numStr.empty() ? defaultValue : std::stoi(numStr);
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
        if (it != functions.end()) {
            return it->second(params);
        }
        return "{\"error\":\"Function not found\",\"functionName\":\"" + name + "\"}";
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

// Флаг для отмены текущего эксперимента
static volatile bool cancelExperiment = false;

/**
 * @brief Устанавливает флаг отмены эксперимента
 */
inline void setCancelExperiment(bool cancel) {
    cancelExperiment = cancel;
}

/**
 * @brief Проверяет флаг отмены
 */
inline bool isCancelled() {
    return cancelExperiment;
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

/**
 * @brief Выделяет память, выровненную по 64 байтам
 * @param size Размер памяти в байтах
 * @return Указатель на выделенную память
 */
inline void* malloc64(size_t size) {
    void* ptr = nullptr;
    if (posix_memalign(&ptr, 64, size) != 0) {
        return nullptr;
    }
    return ptr;
}

#endif // EXPERIMENTS_COMMON_HPP
