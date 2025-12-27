#include "common.hpp"

// ==================== ЭКСПЕРИМЕНТ: СРАВНЕНИЕ АЛГОРИТМОВ СОРТИРОВКИ ====================

// QuickSort (static helper)
static void quickSort(uint64_t* A, int64_t iLo, int64_t iHi) {
    int64_t Lo = iLo;
    int64_t Hi = iHi;
    uint64_t Mid = A[(Lo + Hi) >> 1];
    
    do {
        while (A[Lo] < Mid) Lo++;
        while (A[Hi] > Mid) Hi--;
        if (Lo <= Hi) {
            uint64_t T = A[Lo];
            A[Lo] = A[Hi];
            A[Hi] = T;
            Lo++;
            Hi--;
        }
    } while (Lo < Hi);
    
    if (Hi > iLo) quickSort(A, iLo, Hi);
    if (Lo < iHi) quickSort(A, Lo, iHi);
}

/**
 * @brief Эксперимент сравнения алгоритмов сортировки
 * 
 * Параметры:
 * - param1: 1..20 (М) - Количество 64-разрядных элементов
 * - param2: 4..1024 (К) - Шаг увеличения размера массива
 */
std::string sortingAlgorithmsExperiment(const std::string& params) {
    int param1 = SimpleJsonParser::getInt(params, "param1", 1);   // М элементов
    int param2 = SimpleJsonParser::getInt(params, "param2", 100); // К шаг
    
    if (param1 < 1) param1 = 1;
    if (param1 > 20) param1 = 20;
    if (param2 < 4) param2 = 4;
    if (param2 > 1024) param2 = 1024;
    
    size_t maxElements = static_cast<size_t>(param1) * 1024 * 1024;
    size_t stepElements = static_cast<size_t>(param2) * 1024;
    
    // Выделение памяти
    uint64_t* qmas = static_cast<uint64_t*>(malloc64(maxElements * sizeof(uint64_t)));
    uint8_t* rmas = static_cast<uint8_t*>(malloc64(maxElements * sizeof(uint64_t)));
    uint8_t* tmp = static_cast<uint8_t*>(malloc64(maxElements * sizeof(uint64_t)));
    uint8_t* rmas_opt = static_cast<uint8_t*>(malloc64(maxElements * sizeof(uint64_t)));
    uint8_t* tmp_opt = static_cast<uint8_t*>(malloc64(maxElements * sizeof(uint64_t)));
    
    if (!qmas || !rmas || !tmp || !rmas_opt || !tmp_opt) {
        if (qmas) free(qmas);
        if (rmas) free(rmas);
        if (tmp) free(tmp);
        if (rmas_opt) free(rmas_opt);
        if (tmp_opt) free(tmp_opt);
        return "{\"error\":\"Failed to allocate memory\"}";
    }
    
    // PMU счётчики
    PerfCounters perfCounters;
    PmuMetrics quickSortPmu, radixPmu, radixOptPmu;
    
    // Результаты: elements, quicksort, radix, radix_opt
    struct DataPoint {
        size_t elements;
        double quicksort_time_us;
        double radix_time_us;
        double radix_opt_time_us;
    };
    std::vector<DataPoint> results;
    
    setCancelExperiment(false);
    prepareForMeasurement();
    
    printf("\n[EXP6] ========== sorting_algorithms ==========\n");
    printf("[EXP6] Параметры: param1=%d М, param2=%d К\n", param1, param2);
    printf("[EXP6] maxElements=%zu, stepElements=%zu\n", maxElements, stepElements);
    printf("[EXP6] Всего шагов: %zu\n", maxElements / stepElements);
    fflush(stdout);
    
    for (size_t numElements = stepElements; numElements <= maxElements; numElements += stepElements) {
        if (isCancelled()) {
            free(qmas);
            free(rmas);
            free(tmp);
            free(rmas_opt);
            free(tmp_opt);
            return "{\"error\":\"Experiment cancelled\",\"cancelled\":true}";
        }
        
        // Генерация случайных данных
        for (size_t i = 0; i < numElements; i++) {
            uint64_t val = rand() ^ (static_cast<uint64_t>(rand()) << 32);
            qmas[i] = val;
            reinterpret_cast<uint64_t*>(rmas)[i] = val;
            reinterpret_cast<uint64_t*>(rmas_opt)[i] = val;
        }
        
        // ===== 1. QuickSort =====
        if (perfCounters.isAvailable()) perfCounters.start();
        auto start1 = std::chrono::high_resolution_clock::now();
        quickSort(qmas, 0, numElements - 1);
        auto end1 = std::chrono::high_resolution_clock::now();
        if (perfCounters.isAvailable()) {
            perfCounters.stop();
            quickSortPmu += perfCounters.read();
        }
        double quickSortTime = std::chrono::duration_cast<std::chrono::nanoseconds>(end1 - start1).count() / 1000.0;
        
        // ===== 2. Radix-Counting Sort (обычный) =====
        uint64_t* tmpA64 = reinterpret_cast<uint64_t*>(rmas);
        uint64_t* tmpB64 = reinterpret_cast<uint64_t*>(tmp);
        
        if (perfCounters.isAvailable()) perfCounters.start();
        auto start2 = std::chrono::high_resolution_clock::now();
        int c[256];
        for (int pass = 0; pass < 8; pass++) {
            // Заполнить массив нулями
            for (int j = 0; j < 256; j++) c[j] = 0;
            
            // Накопить количества повторений разрядов
            for (size_t j = 0; j < numElements; j++) {
                c[rmas[pass + j * 8]]++;
            }
            
            // Сохранить количество чисел, меньших данного
            for (int j = 1; j < 256; j++) {
                c[j] += c[j - 1];
            }
            
            // Перестановка
            for (int64_t j = numElements - 1; j >= 0; j--) {
                c[rmas[pass + j * 8]]--;
                tmpB64[c[rmas[pass + j * 8]]] = tmpA64[j];
            }
            
            // Подготовка к следующему проходу
            std::swap(rmas, tmp);
            tmpA64 = reinterpret_cast<uint64_t*>(rmas);
            tmpB64 = reinterpret_cast<uint64_t*>(tmp);
        }
        auto end2 = std::chrono::high_resolution_clock::now();
        if (perfCounters.isAvailable()) {
            perfCounters.stop();
            radixPmu += perfCounters.read();
        }
        double radixTime = std::chrono::duration_cast<std::chrono::nanoseconds>(end2 - start2).count() / 1000.0;
        
        // ===== 3. Radix-Counting Sort (оптимизированный под 8 процессоров) =====
        uint64_t* tmpA64_opt = reinterpret_cast<uint64_t*>(rmas_opt);
        uint64_t* tmpB64_opt = reinterpret_cast<uint64_t*>(tmp_opt);
        
        if (perfCounters.isAvailable()) perfCounters.start();
        auto start3 = std::chrono::high_resolution_clock::now();
        
        // Массив счётчиков для всех 8 проходов (256 * 8)
        int copt[256 * 8];
        
        // Подсчёт всех разрядов за один проход по массиву
        for (int i = 0; i < 256 * 8; i++) copt[i] = 0;
        
        size_t idx = 0;
        for (size_t k = 0; k < numElements; k++) {
            // Параллельное обращение к 8 частям copt
            copt[rmas_opt[idx + 0] * 8 + 0]++;
            copt[rmas_opt[idx + 1] * 8 + 1]++;
            copt[rmas_opt[idx + 2] * 8 + 2]++;
            copt[rmas_opt[idx + 3] * 8 + 3]++;
            copt[rmas_opt[idx + 4] * 8 + 4]++;
            copt[rmas_opt[idx + 5] * 8 + 5]++;
            copt[rmas_opt[idx + 6] * 8 + 6]++;
            copt[rmas_opt[idx + 7] * 8 + 7]++;
            idx += 8;
        }
        
        // Накопление сумм для всех 8 проходов
        for (int j = 1; j < 256; j++) {
            copt[j * 8 + 0] += copt[(j - 1) * 8 + 0];
            copt[j * 8 + 1] += copt[(j - 1) * 8 + 1];
            copt[j * 8 + 2] += copt[(j - 1) * 8 + 2];
            copt[j * 8 + 3] += copt[(j - 1) * 8 + 3];
            copt[j * 8 + 4] += copt[(j - 1) * 8 + 4];
            copt[j * 8 + 5] += copt[(j - 1) * 8 + 5];
            copt[j * 8 + 6] += copt[(j - 1) * 8 + 6];
            copt[j * 8 + 7] += copt[(j - 1) * 8 + 7];
        }
        
        // Сортировка по каждому разряду
        for (int pass = 0; pass < 8; pass++) {
            for (int64_t j = numElements - 1; j >= 0; j--) {
                int ii = rmas_opt[j * 8 + pass] * 8 + pass;
                int jj = --copt[ii];
                tmpB64_opt[jj] = tmpA64_opt[j];
            }
            
            // Подготовка к следующему проходу
            std::swap(rmas_opt, tmp_opt);
            tmpA64_opt = reinterpret_cast<uint64_t*>(rmas_opt);
            tmpB64_opt = reinterpret_cast<uint64_t*>(tmp_opt);
        }
        
        auto end3 = std::chrono::high_resolution_clock::now();
        if (perfCounters.isAvailable()) {
            perfCounters.stop();
            radixOptPmu += perfCounters.read();
        }
        double radixOptTime = std::chrono::duration_cast<std::chrono::nanoseconds>(end3 - start3).count() / 1000.0;
        
        results.push_back({numElements, quickSortTime, radixTime, radixOptTime});
    }
    
    free(qmas);
    free(rmas);
    free(tmp);
    free(rmas_opt);
    free(tmp_opt);
    
    // Расчёт выводов
    double totalQuickSort = 0, totalRadix = 0, totalRadixOpt = 0;
    for (size_t i = 0; i < results.size(); i++) {
        totalQuickSort += results[i].quicksort_time_us;
        totalRadix += results[i].radix_time_us;
        totalRadixOpt += results[i].radix_opt_time_us;
    }
    double quickToRadixRatio = (totalRadix > 0) ? totalQuickSort / totalRadix : 0;
    double quickToRadixOptRatio = (totalRadixOpt > 0) ? totalQuickSort / totalRadixOpt : 0;
    double radixToRadixOptRatio = (totalRadixOpt > 0) ? totalRadix / totalRadixOpt : 0;
    
    // Формирование JSON
    std::ostringstream json;
    json << "{";
    json << "\"experiment\":\"sorting_algorithms\",";
    json << "\"parameters\":{";
    json << "\"param1_m\":" << param1 << ",";
    json << "\"param2_k\":" << param2;
    json << "},";
    json << "\"conclusions\":{";
    json << "\"total_quicksort_us\":" << totalQuickSort << ",";
    json << "\"total_radix_us\":" << totalRadix << ",";
    json << "\"total_radix_opt_us\":" << totalRadixOpt << ",";
    json << "\"quicksort_to_radix_ratio\":" << quickToRadixRatio << ",";
    json << "\"quicksort_to_radix_opt_ratio\":" << quickToRadixOptRatio << ",";
    json << "\"radix_to_radix_opt_ratio\":" << radixToRadixOptRatio;
    json << "},";
    json << "\"dataPoints\":[";
    for (size_t i = 0; i < results.size(); i++) {
        if (i > 0) json << ",";
        json << "{\"elements\":" << results[i].elements 
             << ",\"quicksort_time_us\":" << results[i].quicksort_time_us
             << ",\"radix_time_us\":" << results[i].radix_time_us
             << ",\"radix_opt_time_us\":" << results[i].radix_opt_time_us << "}";
    }
    json << "],";
    // Итоговые PMU метрики
    json << "\"pmu_summary\":{";
    json << "\"quicksort\":" << quickSortPmu.toJson() << ",";
    json << "\"radix\":" << radixPmu.toJson() << ",";
    json << "\"radix_opt\":" << radixOptPmu.toJson();
    json << "}}";
    
    return json.str();
}

