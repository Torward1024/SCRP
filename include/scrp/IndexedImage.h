#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace scrp {
struct Color { uint8_t r = 0, g = 0, b = 0, a = 255; };
using Palette = std::array<Color, 256>;
Palette diagnosticPalette();
struct IndexedImage {
    int width = 0;
    int height = 0;
    std::vector<uint8_t> pixels;
    Palette palette = diagnosticPalette();
    bool valid() const;
    std::vector<uint8_t> rgba() const;
    // The same 24-bit BMP writeBmp produces, as bytes. Needed when the
    // image goes into a package rather than onto disk; writeBmp is a thin
    // wrapper so the two cannot drift.
    std::vector<uint8_t> bmp() const;
    bool writeBmp(const std::string& path, std::string& error) const;
};
} // namespace scrp
