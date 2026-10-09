#include "scrp/Vfs.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>

namespace scrp {
namespace fs = std::filesystem;

namespace {

// ---------------------------------------------------------- формат ---
// Раскладка пака `.scrap` (версия 1, всё little-endian):
//
//   [0..3]    "SCRP"
//   [4..7]    версия
//   [8..15]   смещение таблицы
//   [16..19]  количество записей
//   [20..]    блобы подряд
//   [таблица] на каждую запись:
//               uint16 длина пути
//               байты  путь (UTF-8, разделитель '/')
//               uint64 смещение блоба
//               uint32 размер
//               uint32 crc32
//
// Таблица в конце, чтобы упаковщик мог писать блобы потоком и не держать
// весь пак в памяти. Сжатия в версии 1 нет намеренно: формат должен быть
// тривиально правильным, сжатие добавится за тем же API.
constexpr char kMagic[4] = { 'S', 'C', 'R', 'P' };
constexpr uint32_t kVersion = 1;
constexpr size_t kHeaderSize = 20;

struct PackEntry {
    uint64_t offset = 0;
    uint32_t size = 0;
    uint32_t crc = 0;
};

struct Mount {
    enum class Kind { Dir, Pack };
    Kind kind = Kind::Dir;
    std::string path;                          // корень папки или путь к паку
    std::map<std::string, PackEntry> entries;  // только для Pack
};

// Монтирования в порядке добавления; поиск идёт с конца — позднее важнее
std::vector<Mount> g_mounts;
int g_misses = 0;

// ---------------------------------------------------------- утилиты ---

uint32_t crc32(const uint8_t* data, size_t len) {
    static uint32_t table[256];
    static bool ready = false;
    if (!ready) {
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t c = i;
            for (int k = 0; k < 8; ++k) c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
            table[i] = c;
        }
        ready = true;
    }
    uint32_t c = 0xFFFFFFFFu;
    for (size_t i = 0; i < len; ++i) c = table[(c ^ data[i]) & 0xFF] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

// Приводим к единому виду: прямые слэши, без "./" и ведущего слэша
std::string normalize(const std::string& path) {
    std::string out;
    out.reserve(path.size());
    for (char c : path) out += (c == '\\') ? '/' : c;

    while (out.compare(0, 2, "./") == 0) out.erase(0, 2);
    // Logical resource paths must never escape their mount.
    if (out.empty()) return out;
    if (out.front() == '/' || out.find(':') != std::string::npos ||
        out.find('\0') != std::string::npos) return {};
    fs::path logical = fs::u8path(out);
    for (const auto& part : logical) if (part == "..") return {};
    out = logical.lexically_normal().generic_u8string();
    return out;
}

bool readWholeFile(const fs::path& p, std::vector<uint8_t>& out) {
    std::ifstream f(p, std::ios::binary | std::ios::ate);
    if (!f) return false;
    std::streamoff size = f.tellg();
    if (size < 0 || size > 64 * 1024 * 1024) return false;
    out.resize(static_cast<size_t>(size));
    f.seekg(0);
    if (size > 0) f.read(reinterpret_cast<char*>(out.data()), size);
    return static_cast<bool>(f);
}

template <typename T>
T readLE(const uint8_t* p) {
    T value = 0;
    for (size_t i = 0; i < sizeof(T); ++i) value |= static_cast<T>(p[i]) << (8 * i);
    return value;
}

bool mountHas(const Mount& m, const std::string& path) {
    if (m.kind == Mount::Kind::Pack) return m.entries.count(path) > 0;
    std::error_code ec;
    return !path.empty() && fs::is_regular_file(fs::u8path(m.path) / fs::u8path(path), ec);
}

bool readFrom(const Mount& m, const std::string& path, std::vector<uint8_t>& out) {
    if (m.kind == Mount::Kind::Dir) {
        if (path.empty()) return false;
        return readWholeFile(fs::u8path(m.path) / fs::u8path(path), out);
    }

    auto it = m.entries.find(path);
    if (it == m.entries.end()) return false;

    std::ifstream f(fs::u8path(m.path), std::ios::binary);
    if (!f) return false;
    f.seekg(static_cast<std::streamoff>(it->second.offset));
    out.resize(it->second.size);
    if (it->second.size > 0) {
        f.read(reinterpret_cast<char*>(out.data()), it->second.size);
        if (!f) return false;
    }

    // Битый пак лучше заметить сразу, но не падать: данные всё равно отдаём,
    // а в лог уходит предупреждение
    if (crc32(out.data(), out.size()) != it->second.crc) {
        out.clear();
        return false;
    }
    return true;
}

} // namespace

namespace Vfs {

bool mountDir(const std::string& path) {
    std::error_code ec;
    if (!fs::is_directory(fs::u8path(path), ec)) return false;

    Mount m;
    m.kind = Mount::Kind::Dir;
    m.path = path;
    g_mounts.push_back(std::move(m));
    std::printf("[vfs] смонтирована папка '%s'\n", path.c_str());
    return true;
}

bool mountPack(const std::string& path) {
    std::vector<uint8_t> head;
    std::ifstream f(fs::u8path(path), std::ios::binary | std::ios::ate);
    if (!f) return false;
    const auto fileSize = f.tellg();
    if (fileSize < static_cast<std::streamoff>(kHeaderSize)) return false;
    f.seekg(0);

    uint8_t header[kHeaderSize];
    f.read(reinterpret_cast<char*>(header), kHeaderSize);
    if (!f || std::memcmp(header, kMagic, 4) != 0) {
        std::printf("[vfs] '%s': не пак scrap\n", path.c_str());
        return false;
    }

    uint32_t version = readLE<uint32_t>(header + 4);
    if (version != kVersion) {
        std::printf("[vfs] '%s': версия пака %u, поддерживается %u\n",
                    path.c_str(), version, kVersion);
        return false;
    }

    uint64_t tableOffset = readLE<uint64_t>(header + 8);
    uint32_t entryCount = readLE<uint32_t>(header + 16);
    if (tableOffset < kHeaderSize || tableOffset > static_cast<uint64_t>(fileSize) ||
        entryCount > 100000 || entryCount > (static_cast<uint64_t>(fileSize) - tableOffset) / 18) return false;

    f.seekg(static_cast<std::streamoff>(tableOffset));
    if (!f) return false;

    Mount m;
    m.kind = Mount::Kind::Pack;
    m.path = path;

    for (uint32_t i = 0; i < entryCount; ++i) {
        uint8_t lenBuf[2];
        f.read(reinterpret_cast<char*>(lenBuf), 2);
        if (!f) return false;
        uint16_t pathLen = readLE<uint16_t>(lenBuf);

        std::string entryPath(pathLen, '\0');
        f.read(entryPath.data(), pathLen);

        uint8_t rest[16];
        f.read(reinterpret_cast<char*>(rest), 16);
        if (!f) {
            std::printf("[vfs] '%s': таблица оборвалась на записи %u\n", path.c_str(), i);
            return false;
        }

        PackEntry e;
        e.offset = readLE<uint64_t>(rest);
        e.size = readLE<uint32_t>(rest + 8);
        e.crc = readLE<uint32_t>(rest + 12);
        const auto key = normalize(entryPath);
        if (key.empty() || e.offset < kHeaderSize || e.offset > tableOffset ||
            e.size > tableOffset - e.offset || e.size > 64 * 1024 * 1024 ||
            m.entries.count(key)) return false;
        m.entries[key] = e;
    }

    std::printf("[vfs] смонтирован пак '%s': %zu файлов\n", path.c_str(), m.entries.size());
    g_mounts.push_back(std::move(m));
    return true;
}

int mountPacksFrom(const std::string& dir, const std::string& ext) {
    std::error_code ec;
    if (!fs::is_directory(dir, ec)) return 0;

    // По алфавиту: порядок монтирования должен быть предсказуем,
    // иначе два DLC могли бы перекрывать друг друга по-разному на разных машинах
    std::vector<std::string> packs;
    for (const auto& entry : fs::directory_iterator(dir, ec)) {
        if (!entry.is_regular_file(ec)) continue;
        if (entry.path().extension().string() == ext) {
            packs.push_back(entry.path().string());
        }
    }
    std::sort(packs.begin(), packs.end());

    int mounted = 0;
    for (const std::string& p : packs) {
        if (mountPack(p)) ++mounted;
    }
    return mounted;
}

void unmountAll() {
    g_mounts.clear();
    g_misses = 0;
}

int mountCount() { return static_cast<int>(g_mounts.size()); }

std::vector<std::string> mountNames() {
    std::vector<std::string> names;
    names.reserve(g_mounts.size());
    for (const Mount& m : g_mounts) {
        names.push_back((m.kind == Mount::Kind::Dir ? "dir  " : "pack ") + m.path);
    }
    return names;
}

bool exists(const std::string& path) {
    std::string key = normalize(path);
    for (const Mount& m : g_mounts) {
        if (mountHas(m, key)) return true;
    }
    return false;
}

bool read(const std::string& path, std::vector<uint8_t>& out) {
    out.clear();
    std::string key = normalize(path);
    // С конца: последнее смонтированное имеет наибольший приоритет
    for (auto it = g_mounts.rbegin(); it != g_mounts.rend(); ++it) {
        if (mountHas(*it, key)) return readFrom(*it, key, out);
    }
    ++g_misses;
    return false;
}

bool readText(const std::string& path, std::string& out) {
    out.clear();
    std::vector<uint8_t> bytes;
    if (!read(path, bytes)) return false;
    out.assign(bytes.begin(), bytes.end());
    return true;
}

bool readLayers(const std::string& path, std::vector<std::vector<uint8_t>>& out) {
    std::string key = normalize(path);
    out.clear();
    // По возрастанию приоритета: сначала база, потом то, что её перекрывает
    for (const Mount& m : g_mounts) {
        std::vector<uint8_t> bytes;
        if (!mountHas(m, key)) continue;
        if (!readFrom(m, key, bytes)) { out.clear(); return false; }
        out.push_back(std::move(bytes));
    }
    if (out.empty()) ++g_misses;
    return !out.empty();
}

bool readTextLayers(const std::string& path, std::vector<std::string>& out) {
    out.clear();
    std::vector<std::vector<uint8_t>> layers;
    if (!readLayers(path, layers)) return false;
    out.clear();
    out.reserve(layers.size());
    for (const auto& bytes : layers) out.emplace_back(bytes.begin(), bytes.end());
    return true;
}

std::vector<std::string> list(const std::string& prefix) {
    std::string key = normalize(prefix);
    if (!prefix.empty() && key.empty()) return {};
    std::vector<std::string> found;

    for (const Mount& m : g_mounts) {
        if (m.kind == Mount::Kind::Pack) {
            for (const auto& kv : m.entries) {
                if (kv.first.compare(0, key.size(), key) == 0) found.push_back(kv.first);
            }
        } else {
            std::error_code ec;
            fs::path root = fs::u8path(m.path);
            for (const auto& entry : fs::recursive_directory_iterator(root, ec)) {
                if (!entry.is_regular_file(ec)) continue;
                std::string rel = normalize(fs::relative(entry.path(), root, ec).generic_u8string());
                if (rel.compare(0, key.size(), key) == 0) found.push_back(rel);
            }
        }
    }

    std::sort(found.begin(), found.end());
    found.erase(std::unique(found.begin(), found.end()), found.end());
    return found;
}

std::string describe(const std::string& path) {
    std::string key = normalize(path);
    for (auto it = g_mounts.rbegin(); it != g_mounts.rend(); ++it) {
        if (mountHas(*it, key)) return it->path;
    }
    return "<не найдено>";
}

int missCount() { return g_misses; }

} // namespace Vfs

} // namespace scrp
