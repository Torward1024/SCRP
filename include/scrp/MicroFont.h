#pragma once
#include <SDL.h>

namespace scrp {
class Gfx;

namespace MicroFont {

constexpr int GLYPH_W = 3;
constexpr int GLYPH_H = 5;
constexpr int ADVANCE = 4;   // ширина глифа + 1 пиксель разрядки

int textWidth(const char* text);

void draw(Gfx& gfx, int x, int y, const char* text, SDL_Color color);
void drawInt(Gfx& gfx, int x, int y, int value, SDL_Color color);

void drawRight(Gfx& gfx, int rightX, int y, const char* text, SDL_Color color);

void drawShadowed(Gfx& gfx, int x, int y, const char* text, SDL_Color color);

} // namespace MicroFont
} // namespace scrp
