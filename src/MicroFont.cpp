#include "scrp/MicroFont.h"
#include "scrp/Gfx.h"
#include <cstdio>
#include <cstring>

namespace scrp {
namespace {

struct Glyph { unsigned char rows[MicroFont::GLYPH_H]; };

constexpr unsigned char R(unsigned char a, unsigned char b, unsigned char c) {
    return static_cast<unsigned char>((a << 2) | (b << 1) | c);
}

const Glyph kDigits[10] = {
    {{ R(1,1,1), R(1,0,1), R(1,0,1), R(1,0,1), R(1,1,1) }}, // 0
    {{ R(0,1,0), R(1,1,0), R(0,1,0), R(0,1,0), R(1,1,1) }}, // 1
    {{ R(1,1,1), R(0,0,1), R(1,1,1), R(1,0,0), R(1,1,1) }}, // 2
    {{ R(1,1,1), R(0,0,1), R(1,1,1), R(0,0,1), R(1,1,1) }}, // 3
    {{ R(1,0,1), R(1,0,1), R(1,1,1), R(0,0,1), R(0,0,1) }}, // 4
    {{ R(1,1,1), R(1,0,0), R(1,1,1), R(0,0,1), R(1,1,1) }}, // 5
    {{ R(1,1,1), R(1,0,0), R(1,1,1), R(1,0,1), R(1,1,1) }}, // 6
    {{ R(1,1,1), R(0,0,1), R(0,1,0), R(0,1,0), R(0,1,0) }}, // 7
    {{ R(1,1,1), R(1,0,1), R(1,1,1), R(1,0,1), R(1,1,1) }}, // 8
    {{ R(1,1,1), R(1,0,1), R(1,1,1), R(0,0,1), R(1,1,1) }}, // 9
};

const Glyph kLetters[26] = {
    {{ R(0,1,0), R(1,0,1), R(1,1,1), R(1,0,1), R(1,0,1) }}, // A
    {{ R(1,1,0), R(1,0,1), R(1,1,0), R(1,0,1), R(1,1,0) }}, // B
    {{ R(0,1,1), R(1,0,0), R(1,0,0), R(1,0,0), R(0,1,1) }}, // C
    {{ R(1,1,0), R(1,0,1), R(1,0,1), R(1,0,1), R(1,1,0) }}, // D
    {{ R(1,1,1), R(1,0,0), R(1,1,0), R(1,0,0), R(1,1,1) }}, // E
    {{ R(1,1,1), R(1,0,0), R(1,1,0), R(1,0,0), R(1,0,0) }}, // F
    {{ R(0,1,1), R(1,0,0), R(1,0,1), R(1,0,1), R(0,1,1) }}, // G
    {{ R(1,0,1), R(1,0,1), R(1,1,1), R(1,0,1), R(1,0,1) }}, // H
    {{ R(1,1,1), R(0,1,0), R(0,1,0), R(0,1,0), R(1,1,1) }}, // I
    {{ R(0,0,1), R(0,0,1), R(0,0,1), R(1,0,1), R(0,1,0) }}, // J
    {{ R(1,0,1), R(1,1,0), R(1,0,0), R(1,1,0), R(1,0,1) }}, // K
    {{ R(1,0,0), R(1,0,0), R(1,0,0), R(1,0,0), R(1,1,1) }}, // L
    {{ R(1,0,1), R(1,1,1), R(1,1,1), R(1,0,1), R(1,0,1) }}, // M
    {{ R(1,1,0), R(1,0,1), R(1,0,1), R(1,0,1), R(1,0,1) }}, // N
    {{ R(0,1,0), R(1,0,1), R(1,0,1), R(1,0,1), R(0,1,0) }}, // O
    {{ R(1,1,0), R(1,0,1), R(1,1,0), R(1,0,0), R(1,0,0) }}, // P
    {{ R(0,1,0), R(1,0,1), R(1,0,1), R(1,1,1), R(0,1,1) }}, // Q
    {{ R(1,1,0), R(1,0,1), R(1,1,0), R(1,0,1), R(1,0,1) }}, // R
    {{ R(0,1,1), R(1,0,0), R(0,1,0), R(0,0,1), R(1,1,0) }}, // S
    {{ R(1,1,1), R(0,1,0), R(0,1,0), R(0,1,0), R(0,1,0) }}, // T
    {{ R(1,0,1), R(1,0,1), R(1,0,1), R(1,0,1), R(1,1,1) }}, // U
    {{ R(1,0,1), R(1,0,1), R(1,0,1), R(1,0,1), R(0,1,0) }}, // V
    {{ R(1,0,1), R(1,0,1), R(1,1,1), R(1,1,1), R(1,0,1) }}, // W
    {{ R(1,0,1), R(1,0,1), R(0,1,0), R(1,0,1), R(1,0,1) }}, // X
    {{ R(1,0,1), R(1,0,1), R(0,1,0), R(0,1,0), R(0,1,0) }}, // Y
    {{ R(1,1,1), R(0,0,1), R(0,1,0), R(1,0,0), R(1,1,1) }}, // Z
};

const Glyph kColon  {{ R(0,0,0), R(0,1,0), R(0,0,0), R(0,1,0), R(0,0,0) }};
const Glyph kMinus  {{ R(0,0,0), R(0,0,0), R(1,1,1), R(0,0,0), R(0,0,0) }};
const Glyph kPlus   {{ R(0,0,0), R(0,1,0), R(1,1,1), R(0,1,0), R(0,0,0) }};
const Glyph kDot    {{ R(0,0,0), R(0,0,0), R(0,0,0), R(0,0,0), R(0,1,0) }};
const Glyph kSlash  {{ R(0,0,1), R(0,0,1), R(0,1,0), R(1,0,0), R(1,0,0) }};
const Glyph kExcl   {{ R(0,1,0), R(0,1,0), R(0,1,0), R(0,0,0), R(0,1,0) }};
const Glyph kPercent{{ R(1,0,1), R(0,0,1), R(0,1,0), R(1,0,0), R(1,0,1) }};
const Glyph kBlank  {{ R(0,0,0), R(0,0,0), R(0,0,0), R(0,0,0), R(0,0,0) }};

const Glyph* glyphFor(char c) {
    if (c >= '0' && c <= '9') return &kDigits[c - '0'];
    if (c >= 'A' && c <= 'Z') return &kLetters[c - 'A'];
    if (c >= 'a' && c <= 'z') return &kLetters[c - 'a'];
    switch (c) {
        case ':': return &kColon;
        case '-': return &kMinus;
        case '+': return &kPlus;
        case '.': return &kDot;
        case '/': return &kSlash;
        case '!': return &kExcl;
        case '%': return &kPercent;
        default:  return nullptr;   // spaces and unsupported characters render blank
    }
}

} // namespace

namespace MicroFont {

int textWidth(const char* text) {
    if (!text || !*text) return 0;
    int len = static_cast<int>(std::strlen(text));
    return len * ADVANCE - 1;   // omit the trailing spacing pixel
}

void draw(Gfx& gfx, int x, int y, const char* text, SDL_Color color) {
    if (!text) return;
    int cursor = x;
    for (const char* p = text; *p; ++p, cursor += ADVANCE) {
        const Glyph* g = glyphFor(*p);
        if (!g) continue;
        for (int row = 0; row < GLYPH_H; ++row) {
            unsigned char bits = g->rows[row];
            if (!bits) continue;
            for (int col = 0; col < GLYPH_W; ++col) {
                if (bits & (1 << (GLYPH_W - 1 - col))) {
                    gfx.fillRectScreen(cursor + col, y + row, 1, 1, color);
                }
            }
        }
    }
}

void drawInt(Gfx& gfx, int x, int y, int value, SDL_Color color) {
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%d", value);
    draw(gfx, x, y, buf, color);
}

void drawRight(Gfx& gfx, int rightX, int y, const char* text, SDL_Color color) {
    draw(gfx, rightX - textWidth(text), y, text, color);
}

void drawShadowed(Gfx& gfx, int x, int y, const char* text, SDL_Color color) {
    draw(gfx, x + 1, y + 1, text, Col::withAlpha(Col::Black, 200));
    draw(gfx, x, y, text, color);
}

} // namespace MicroFont
} // namespace scrp
