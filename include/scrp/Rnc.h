#pragma once
// Rob Northen Compression, method 1 decoder.
//
// This is a general purpose codec of the Amiga/Atari ST era, not a decoder
// for any single game's data layout, which is why it belongs here and not
// in a game. Games keep their own container formats and build on this.
//
// Format in short. An 18 byte big-endian header, then a bit stream read as
// little-endian 16 bit words, least significant bit first, with the first
// two bits reserved. A block starts with three Huffman tables (literal
// lengths, match distances, match lengths) followed by a 16 bit pair count.
// Each pair is a literal run then a match; the last pair of a block has no
// match.
//
// The subtle part: a single cursor serves both streams. Refilling the bit
// buffer consumes two bytes and a literal run consumes its own bytes, both
// advancing the same pointer, and the bit buffer is NOT reset across a
// literal run. Bits already buffered before the run are still used after it.
//
// Output is always checked against the header checksum, so corrupt input
// cannot pass silently.

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace scrp {

struct RncHeader {
    std::uint8_t  method = 0;
    std::uint32_t unpackedSize = 0;
    std::uint32_t packedSize = 0;
    std::uint16_t unpackedCrc = 0;
    std::uint16_t packedCrc = 0;
    std::uint16_t leeway = 0;
};

namespace Rnc {

// CRC-16/IBM, also known as ARC: polynomial 0xA001, reflected, zero seed.
// Check value for "123456789" is 0xBB3D.
std::uint16_t crc16(const std::uint8_t* data, std::size_t size);

// Whether the buffer starts with the RNC signature.
bool isPacked(const std::uint8_t* data, std::size_t size);

// Reads the header. Returns false when the buffer is not RNC.
bool readHeader(const std::uint8_t* data, std::size_t size, RncHeader& out);

// Decodes method 1. On success `out` holds data whose checksum matches the
// header; on failure `error` explains what went wrong.
bool unpack(const std::uint8_t* data, std::size_t size,
            std::vector<std::uint8_t>& out, std::string* error = nullptr);

// Convenience: an unpacked buffer is copied through unchanged. Mixed sets
// where only some files are packed are common.
bool unpackIfPacked(const std::uint8_t* data, std::size_t size,
                    std::vector<std::uint8_t>& out,
                    std::string* error = nullptr);

}  // namespace Rnc
}  // namespace scrp
