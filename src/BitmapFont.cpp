#include "scrp/BitmapFont.h"
#include "scrp/Gfx.h"
#include "scrp/Utf8.h"
#include <algorithm>
#include <limits>

namespace scrp {
namespace {
uint32_t nextCode(std::string_view text, size_t& offset) {
    uint32_t code;
    if (Utf8::next(text, offset, code)) return code;
    ++offset; return 0xfffd;
}
bool nativeInt(int64_t value) { return value >= std::numeric_limits<int>::min() && value <= std::numeric_limits<int>::max(); }
}
bool BitmapFont::configure(SpriteId atlas, int lineHeight, std::map<uint32_t, BitmapGlyph> glyphs, uint32_t fallback) {
    if (!atlas.valid() || lineHeight <= 0 || glyphs.empty()) return false;
    for (const auto& entry : glyphs) {
        const auto& g = entry.second;
        if (entry.first > 0x10ffff || (entry.first >= 0xd800 && entry.first <= 0xdfff) ||
            g.source.x < 0 || g.source.y < 0 || g.source.w < 0 || g.source.h < 0 || g.advance < 0) return false;
    }
    atlas_ = atlas; lineHeight_ = lineHeight; fallback_ = fallback; glyphs_ = std::move(glyphs); return true;
}
const BitmapGlyph* BitmapFont::glyph(uint32_t code) const {
    auto it = glyphs_.find(code);
    if (it == glyphs_.end()) it = glyphs_.find(fallback_);
    return it == glyphs_.end() ? nullptr : &it->second;
}
int BitmapFont::textWidth(std::string_view text) const {
    int64_t width = 0, maximum = 0;
    for (size_t i = 0; i < text.size();) {
        const auto code = nextCode(text, i);
        if (code == '\n') { maximum = std::max(maximum, width); width = 0; }
        else if (code != '\r') if (const auto* g = glyph(code)) width = std::min<int64_t>(std::numeric_limits<int>::max(), width + g->advance);
    }
    return int(std::max(maximum, width));
}
bool BitmapFont::draw(Gfx& gfx, int x, int y, std::string_view text, SDL_Color color, int maxWidth) const {
    if (!lineHeight_) { SDL_SetError("Unconfigured bitmap font"); return false; }
    int64_t cursor = 0, row = y;
    bool clipped = false;
    for (size_t i = 0; i < text.size();) {
        const auto code = nextCode(text, i);
        if (code == '\n') { cursor = 0; row += lineHeight_; clipped = false; continue; }
        if (code == '\r') continue;
        const auto* g = glyph(code); if (!g || clipped) continue;
        if (maxWidth >= 0 && cursor + std::max(g->advance, g->source.w) > maxWidth) { clipped = true; continue; }
        if (!nativeInt(int64_t(x) + cursor) || !nativeInt(row)) { SDL_SetError("Bitmap text coordinate overflow"); return false; }
        if (g->source.w && g->source.h && !gfx.drawRegionScreen(atlas_, g->source, {int(int64_t(x) + cursor), int(row), g->source.w, g->source.h}, color)) return false;
        cursor += g->advance;
    }
    return true;
}
} // namespace scrp
