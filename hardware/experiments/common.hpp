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

#ifdef __APPLE__
#include <sys/sysctl.h>
#include <mach/thread_policy.h>
#include <mach/thread_act.h>
#endif

#ifdef __linux__
#include <unistd.h>
#include <sys/syscall.h>
#endif

#include "../tester.cpp"

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
