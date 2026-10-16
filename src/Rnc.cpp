#include "scrp/Rnc.h"

namespace scrp {
namespace {

constexpr std::size_t kHeaderSize = 18;

std::uint16_t readBe16(const std::uint8_t* p) {
    return static_cast<std::uint16_t>((p[0] << 8) | p[1]);
}

std::uint32_t readBe32(const std::uint8_t* p) {
    return (static_cast<std::uint32_t>(p[0]) << 24) |
           (static_cast<std::uint32_t>(p[1]) << 16) |
           (static_cast<std::uint32_t>(p[2]) << 8) |
           static_cast<std::uint32_t>(p[3]);
}

// Codes are stored bit-reversed: cheaper to compare on a 68000.
std::uint32_t mirror(std::uint32_t value, int bits) {
    std::uint32_t out = 0;
    for (int i = 0; i < bits; ++i) {
        if ((value >> i) & 1u) out |= 1u << (bits - 1 - i);
    }
    return out;
}

// One cursor for both the bit stream and the literal bytes. See Rnc.h for
// why they must share it.
class Cursor {
public:
    Cursor(const std::uint8_t* data, std::size_t size, std::size_t pos)
        : data_(data), size_(size), pos_(pos) {}

    bool fill(int bits) {
        while (count_ < bits) {
            if (pos_ >= size_) return false;
            std::uint32_t low = data_[pos_];
            std::uint32_t high = (pos_ + 1 < size_) ? data_[pos_ + 1] : 0u;
            buffer_ |= ((low | (high << 8)) << count_);
            count_ += 16;
            pos_ += 2;
        }
        return true;
    }

    bool peek(int bits, std::uint32_t& out) {
        if (!fill(bits)) return false;
        out = buffer_ & ((1u << bits) - 1u);
        return true;
    }

    bool drop(int bits) {
        if (!fill(bits)) return false;
        buffer_ >>= bits;
        count_ -= bits;
        return true;
    }

    bool read(int bits, std::uint32_t& out) {
        return peek(bits, out) && drop(bits);
    }

    // Literal bytes come from the same cursor. The bit buffer is left
    // alone on purpose: its remaining bits are still part of the stream.
    bool literal(std::size_t length, const std::uint8_t*& begin) {
        if (pos_ + length > size_) return false;
        begin = data_ + pos_;
        pos_ += length;
        return true;
    }

private:
    const std::uint8_t* data_;
    std::size_t size_;
    std::size_t pos_;
    std::uint64_t buffer_ = 0;
    int count_ = 0;
};

struct Leaf {
    std::uint32_t code = 0;
    int bits = 0;
    std::uint32_t value = 0;
};

class Table {
public:
    // A table is given as code lengths: a 5 bit leaf count, then 4 bits per
    // leaf. Codes are rebuilt canonically and then mirrored.
    bool read(Cursor& cursor) {
        leaves_.clear();
        std::uint32_t count = 0;
        if (!cursor.read(5, count)) return false;
        if (count == 0) return true;

        std::vector<std::uint32_t> lengths(count);
        int longest = 0;
        for (std::uint32_t i = 0; i < count; ++i) {
            if (!cursor.read(4, lengths[i])) return false;
            if (static_cast<int>(lengths[i]) > longest) longest = static_cast<int>(lengths[i]);
        }

        std::uint32_t code = 0;
        for (int bits = 1; bits <= longest; ++bits) {
            for (std::uint32_t value = 0; value < count; ++value) {
                if (static_cast<int>(lengths[value]) != bits) continue;
                leaves_.push_back({mirror(code, bits), bits, value});
                ++code;
            }
            code <<= 1;
        }
        return true;
    }

    // Values of two and above are an ORDER rather than a number: the value
    // itself follows as another value-1 bits. That is how large distances
    // and lengths stay cheap without inflating the table.
    bool value(Cursor& cursor, std::uint32_t& out) const {
        for (const Leaf& leaf : leaves_) {
            std::uint32_t bits = 0;
            if (!cursor.peek(leaf.bits, bits)) return false;
            if (bits != leaf.code) continue;
            if (!cursor.drop(leaf.bits)) return false;
            if (leaf.value < 2) {
                out = leaf.value;
                return true;
            }
            const int extraBits = static_cast<int>(leaf.value) - 1;
            std::uint32_t extra = 0;
            if (!cursor.read(extraBits, extra)) return false;
            out = extra | (1u << extraBits);
            return true;
        }
        return false;
    }

private:
    std::vector<Leaf> leaves_;
};

bool fail(std::string* error, const char* text) {
    if (error) *error = text;
    return false;
}

}  // namespace

std::uint16_t Rnc::crc16(const std::uint8_t* data, std::size_t size) {
    std::uint16_t crc = 0;
    for (std::size_t i = 0; i < size; ++i) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; ++bit) {
            crc = (crc & 1u) ? static_cast<std::uint16_t>((crc >> 1) ^ 0xA001u)
                             : static_cast<std::uint16_t>(crc >> 1);
        }
    }
    return crc;
}

bool Rnc::isPacked(const std::uint8_t* data, std::size_t size) {
    return data != nullptr && size >= kHeaderSize &&
           data[0] == 'R' && data[1] == 'N' && data[2] == 'C';
}

bool Rnc::readHeader(const std::uint8_t* data, std::size_t size, RncHeader& out) {
    if (!isPacked(data, size)) return false;
    out.method = data[3];
    out.unpackedSize = readBe32(data + 4);
    out.packedSize = readBe32(data + 8);
    // Offset 12 holds the checksum of the UNPACKED data. Some format
    // descriptions give the opposite order; measurement on real files
    // says otherwise.
    out.unpackedCrc = readBe16(data + 12);
    out.packedCrc = readBe16(data + 14);
    out.leeway = readBe16(data + 16);
    return true;
}

bool Rnc::unpack(const std::uint8_t* data, std::size_t size,
                 std::vector<std::uint8_t>& out, std::string* error) {
    RncHeader header;
    if (!readHeader(data, size, header)) return fail(error, "not an RNC stream");
    if (header.method != 1) return fail(error, "only RNC method 1 is supported");

    out.clear();
    out.reserve(header.unpackedSize);

    Cursor cursor(data, size, kHeaderSize);
    if (!cursor.drop(2)) return fail(error, "stream ends in the header");

    Table literals, distances, lengths;
    while (out.size() < header.unpackedSize) {
        if (!literals.read(cursor) || !distances.read(cursor) || !lengths.read(cursor))
            return fail(error, "truncated Huffman table");

        std::uint32_t pairs = 0;
        if (!cursor.read(16, pairs)) return fail(error, "truncated pair count");
        if (pairs == 0) return fail(error, "empty block would not terminate");

        while (pairs > 0) {
            --pairs;

            std::uint32_t runLength = 0;
            if (!literals.value(cursor, runLength)) return fail(error, "bad literal code");
            if (runLength > 0) {
                const std::uint8_t* begin = nullptr;
                if (!cursor.literal(runLength, begin)) return fail(error, "literal past end");
                out.insert(out.end(), begin, begin + runLength);
            }
            if (pairs == 0) break;   // the last pair of a block has no match

            std::uint32_t distance = 0;
            std::uint32_t matchLength = 0;
            if (!distances.value(cursor, distance)) return fail(error, "bad distance code");
            if (!lengths.value(cursor, matchLength)) return fail(error, "bad length code");
            distance += 1;
            matchLength += 2;

            if (distance > out.size()) return fail(error, "distance past start of output");
            // A match may overlap itself, so copy one byte at a time.
            const std::size_t start = out.size() - distance;
            for (std::uint32_t i = 0; i < matchLength; ++i) out.push_back(out[start + i]);
        }
    }

    out.resize(header.unpackedSize);
    if (crc16(out.data(), out.size()) != header.unpackedCrc)
        return fail(error, "checksum mismatch");
    if (error) error->clear();
    return true;
}

bool Rnc::unpackIfPacked(const std::uint8_t* data, std::size_t size,
                         std::vector<std::uint8_t>& out, std::string* error) {
    if (isPacked(data, size)) return unpack(data, size, out, error);
    out.assign(data, data + size);
    if (error) error->clear();
    return true;
}

}  // namespace scrp
