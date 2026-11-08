#include "common.hpp"
#include <unistd.h>

#ifdef MEMLAB_X86
#include <cpuid.h>
#endif

// ==================== ИДЕНТИФИКАЦИЯ ПРОЦЕССОРА ====================
//
// Аналог вкладки «Идентификация процессора» программы PCLAB (задания 1–2 методички).
// Даёт исходные данные для экспериментов: размер линейки кэша, объём физической
// памяти, объём и ассоциативность кэш-памяти всех уровней, размер и ассоциативность TLB.
//
// Источники данных:
// - x86: команда CPUID (как в PCLAB), работает в любой ОС и в WSL;
// - Linux без CPUID (ARM): /sys/devices/system/cpu/cpu0/cache;
// - macOS на Apple Silicon: sysctl hw.* (ассоциативность и TLB система не сообщает).

/**
 * @brief Описание одного кэша
 */
struct CacheDescriptor {
    int level = 0;
    std::string type;          // Data, Instruction, Unified
    size_t sizeBytes = 0;
    size_t lineBytes = 0;
    size_t ways = 0;           // 0 — неизвестно
    size_t sets = 0;           // 0 — неизвестно
    bool fullyAssociative = false;
};

/**
 * @brief Описание одного буфера TLB
 */
struct TlbDescriptor {
    int level = 0;
    std::string type;          // Data, Instruction, Unified, Load, Store
    std::string pages;         // Размеры страниц, например "4K" или "2M/4M"
    size_t entries = 0;
    size_t ways = 0;
    bool fullyAssociative = false;
};

/**
 * @brief Сведения о процессоре
 */
struct CpuIdentity {
    std::string vendor;
    std::string brand;
    int family = 0;
    int model = 0;
    int stepping = 0;
};

/**
 * @brief Объём физической памяти в байтах (0, если определить не удалось)
 */
inline size_t getPhysicalMemoryBytes() {
#if defined(__APPLE__)
    uint64_t memory = 0;
    size_t length = sizeof(memory);
    if (sysctlbyname("hw.memsize", &memory, &length, NULL, 0) == 0) {
        return static_cast<size_t>(memory);
    }
    return 0;
#else
    long pages = sysconf(_SC_PHYS_PAGES);
    long pageSize = sysconf(_SC_PAGESIZE);
    if (pages > 0 && pageSize > 0) {
        return static_cast<size_t>(pages) * static_cast<size_t>(pageSize);
    }
    return 0;
#endif
}

/**
 * @brief Название архитектуры, под которую собран сервер
 */
inline const char* buildArchitecture() {
#if defined(__x86_64__)
    return "x86_64";
#elif defined(__i386__)
    return "x86";
#elif defined(__aarch64__) || defined(__arm64__)
    return "arm64";
#elif defined(__arm__)
    return "arm";
#elif defined(__riscv)
    return "riscv";
#else
    return "unknown";
#endif
}

// ==================== x86: CPUID ====================

#ifdef MEMLAB_X86

struct CpuidRegisters {
    uint32_t eax = 0, ebx = 0, ecx = 0, edx = 0;
};

inline CpuidRegisters runCpuid(uint32_t leaf, uint32_t subleaf = 0) {
    CpuidRegisters r;
    __cpuid_count(leaf, subleaf, r.eax, r.ebx, r.ecx, r.edx);
    return r;
}

inline uint32_t maxBasicLeaf() { return runCpuid(0).eax; }
inline uint32_t maxExtendedLeaf() { return runCpuid(0x80000000u).eax; }

inline std::string trimSpaces(const std::string& text) {
    size_t first = text.find_first_not_of(' ');
    if (first == std::string::npos) return "";
    size_t last = text.find_last_not_of(' ');
    return text.substr(first, last - first + 1);
}

inline CpuIdentity x86Identity() {
    CpuIdentity id;
    CpuidRegisters r0 = runCpuid(0);
    char vendor[13] = {0};
    memcpy(vendor + 0, &r0.ebx, 4);
    memcpy(vendor + 4, &r0.edx, 4);
    memcpy(vendor + 8, &r0.ecx, 4);
    id.vendor = vendor;

    if (r0.eax >= 1) {
        uint32_t signature = runCpuid(1).eax;
        int stepping = signature & 0xF;
        int model = (signature >> 4) & 0xF;
        int family = (signature >> 8) & 0xF;
        int extModel = (signature >> 16) & 0xF;
        int extFamily = (signature >> 20) & 0xFF;
        id.stepping = stepping;
        id.family = family == 0xF ? family + extFamily : family;
        id.model = (family == 0x6 || family == 0xF) ? (extModel << 4) + model : model;
    }

    if (maxExtendedLeaf() >= 0x80000004u) {
        char brand[49] = {0};
        for (uint32_t i = 0; i < 3; i++) {
            CpuidRegisters r = runCpuid(0x80000002u + i);
            memcpy(brand + i * 16 + 0, &r.eax, 4);
            memcpy(brand + i * 16 + 4, &r.ebx, 4);
            memcpy(brand + i * 16 + 8, &r.ecx, 4);
            memcpy(brand + i * 16 + 12, &r.edx, 4);
        }
        id.brand = trimSpaces(brand);
    }
    return id;
}

inline bool isAmdLike(const std::string& vendor) {
    return vendor == "AuthenticAMD" || vendor == "HygonGenuine";
}

/**
 * @brief Кэши по CPUID: лист 4 (Intel) или 0x8000001D (AMD, Hygon)
 * У обоих листов одинаковый формат регистров
 */
inline std::vector<CacheDescriptor> x86Caches(const std::string& vendor) {
    uint32_t leaf = 0;
    if (isAmdLike(vendor)) {
        bool topologyExtensions = maxExtendedLeaf() >= 0x8000001Du &&
                                  (runCpuid(0x80000001u).ecx & (1u << 22));
        if (topologyExtensions) leaf = 0x8000001Du;
    } else if (maxBasicLeaf() >= 4) {
        leaf = 4;
    }

    std::vector<CacheDescriptor> caches;
    if (leaf == 0) return caches;
    for (uint32_t sub = 0; sub < 16; sub++) {
        CpuidRegisters r = runCpuid(leaf, sub);
        uint32_t type = r.eax & 0x1F;
        if (type == 0) break;
        CacheDescriptor c;
        c.level = (r.eax >> 5) & 0x7;
        c.type = type == 1 ? "Data" : type == 2 ? "Instruction" : "Unified";
        c.fullyAssociative = (r.eax >> 9) & 1;
        c.lineBytes = (r.ebx & 0xFFF) + 1;
        size_t partitions = ((r.ebx >> 12) & 0x3FF) + 1;
        c.ways = ((r.ebx >> 22) & 0x3FF) + 1;
        c.sets = static_cast<size_t>(r.ecx) + 1;
        c.sizeBytes = c.ways * partitions * c.lineBytes * c.sets;
        caches.push_back(c);
    }
    return caches;
}

/**
 * @brief Дескриптор TLB из листа 2 CPUID (таблица дескрипторов Intel SDM)
 */
struct Leaf2Tlb {
    uint8_t code;
    int level;
    const char* type;
    const char* pages;
    uint16_t entries;
    uint16_t ways;             // 0 — полностью ассоциативный, LEAF2_UNKNOWN_WAYS — не указано
};

constexpr uint16_t LEAF2_UNKNOWN_WAYS = 0xFFFF;

static const Leaf2Tlb LEAF2_TLBS[] = {
    {0x01, 1, "Instruction", "4K", 32, 4},
    {0x02, 1, "Instruction", "4M", 2, 0},
    {0x03, 1, "Data", "4K", 64, 4},
    {0x04, 1, "Data", "4M", 8, 4},
    {0x05, 1, "Data", "4M", 32, 4},
    {0x0B, 1, "Instruction", "4M", 4, 4},
    {0x4F, 1, "Instruction", "4K", 32, LEAF2_UNKNOWN_WAYS},
    {0x50, 1, "Instruction", "4K/2M/4M", 64, LEAF2_UNKNOWN_WAYS},
    {0x51, 1, "Instruction", "4K/2M/4M", 128, LEAF2_UNKNOWN_WAYS},
    {0x52, 1, "Instruction", "4K/2M/4M", 256, LEAF2_UNKNOWN_WAYS},
    {0x55, 1, "Instruction", "2M/4M", 7, 0},
    {0x56, 1, "Data", "4M", 16, 4},
    {0x57, 1, "Data", "4K", 16, 4},
    {0x59, 1, "Data", "4K", 16, 0},
    {0x5A, 1, "Data", "2M/4M", 32, 4},
    {0x5B, 1, "Data", "4K/4M", 64, LEAF2_UNKNOWN_WAYS},
    {0x5C, 1, "Data", "4K/4M", 128, LEAF2_UNKNOWN_WAYS},
    {0x5D, 1, "Data", "4K/4M", 256, LEAF2_UNKNOWN_WAYS},
    {0x61, 1, "Instruction", "4K", 48, 0},
    {0x63, 1, "Data", "2M/4M", 32, 4},
    {0x63, 1, "Data", "1G", 4, 4},
    {0x64, 1, "Data", "4K", 512, 4},
    {0x6A, 1, "Data", "4K", 64, 8},
    {0x6B, 1, "Data", "4K", 256, 8},
    {0x6C, 1, "Data", "2M/4M", 128, 8},
    {0x6D, 1, "Data", "1G", 16, 0},
    {0x76, 1, "Instruction", "2M/4M", 8, 0},
    {0xA0, 1, "Data", "4K", 32, 0},
    {0xB0, 1, "Instruction", "4K", 128, 4},
    {0xB1, 1, "Instruction", "2M", 8, 4},
    {0xB2, 1, "Instruction", "4K", 64, 4},
    {0xB3, 1, "Data", "4K", 128, 4},
    {0xB4, 1, "Data", "4K", 256, 4},
    {0xB5, 1, "Instruction", "4K", 64, 8},
    {0xB6, 1, "Instruction", "4K", 128, 8},
    {0xBA, 1, "Data", "4K", 64, 4},
    {0xC0, 1, "Data", "4K/4M", 8, 4},
    {0xC1, 2, "Unified", "4K/2M", 1024, 8},
    {0xC2, 1, "Data", "4K/2M", 16, 4},
    {0xC3, 2, "Unified", "4K/2M", 1536, 6},
    {0xC3, 2, "Unified", "1G", 16, 4},
    {0xC4, 1, "Data", "2M/4M", 32, 4},
    {0xCA, 2, "Unified", "4K", 512, 4},
};

/**
 * @brief TLB по дескрипторам листа 2 (Intel до Ice Lake, где нет листа 0x18)
 * Младший байт EAX — счётчик повторов, а не дескриптор.
 * Регистр с установленным битом 31 дескрипторов не содержит
 */
inline std::vector<TlbDescriptor> intelLeaf2Tlbs() {
    std::vector<TlbDescriptor> tlbs;
    if (maxBasicLeaf() < 2) return tlbs;
    CpuidRegisters r = runCpuid(2);
    const uint32_t registers[4] = {r.eax, r.ebx, r.ecx, r.edx};
    for (int k = 0; k < 4; k++) {
        if (registers[k] & 0x80000000u) continue;
        for (int byte = (k == 0 ? 1 : 0); byte < 4; byte++) {
            uint8_t code = (registers[k] >> (8 * byte)) & 0xFF;
            for (const Leaf2Tlb& e : LEAF2_TLBS) {
                if (e.code != code) continue;
                TlbDescriptor t;
                t.level = e.level;
                t.type = e.type;
                t.pages = e.pages;
                t.entries = e.entries;
                t.fullyAssociative = e.ways == 0;
                t.ways = e.ways == 0 ? e.entries : (e.ways == LEAF2_UNKNOWN_WAYS ? 0 : e.ways);
                tlbs.push_back(t);
            }
        }
    }
    std::stable_sort(tlbs.begin(), tlbs.end(),
                     [](const TlbDescriptor& a, const TlbDescriptor& b) { return a.level < b.level; });
    return tlbs;
}

/**
 * @brief TLB по CPUID листа 0x18 (Intel, начиная с Ice Lake), иначе по листу 2
 */
inline std::vector<TlbDescriptor> intelTlbs() {
    std::vector<TlbDescriptor> tlbs;
    if (maxBasicLeaf() < 0x18) return intelLeaf2Tlbs();
    static const char* types[] = {"", "Data", "Instruction", "Unified", "Load", "Store"};
    static const char* pageNames[] = {"4K", "2M", "4M", "1G"};
    uint32_t maxSub = runCpuid(0x18, 0).eax;
    for (uint32_t sub = 0; sub <= maxSub && sub < 64; sub++) {
        CpuidRegisters r = runCpuid(0x18, sub);
        uint32_t type = r.edx & 0x1F;
        if (type == 0) continue;
        TlbDescriptor t;
        t.type = type <= 5 ? types[type] : "Unknown";
        t.level = (r.edx >> 5) & 0x7;
        t.fullyAssociative = (r.edx >> 8) & 1;
        t.ways = r.ebx >> 16;
        t.entries = t.ways * r.ecx;
        for (int bit = 0; bit < 4; bit++) {
            if (r.ebx & (1u << bit)) t.pages += (t.pages.empty() ? "" : "/") + std::string(pageNames[bit]);
        }
        tlbs.push_back(t);
    }
    return tlbs;
}

/**
 * @brief Ассоциативность L2 TLB в кодировке листа 0x80000006 (AMD)
 * @return Количество путей; SIZE_MAX — полностью ассоциативный
 */
inline size_t decodeAmdL2Ways(uint32_t code) {
    static const size_t ways[16] = {0, 1, 2, 3, 4, 6, 8, 0, 16, 0, 32, 48, 64, 96, 128, SIZE_MAX};
    return ways[code & 0xF];
}

inline size_t decodeAmdL1Ways(uint32_t code) {
    return code == 0xFF ? SIZE_MAX : static_cast<size_t>(code);
}

inline void addAmdTlb(std::vector<TlbDescriptor>& tlbs, int level, const char* type,
                      const char* pages, size_t entries, size_t ways) {
    if (entries == 0) return;
    TlbDescriptor t;
    t.level = level;
    t.type = type;
    t.pages = pages;
    t.entries = entries;
    t.fullyAssociative = ways == SIZE_MAX || ways == entries;
    t.ways = t.fullyAssociative ? entries : ways;
    tlbs.push_back(t);
}

/**
 * @brief TLB по CPUID листов 0x80000005 и 0x80000006 (AMD, Hygon)
 */
inline std::vector<TlbDescriptor> amdTlbs() {
    std::vector<TlbDescriptor> tlbs;
    uint32_t maxExt = maxExtendedLeaf();
    if (maxExt >= 0x80000005u) {
        // L1: по 8 бит на ассоциативность и количество записей
        CpuidRegisters r = runCpuid(0x80000005u);
        addAmdTlb(tlbs, 1, "Data", "4K", (r.ebx >> 16) & 0xFF, decodeAmdL1Ways(r.ebx >> 24));
        addAmdTlb(tlbs, 1, "Instruction", "4K", r.ebx & 0xFF, decodeAmdL1Ways((r.ebx >> 8) & 0xFF));
        addAmdTlb(tlbs, 1, "Data", "2M/4M", (r.eax >> 16) & 0xFF, decodeAmdL1Ways(r.eax >> 24));
        addAmdTlb(tlbs, 1, "Instruction", "2M/4M", r.eax & 0xFF, decodeAmdL1Ways((r.eax >> 8) & 0xFF));
    }
    if (maxExt >= 0x80000006u) {
        // L2: 4 бита кода ассоциативности и 12 бит количества записей
        CpuidRegisters r = runCpuid(0x80000006u);
        addAmdTlb(tlbs, 2, "Data", "4K", (r.ebx >> 16) & 0xFFF, decodeAmdL2Ways(r.ebx >> 28));
        addAmdTlb(tlbs, 2, "Instruction", "4K", r.ebx & 0xFFF, decodeAmdL2Ways((r.ebx >> 12) & 0xF));
        addAmdTlb(tlbs, 2, "Data", "2M/4M", (r.eax >> 16) & 0xFFF, decodeAmdL2Ways(r.eax >> 28));
        addAmdTlb(tlbs, 2, "Instruction", "2M/4M", r.eax & 0xFFF, decodeAmdL2Ways((r.eax >> 12) & 0xF));
    }
    return tlbs;
}

/**
 * @brief Таблица «лист — регистры», как во вкладке PCLAB
 */
inline std::string rawCpuidJson() {
    std::ostringstream json;
    json << "[";
    bool first = true;
    auto add = [&](uint32_t leaf) {
        CpuidRegisters r = runCpuid(leaf, 0);
        char line[160];
        snprintf(line, sizeof(line),
                 "{\"leaf\":\"%08x\",\"eax\":\"%08x\",\"ebx\":\"%08x\",\"ecx\":\"%08x\",\"edx\":\"%08x\"}",
                 leaf, r.eax, r.ebx, r.ecx, r.edx);
        json << (first ? "" : ",") << line;
        first = false;
    };
    uint32_t maxBasic = std::min<uint32_t>(maxBasicLeaf(), 0x20);
    for (uint32_t leaf = 0; leaf <= maxBasic; leaf++) add(leaf);
    uint32_t maxExt = maxExtendedLeaf();
    if (maxExt >= 0x80000000u) {
        maxExt = std::min<uint32_t>(maxExt, 0x80000020u);
        for (uint32_t leaf = 0x80000000u; leaf <= maxExt; leaf++) add(leaf);
    }
    json << "]";
    return json.str();
}

#endif // MEMLAB_X86

// ==================== Linux: sysfs ====================

#ifdef __linux__
inline std::vector<CacheDescriptor> sysfsCaches() {
    std::vector<CacheDescriptor> caches;
    for (int index = 0; index < 16; index++) {
        size_t level = readSysfsCacheValue(index, "level");
        if (level == 0) break;
        std::string path = "/sys/devices/system/cpu/cpu0/cache/index" + std::to_string(index) + "/type";
        char type[32] = {0};
        FILE* f = fopen(path.c_str(), "r");
        if (f) {
            if (fscanf(f, "%31s", type) != 1) type[0] = 0;
            fclose(f);
        }
        CacheDescriptor c;
        c.level = static_cast<int>(level);
        c.type = type[0] ? type : "Unified";
        c.sizeBytes = readSysfsCacheValue(index, "size");
        c.lineBytes = readSysfsCacheValue(index, "coherency_line_size");
        c.ways = readSysfsCacheValue(index, "ways_of_associativity");
        c.sets = readSysfsCacheValue(index, "number_of_sets");
        caches.push_back(c);
    }
    return caches;
}

inline std::string procCpuinfoBrand() {
    FILE* f = fopen("/proc/cpuinfo", "r");
    if (!f) return "";
    char line[512];
    std::string brand;
    while (fgets(line, sizeof(line), f)) {
        if (strncmp(line, "model name", 10) != 0 && strncmp(line, "Hardware", 8) != 0) continue;
        const char* colon = strchr(line, ':');
        if (!colon) continue;
        brand = colon + 1;
        brand.erase(0, brand.find_first_not_of(" \t"));
        brand.erase(brand.find_last_not_of(" \t\n") + 1);
        break;
    }
    fclose(f);
    return brand;
}
#endif

// ==================== macOS: sysctl ====================

#ifdef __APPLE__
inline size_t sysctlSize(const char* name) {
    uint64_t value = 0;
    size_t length = sizeof(value);
    if (sysctlbyname(name, &value, &length, NULL, 0) != 0) return 0;
    if (length == sizeof(uint32_t)) return static_cast<size_t>(static_cast<uint32_t>(value));
    return static_cast<size_t>(value);
}

inline std::string sysctlString(const char* name) {
    char buffer[256] = {0};
    size_t length = sizeof(buffer) - 1;
    if (sysctlbyname(name, buffer, &length, NULL, 0) != 0) return "";
    return buffer;
}

/**
 * @brief Кэши по sysctl. На Apple Silicon берутся производительные ядра (perflevel0)
 */
inline std::vector<CacheDescriptor> sysctlCaches() {
    std::vector<CacheDescriptor> caches;
    size_t line = sysctlSize("hw.cachelinesize");
    struct Entry { int level; const char* type; const char* perf; const char* plain; };
    const Entry entries[] = {
        {1, "Data", "hw.perflevel0.l1dcachesize", "hw.l1dcachesize"},
        {1, "Instruction", "hw.perflevel0.l1icachesize", "hw.l1icachesize"},
        {2, "Unified", "hw.perflevel0.l2cachesize", "hw.l2cachesize"},
        {3, "Unified", "hw.perflevel0.l3cachesize", "hw.l3cachesize"},
    };
    for (const Entry& e : entries) {
        size_t size = sysctlSize(e.perf);
        if (size == 0) size = sysctlSize(e.plain);
        if (size == 0) continue;
        CacheDescriptor c;
        c.level = e.level;
        c.type = e.type;
        c.sizeBytes = size;
        c.lineBytes = line;
        caches.push_back(c);
    }
    return caches;
}
#endif

// ==================== Общий интерфейс ====================

inline CpuIdentity collectIdentity() {
#ifdef MEMLAB_X86
    return x86Identity();
#else
    CpuIdentity id;
#if defined(__APPLE__)
    id.vendor = "Apple";
    id.brand = sysctlString("machdep.cpu.brand_string");
#elif defined(__linux__)
    id.vendor = "ARM";
    id.brand = procCpuinfoBrand();
#endif
    return id;
#endif
}

/**
 * @brief Все кэши процессора, отсортированные по уровню
 */
inline std::vector<CacheDescriptor> collectCaches() {
    std::vector<CacheDescriptor> caches;
#ifdef MEMLAB_X86
    caches = x86Caches(x86Identity().vendor);
#endif
#ifdef __linux__
    if (caches.empty()) caches = sysfsCaches();
#endif
#ifdef __APPLE__
    if (caches.empty()) caches = sysctlCaches();
#endif
    std::stable_sort(caches.begin(), caches.end(),
                     [](const CacheDescriptor& a, const CacheDescriptor& b) { return a.level < b.level; });
    return caches;
}

/**
 * @brief Объём кэша данных (или объединённого кэша) заданного уровня; 0 — такого кэша нет
 */
inline size_t dataCacheSize(const std::vector<CacheDescriptor>& caches, int level) {
    for (const CacheDescriptor& c : caches) {
        if (c.level == level && c.type != "Instruction") return c.sizeBytes;
    }
    return 0;
}

inline std::vector<TlbDescriptor> collectTlbs() {
#ifdef MEMLAB_X86
    if (isAmdLike(x86Identity().vendor)) return amdTlbs();
    return intelTlbs();
#else
    return {};
#endif
}

inline void writeCachesJson(std::ostringstream& json, const std::vector<CacheDescriptor>& caches) {
    json << "[";
    for (size_t i = 0; i < caches.size(); i++) {
        const CacheDescriptor& c = caches[i];
        // Банк кэша в терминах методички: один путь набора, т.е. объём / ассоциативность
        size_t bank = (c.ways > 0 && !c.fullyAssociative) ? c.sizeBytes / c.ways : 0;
        json << (i ? "," : "") << "{\"level\":" << c.level
             << ",\"type\":\"" << c.type << "\""
             << ",\"size_bytes\":" << c.sizeBytes
             << ",\"line_bytes\":" << c.lineBytes
             << ",\"ways\":" << c.ways
             << ",\"sets\":" << c.sets
             << ",\"fully_associative\":" << (c.fullyAssociative ? "true" : "false")
             << ",\"bank_bytes\":" << bank << "}";
    }
    json << "]";
}

inline void writeTlbsJson(std::ostringstream& json, const std::vector<TlbDescriptor>& tlbs) {
    json << "[";
    for (size_t i = 0; i < tlbs.size(); i++) {
        const TlbDescriptor& t = tlbs[i];
        json << (i ? "," : "") << "{\"level\":" << t.level
             << ",\"type\":\"" << t.type << "\""
             << ",\"pages\":\"" << t.pages << "\""
             << ",\"entries\":" << t.entries
             << ",\"ways\":" << t.ways
             << ",\"fully_associative\":" << (t.fullyAssociative ? "true" : "false") << "}";
    }
    json << "]";
}

/**
 * @brief Идентификация процессора: параметры процессора, кэша, TLB и памяти в JSON
 */
std::string cpuInfoExperiment(const std::string& params) {
    (void)params;
    CpuIdentity id = collectIdentity();
    long pageSize = sysconf(_SC_PAGESIZE);

    std::ostringstream json;
    json << "{\"experiment\":\"cpu_info\",";
    json << "\"cpu\":{";
    json << "\"vendor\":\"" << jsonEscape(id.vendor) << "\",";
    json << "\"brand\":\"" << jsonEscape(id.brand) << "\",";
    json << "\"arch\":\"" << buildArchitecture() << "\",";
    json << "\"logical_cpus\":" << std::thread::hardware_concurrency() << ",";
    json << "\"family\":" << id.family << ",";
    json << "\"model\":" << id.model << ",";
    json << "\"stepping\":" << id.stepping;
    json << "},";
    json << "\"memory\":{";
    json << "\"physical_bytes\":" << getPhysicalMemoryBytes() << ",";
    json << "\"page_size_bytes\":" << (pageSize > 0 ? pageSize : 4096) << ",";
    json << "\"cache_line_bytes\":" << getCacheLineSize();
    json << "},";
    json << "\"caches\":";
    writeCachesJson(json, collectCaches());
    json << ",\"tlb\":";
    writeTlbsJson(json, collectTlbs());
#ifdef MEMLAB_X86
    json << ",\"raw_cpuid\":" << rawCpuidJson();
#else
    json << ",\"raw_cpuid\":[]";
#endif
    json << "}";
    return json.str();
}
