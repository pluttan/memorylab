#include "common.hpp"
#include <fstream>
#include <sstream>
#include <vector>

// ==================== ЭКСПЕРИМЕНТ 7: DOOM JIT BENCHMARK ====================
//
// Этот эксперимент читает данные бенчмарка из DOOM:
//   - jit_benchmark.csv содержит данные о производительности JIT vs Branching
//   - Данные собираются во время игры с автоматическим переключением режимов
//

/**
 * @brief Структура для хранения одной записи бенчмарка
 */
struct BenchmarkEntry {
    double timestamp_ms;
    std::string mode;  // "JIT" or "BRANCH"
    double frame_time_ms;
    uint64_t draw_calls;
};

/**
 * @brief Читает CSV файл с данными бенчмарка
 */
std::vector<BenchmarkEntry> readBenchmarkCSV(const std::string& filename) {
    std::vector<BenchmarkEntry> entries;
    std::ifstream file(filename);
    
    if (!file.is_open()) {
        return entries;
    }
    
    std::string line;
    // Skip header
    std::getline(file, line);
    
    while (std::getline(file, line)) {
        std::stringstream ss(line);
        std::string token;
        BenchmarkEntry entry;
        
        // timestamp_ms
        std::getline(ss, token, ',');
        entry.timestamp_ms = std::stod(token);
        
        // mode
        std::getline(ss, entry.mode, ',');
        
        // frame_time_ms
        std::getline(ss, token, ',');
        entry.frame_time_ms = std::stod(token);
        
        // draw_calls
        std::getline(ss, token, ',');
        entry.draw_calls = std::stoull(token);
        
        entries.push_back(entry);
    }
    
    return entries;
}

/**
 * @brief Основная функция эксперимента
 * 
 * Читает jit_benchmark.csv и возвращает данные в JSON формате
 */
std::string selfModifyingCodeExperiment(const std::string& params) {
    // Получаем путь к CSV файлу
    // По умолчанию ищем в корневой директории, так как DOOM запускается оттуда через make
    std::string csvPath = SimpleJsonParser::getString(params, "csv_path", 
        "jit_benchmark.csv");
    
    std::cout << "[DOOM JIT] Reading benchmark data from: " << csvPath << std::endl;
    
    // Читаем данные
    std::vector<BenchmarkEntry> entries = readBenchmarkCSV(csvPath);
    
    if (entries.empty()) {
        return R"({"error": "No data found in CSV file. Run DOOM first with JIT benchmark enabled."})";
    }
    
    std::cout << "[DOOM JIT] Read " << entries.size() << " entries" << std::endl;
    
    // Вычисляем статистику
    double jit_total_time = 0, branch_total_time = 0;
    uint64_t jit_total_calls = 0, branch_total_calls = 0;
    int jit_frames = 0, branch_frames = 0;
    
    for (const auto& entry : entries) {
        if (entry.mode == "JIT") {
            jit_total_time += entry.frame_time_ms;
            jit_total_calls += entry.draw_calls;
            jit_frames++;
        } else {
            branch_total_time += entry.frame_time_ms;
            branch_total_calls += entry.draw_calls;
            branch_frames++;
        }
    }
    
    double jit_avg_time = jit_frames > 0 ? jit_total_time / jit_frames : 0;
    double branch_avg_time = branch_frames > 0 ? branch_total_time / branch_frames : 0;
    double speedup = jit_avg_time > 0 ? branch_avg_time / jit_avg_time : 0;
    
    std::cout << "[DOOM JIT] JIT frames: " << jit_frames << ", avg: " << jit_avg_time << " ms" << std::endl;
    std::cout << "[DOOM JIT] Branch frames: " << branch_frames << ", avg: " << branch_avg_time << " ms" << std::endl;
    std::cout << "[DOOM JIT] Speedup: " << speedup << "x" << std::endl;
    
    // Формируем JSON ответ
    std::ostringstream json;
    json << "{\n";
    json << "  \"experiment\": \"DOOM JIT Benchmark\",\n";
    json << "  \"total_entries\": " << entries.size() << ",\n";
    json << "  \"jit\": {\n";
    json << "    \"frames\": " << jit_frames << ",\n";
    json << "    \"total_time_ms\": " << jit_total_time << ",\n";
    json << "    \"avg_frame_time_ms\": " << jit_avg_time << ",\n";
    json << "    \"total_draw_calls\": " << jit_total_calls << "\n";
    json << "  },\n";
    json << "  \"branching\": {\n";
    json << "    \"frames\": " << branch_frames << ",\n";
    json << "    \"total_time_ms\": " << branch_total_time << ",\n";
    json << "    \"avg_frame_time_ms\": " << branch_avg_time << ",\n";
    json << "    \"total_draw_calls\": " << branch_total_calls << "\n";
    json << "  },\n";
    json << "  \"speedup\": " << speedup << ",\n";
    
    // Добавляем сырые данные (первые 100 записей для каждого режима)
    json << "  \"raw_data\": {\n";
    json << "    \"jit_frames\": [";
    int count = 0;
    for (const auto& entry : entries) {
        if (entry.mode == "JIT") {
            if (count > 0) json << ",";
            json << entry.frame_time_ms;
            count++;
        }
    }
    json << "],\n";
    
    json << "    \"branch_frames\": [";
    count = 0;
    for (const auto& entry : entries) {
        if (entry.mode == "BRANCH") {
            if (count > 0) json << ",";
            json << entry.frame_time_ms;
            count++;
        }
    }
    json << "]\n";
    json << "  }\n";
    json << "}";
    
    return json.str();
}
