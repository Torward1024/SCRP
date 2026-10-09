#include "scrp/IndexedImage.h"
#include <filesystem>
#include <fstream>

namespace scrp {
Palette diagnosticPalette() {
    Palette p{};
    for (int i = 0; i < 256; ++i) p[i] = {uint8_t(i), uint8_t(i), uint8_t(i), 255};
    // Standard EGA palette. Remaining entries are diagnostic grayscale.
    for (int i = 0; i < 16; ++i) {
        const int bright = (i & 8) ? 85 : 0;
        p[i] = {uint8_t(((i & 4) ? 170 : 0) + bright),
                uint8_t(((i & 2) ? 170 : 0) + bright),
                uint8_t(((i & 1) ? 170 : 0) + bright), 255};
    }
    p[6] = {170, 85, 0, 255};
    return p;
}
bool IndexedImage::valid() const {
    return width > 0 && height > 0 && width <= 4096 && height <= 4096 &&
           pixels.size() == size_t(width) * size_t(height);
}
std::vector<uint8_t> IndexedImage::rgba() const {
    if (!valid()) return {};
    std::vector<uint8_t> result(pixels.size() * 4);
    for (size_t i = 0; i < pixels.size(); ++i) {
        const auto c = palette[pixels[i]];
        result[i*4] = c.r; result[i*4+1] = c.g;
        result[i*4+2] = c.b; result[i*4+3] = c.a;
    }
    return result;
}
bool IndexedImage::writeBmp(const std::string& path, std::string& error) const {
    if (!valid()) { error = "Invalid indexed image"; return false; }
    std::ofstream file(std::filesystem::u8path(path), std::ios::binary);
    if (!file) { error = "Cannot create BMP: " + path; return false; }
    const uint32_t stride = (uint32_t(width)*3+3) & ~3u;
    auto le = [&file](uint32_t v, int count) {
        for (int i = 0; i < count; ++i) file.put(char((v >> (i*8)) & 255));
    };
    file.write("BM", 2); le(54+stride*height, 4); le(0,4); le(54,4);
    le(40,4); le(width,4); le(height,4); le(1,2); le(24,2);
    le(0,4); le(stride*height,4); le(0,4); le(0,4); le(0,4); le(0,4);
    for (int y = height-1; y >= 0; --y) {
        for (int x = 0; x < width; ++x) {
            const auto c = palette[pixels[size_t(y)*width+x]];
            file.put(char(c.b)); file.put(char(c.g)); file.put(char(c.r));
        }
        for (uint32_t i = width*3; i < stride; ++i) file.put(0);
    }
    file.flush();
    if (!file) { error = "Cannot write BMP: " + path; return false; }
    return true;
}
} // namespace scrp
