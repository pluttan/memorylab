#ifndef TESTER_HPP
#define TESTER_HPP

#include <chrono>
#include <functional>
#include <vector>
#include <string>
#include <sstream>
#include <sys/resource.h>

/**
 * @class Tester
 * @brief Класс для тестирования производительности функций
 * 
 * Позволяет измерять время выполнения, использование памяти
 * и другие метрики производительности для заданных функций.
 * Возвращает результаты в формате JSON.
 */
class Tester {
public:
    /**
     * @struct TestResult
     * @brief Структура для хранения результатов тестирования
     */
    struct TestResult {
        std::string testName;           // Название теста
        double executionTimeMs;         // Время выполнения в миллисекундах
        double executionTimeUs;         // Время выполнения в микросекундах
        long memoryUsedKb;              // Использованная память в килобайтах
        size_t iterations;              // Количество итераций
        double avgTimePerIterationUs;   // Среднее время на итерацию в микросекундах
        bool success;                   // Успешность выполнения
        std::string errorMessage;       // Сообщение об ошибке (если есть)

        /**
         * @brief Преобразует результат в JSON строку
         * @return JSON строка с результатами
         */
        std::string toJson() const {
            std::ostringstream json;
            json << "{";
            json << "\"testName\":\"" << escapeJson(testName) << "\",";
            json << "\"executionTimeMs\":" << executionTimeMs << ",";
            json << "\"executionTimeUs\":" << executionTimeUs << ",";
            json << "\"memoryUsedKb\":" << memoryUsedKb << ",";
            json << "\"iterations\":" << iterations << ",";
            json << "\"avgTimePerIterationUs\":" << avgTimePerIterationUs << ",";
            json << "\"success\":" << (success ? "true" : "false");
            if (!success) {
                json << ",\"errorMessage\":\"" << escapeJson(errorMessage) << "\"";
            }
            json << "}";
            return json.str();
        }

    private:
        static std::string escapeJson(const std::string& str) {
            std::ostringstream escaped;
            for (char c : str) {
                switch (c) {
                    case '"': escaped << "\\\""; break;
                    case '\\': escaped << "\\\\"; break;
                    case '\b': escaped << "\\b"; break;
                    case '\f': escaped << "\\f"; break;
                    case '\n': escaped << "\\n"; break;
                    case '\r': escaped << "\\r"; break;
                    case '\t': escaped << "\\t"; break;
                    default: escaped << c; break;
                }
            }
            return escaped.str();
        }
    };

private:
    std::vector<TestResult> results;    // История результатов тестов

    /**
     * @brief Получает текущее использование памяти процессом
     * @return Использование памяти в килобайтах
     */
    long getCurrentMemoryUsage() {
        struct rusage usage;
        if (getrusage(RUSAGE_SELF, &usage) == 0) {
            return usage.ru_maxrss / 1024;
        }
        return -1;
    }

public:
    /**
     * @brief Конструктор класса Tester
     */
    Tester() {}

    /**
     * @brief Тестирует функцию без аргументов
     * @tparam Func Тип функции
     * @param name Название теста
     * @param func Функция для тестирования
     * @param iterations Количество итераций (по умолчанию 1)
     * @return Результат тестирования
     */
    template<typename Func>
    TestResult run(const std::string& name, Func func, size_t iterations = 1) {
        TestResult result;
        result.testName = name;
        result.iterations = iterations;
        result.success = true;
        result.executionTimeMs = 0;
        result.executionTimeUs = 0;
        result.avgTimePerIterationUs = 0;

        long memoryBefore = getCurrentMemoryUsage();

        try {
            auto start = std::chrono::high_resolution_clock::now();
            
            for (size_t i = 0; i < iterations; ++i) {
                func();
            }
            
            auto end = std::chrono::high_resolution_clock::now();
            
            auto durationUs = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
            auto durationMs = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
            
            result.executionTimeUs = static_cast<double>(durationUs);
            result.executionTimeMs = static_cast<double>(durationMs);
            result.avgTimePerIterationUs = result.executionTimeUs / iterations;
        } catch (const std::exception& e) {
            result.success = false;
            result.errorMessage = e.what();
        } catch (...) {
            result.success = false;
            result.errorMessage = "Unknown error";
        }

        long memoryAfter = getCurrentMemoryUsage();
        result.memoryUsedKb = memoryAfter - memoryBefore;

        results.push_back(result);

        return result;
    }

    /**
     * @brief Тестирует функцию с аргументами
     * @tparam Func Тип функции
     * @tparam Args Типы аргументов
     * @param name Название теста
     * @param func Функция для тестирования
     * @param iterations Количество итераций
     * @param args Аргументы функции
     * @return Результат тестирования
     */
    template<typename Func, typename... Args>
    TestResult runWithArgs(const std::string& name, Func func, size_t iterations, Args&&... args) {
        return run(name, [&]() {
            func(std::forward<Args>(args)...);
        }, iterations);
    }

    /**
     * @brief Сравнивает две функции и возвращает результат в JSON
     * @tparam Func1 Тип первой функции
     * @tparam Func2 Тип второй функции
     * @param name1 Название первого теста
     * @param func1 Первая функция
     * @param name2 Название второго теста
     * @param func2 Вторая функция
     * @param iterations Количество итераций
     * @return JSON строка с результатами сравнения
     */
    template<typename Func1, typename Func2>
    std::string compare(const std::string& name1, Func1 func1, 
                        const std::string& name2, Func2 func2, 
                        size_t iterations = 1000) {
        TestResult result1 = run(name1, func1, iterations);
        TestResult result2 = run(name2, func2, iterations);
        
        std::ostringstream json;
        json << "{";
        json << "\"comparison\":{";
        json << "\"test1\":" << result1.toJson() << ",";
        json << "\"test2\":" << result2.toJson() << ",";
        
        double speedup = 0;
        std::string faster = "";
        if (result1.avgTimePerIterationUs < result2.avgTimePerIterationUs && result1.avgTimePerIterationUs > 0) {
            speedup = result2.avgTimePerIterationUs / result1.avgTimePerIterationUs;
            faster = name1;
        } else if (result2.avgTimePerIterationUs < result1.avgTimePerIterationUs && result2.avgTimePerIterationUs > 0) {
            speedup = result1.avgTimePerIterationUs / result2.avgTimePerIterationUs;
            faster = name2;
        }
        
        json << "\"faster\":\"" << faster << "\",";
        json << "\"speedup\":" << speedup;
        json << "}}";
        
        return json.str();
    }

    /**
     * @brief Возвращает все результаты в формате JSON
     * @return JSON строка со всеми результатами
     */
    std::string getAllResultsJson() const {
        std::ostringstream json;
        json << "{\"results\":[";
        
        for (size_t i = 0; i < results.size(); ++i) {
            json << results[i].toJson();
            if (i < results.size() - 1) {
                json << ",";
            }
        }
        
        json << "]}";
        return json.str();
    }

    /**
     * @brief Очищает историю результатов
     */
    void clearResults() {
        results.clear();
    }

    /**
     * @brief Получает все результаты тестов
     * @return Вектор с результатами
     */
    const std::vector<TestResult>& getResults() const {
        return results;
    }
};

#endif // TESTER_HPP
