#pragma once
// Writer for SCRP v1 resource packages.
//
// Vfs mounts packages read-only on purpose, so writing lives here instead
// of widening that interface. The byte layout is defined by the engine, so
// it is written in one place: a second implementation in a consumer would
// drift from the reader sooner or later.
//
// Layout, version 1, little-endian throughout:
//
//   [0..3]    "SCRP"
//   [4..7]    version
//   [8..15]   table offset
//   [16..19]  entry count
//   [20..]    blobs back to back
//   [table]   per entry: uint16 path length, path bytes (UTF-8, '/'),
//             uint64 blob offset, uint32 size, uint32 crc32
//
// The table trails the blobs so a package can be written as a stream
// without holding it in memory. Version 1 has no compression by design:
// the format must be trivially correct first.

#include <cstddef>
#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

namespace scrp {

// CRC-32 as used by the package format (polynomial 0xEDB88320).
std::uint32_t crc32(const std::uint8_t* data, std::size_t size);

class PackWriter {
public:
    PackWriter() = default;
    ~PackWriter();

    PackWriter(const PackWriter&) = delete;
    PackWriter& operator=(const PackWriter&) = delete;

    bool open(const std::string& path, std::string* error = nullptr);

    // Paths are virtual: UTF-8, '/' separated, no leading slash. Adding the
    // same path twice is rejected rather than silently shadowed.
    bool add(const std::string& virtualPath, const std::uint8_t* data,
             std::size_t size, std::string* error = nullptr);
    bool add(const std::string& virtualPath, const std::string& text,
             std::string* error = nullptr);

    // Writes the table and patches the header. Nothing is usable before it.
    bool finish(std::string* error = nullptr);

    std::size_t entryCount() const { return entries_.size(); }

private:
    struct Entry {
        std::string path;
        std::uint64_t offset = 0;
        std::uint32_t size = 0;
        std::uint32_t crc = 0;
    };

    std::ofstream out_;
    std::string path_;
    std::vector<Entry> entries_;
    bool finished_ = false;
};

}  // namespace scrp
