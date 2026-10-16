#include "scrp/PackWriter.h"

#include <cstdio>

namespace scrp {
namespace {

constexpr std::uint32_t kVersion = 1;
constexpr std::size_t kHeaderSize = 20;

template <typename T>
void writeLe(std::ofstream& out, T value) {
    std::uint8_t bytes[sizeof(T)];
    for (std::size_t i = 0; i < sizeof(T); ++i)
        bytes[i] = static_cast<std::uint8_t>(value >> (i * 8));
    out.write(reinterpret_cast<const char*>(bytes), sizeof(T));
}

bool fail(std::string* error, const std::string& text) {
    if (error) *error = text;
    return false;
}

// A virtual path must be usable by the reader on any platform, so the
// awkward cases are rejected at write time rather than at load time.
bool pathLooksSane(const std::string& p) {
    if (p.empty() || p.size() > 0xFFFF) return false;
    if (p.front() == '/' || p.back() == '/') return false;
    if (p.find('\\') != std::string::npos) return false;
    if (p.find("..") != std::string::npos) return false;
    if (p.find("//") != std::string::npos) return false;
    return true;
}

}  // namespace

std::uint32_t crc32(const std::uint8_t* data, std::size_t size) {
    static std::uint32_t table[256];
    static bool ready = false;
    if (!ready) {
        for (std::uint32_t i = 0; i < 256; ++i) {
            std::uint32_t c = i;
            for (int k = 0; k < 8; ++k)
                c = (c & 1u) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
            table[i] = c;
        }
        ready = true;
    }
    std::uint32_t crc = 0xFFFFFFFFu;
    for (std::size_t i = 0; i < size; ++i)
        crc = table[(crc ^ data[i]) & 0xFFu] ^ (crc >> 8);
    return crc ^ 0xFFFFFFFFu;
}

PackWriter::~PackWriter() {
    // An unfinished package is worse than none: it has a header promising
    // a table that was never written. Remove it rather than leave a trap.
    if (out_.is_open() && !finished_) {
        out_.close();
        std::remove(path_.c_str());
    }
}

bool PackWriter::open(const std::string& path, std::string* error) {
    out_.open(path, std::ios::binary | std::ios::trunc);
    if (!out_) return fail(error, "cannot create package: " + path);
    path_ = path;
    entries_.clear();
    finished_ = false;
    // Placeholder header; patched by finish() once the table offset is known.
    const char zero[kHeaderSize] = {};
    out_.write(zero, kHeaderSize);
    return static_cast<bool>(out_);
}

bool PackWriter::add(const std::string& virtualPath, const std::uint8_t* data,
                     std::size_t size, std::string* error) {
    if (!out_.is_open()) return fail(error, "package is not open");
    if (finished_) return fail(error, "package is already finished");
    if (!pathLooksSane(virtualPath)) return fail(error, "bad virtual path: " + virtualPath);
    for (const Entry& e : entries_) {
        if (e.path == virtualPath) return fail(error, "duplicate path: " + virtualPath);
    }
    if (size > 0xFFFFFFFFull) return fail(error, "entry too large: " + virtualPath);

    Entry entry;
    entry.path = virtualPath;
    entry.offset = static_cast<std::uint64_t>(out_.tellp());
    entry.size = static_cast<std::uint32_t>(size);
    entry.crc = crc32(data, size);
    if (size) out_.write(reinterpret_cast<const char*>(data), static_cast<std::streamsize>(size));
    if (!out_) return fail(error, "write failed: " + virtualPath);
    entries_.push_back(entry);
    return true;
}

bool PackWriter::add(const std::string& virtualPath, const std::string& text,
                     std::string* error) {
    return add(virtualPath, reinterpret_cast<const std::uint8_t*>(text.data()),
               text.size(), error);
}

bool PackWriter::finish(std::string* error) {
    if (!out_.is_open()) return fail(error, "package is not open");
    if (finished_) return fail(error, "package is already finished");

    const std::uint64_t tableOffset = static_cast<std::uint64_t>(out_.tellp());
    for (const Entry& e : entries_) {
        writeLe<std::uint16_t>(out_, static_cast<std::uint16_t>(e.path.size()));
        out_.write(e.path.data(), static_cast<std::streamsize>(e.path.size()));
        writeLe<std::uint64_t>(out_, e.offset);
        writeLe<std::uint32_t>(out_, e.size);
        writeLe<std::uint32_t>(out_, e.crc);
    }
    if (!out_) return fail(error, "writing the table failed");

    out_.seekp(0);
    out_.write("SCRP", 4);
    writeLe<std::uint32_t>(out_, kVersion);
    writeLe<std::uint64_t>(out_, tableOffset);
    writeLe<std::uint32_t>(out_, static_cast<std::uint32_t>(entries_.size()));
    if (!out_) return fail(error, "writing the header failed");

    out_.close();
    finished_ = true;
    if (error) error->clear();
    return true;
}

}  // namespace scrp
