#pragma once
#include <cstdint>
#include <string>
#include <string_view>

namespace scrp::Utf8 {
// Failed decoding leaves the cursor and value unchanged.
inline bool next(std::string_view text, size_t& offset, uint32_t& value) {
    if (offset >= text.size()) return false;
    size_t pos = offset; const auto first = static_cast<unsigned char>(text[pos++]);
    uint32_t code = first, minimum = 0; int count = 0;
    if (first >= 0xc2 && first <= 0xdf) { count = 1; code = first & 31; minimum = 128; }
    else if (first >= 0xe0 && first <= 0xef) { count = 2; code = first & 15; minimum = 2048; }
    else if (first >= 0xf0 && first <= 0xf4) { count = 3; code = first & 7; minimum = 65536; }
    else if (first >= 128) return false;
    if (text.size() - pos < size_t(count)) return false;
    for (int n = 0; n < count; ++n) {
        const auto byte = static_cast<unsigned char>(text[pos++]);
        if ((byte & 0xc0) != 0x80) return false;
        code = code * 64 + (byte & 63);
    }
    if (code < minimum || code > 0x10ffff || (code >= 0xd800 && code <= 0xdfff)) return false;
    offset = pos; value = code; return true;
}
inline bool valid(std::string_view text) {
    for (size_t pos = 0; pos < text.size();) { uint32_t code; if (!next(text, pos, code)) return false; }
    return true;
}
inline bool popBack(std::string& text) {
    if (text.empty()) return false;
    size_t last = 0;
    for (size_t pos = 0; pos < text.size();) { last = pos; uint32_t code; if (!next(text, pos, code)) return false; }
    text.resize(last); return true;
}
} // namespace scrp::Utf8
