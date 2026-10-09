#pragma once
#include "Assets.h"
#include <map>
#include <string_view>

namespace scrp {
class Gfx;
struct BitmapGlyph { SDL_Rect source{}; int advance = 0; };
class BitmapFont {
public:
    // Glyphs use Unicode code points; asset decoding and character mapping belong to the caller.
    bool configure(SpriteId atlas, int lineHeight, std::map<uint32_t, BitmapGlyph> glyphs, uint32_t fallback = '?');
    int textWidth(std::string_view text) const;
    bool draw(Gfx& gfx, int x, int y, std::string_view text, SDL_Color color, int maxWidth = -1) const;
    int lineHeight() const { return lineHeight_; }
private:
    SpriteId atlas_;
    int lineHeight_ = 0;
    uint32_t fallback_ = '?';
    std::map<uint32_t, BitmapGlyph> glyphs_;
    const BitmapGlyph* glyph(uint32_t code) const;
};
} // namespace scrp
