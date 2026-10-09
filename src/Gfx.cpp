#include "scrp/Gfx.h"
#include "scrp/Rng.h"
#include <cmath>
#include <algorithm>

namespace scrp {
namespace {

inline Uint8 modulate(Uint8 base, Uint8 tint) {
    return static_cast<Uint8>((static_cast<int>(base) * static_cast<int>(tint)) / 255);
}
} // namespace

bool Gfx::init(SDL_Renderer* renderer, int worldW, int worldH, int artScale) {
    renderer_ = renderer;
    logicalW_ = worldW;
    logicalH_ = worldH;
    artScale_ = artScale > 0 ? artScale : 1;

    SDL_RenderSetLogicalSize(renderer_, logicalW_ * artScale_, logicalH_ * artScale_);
    SDL_RenderSetIntegerScale(renderer_, SDL_TRUE);
    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);

    return assets_.init(renderer_);
}

void Gfx::shutdown() {
    assets_.shutdown();
    renderer_ = nullptr;
}

void Gfx::setColor(SDL_Color c) {
    SDL_SetRenderDrawColor(renderer_, c.r, c.g, c.b, c.a);
}

void Gfx::beginFrame(SDL_Color clear) {
    setColor(clear);
    SDL_RenderClear(renderer_);
}

void Gfx::endFrame() {
    SDL_RenderPresent(renderer_);
}

void Gfx::focusOn(const Vec2& point, float time) {
    if (time <= 0.f) return;
    focusPoint_ = point;
    focusTime_ = focusMaxTime_ = time;
}

void Gfx::updateFocus(float dt) {
    if (focusTime_ > 0.f) focusTime_ -= dt;
}

void Gfx::centerCameraOn(const Vec2& target, int levelPxW, int levelPxH) {
    Vec2 aim = target;

    if (focusTime_ > 0.f && focusMaxTime_ > 0.f) {
        float passed = 1.f - focusTime_ / focusMaxTime_;   // 0 -> 1
        const float kTravel = focusTravel_;                    // разгон и возврат
        float k = passed < kTravel        ? passed / kTravel
                : passed > 1.f - kTravel  ? (1.f - passed) / kTravel
                                          : 1.f;
        aim = target + (focusPoint_ - target) * std::clamp(k, 0.f, 1.f);
    }

    camera_.x = aim.x - logicalW_ * 0.5f;
    camera_.y = aim.y - logicalH_ * 0.5f;

    float maxX = static_cast<float>(levelPxW - logicalW_);
    float maxY = static_cast<float>(levelPxH - logicalH_);

    if (maxX <= 0.f) camera_.x = (levelPxW - logicalW_) * 0.5f;
    else camera_.x = std::clamp(camera_.x, 0.f, maxX);

    if (maxY <= 0.f) camera_.y = (levelPxH - logicalH_) * 0.5f;
    else camera_.y = std::clamp(camera_.y, 0.f, maxY);
}

void Gfx::addShake(float strength) {
    shakeAmount_ = std::min(shakeAmount_ + strength, shakeMax_);
}

void Gfx::addFlash(SDL_Color color, float time) {
    if (time <= 0.f) return;
    if (flashTime_ > 0.f && color.a < flashColor_.a) return;
    flashColor_ = color;
    flashTime_ = flashMaxTime_ = time;
}

void Gfx::updateFlash(float dt) {
    if (flashTime_ > 0.f) flashTime_ -= dt;
}

void Gfx::renderFlash() {
    if (flashTime_ <= 0.f || flashMaxTime_ <= 0.f) return;
    float k = flashTime_ / flashMaxTime_;
    SDL_Color c = flashColor_;
    c.a = static_cast<Uint8>(flashColor_.a * k);
    fillRectScreen(0, 0, width(), height(), c);
}

void Gfx::updateShake(float dt) {
    if (shakeAmount_ <= 0.01f) {
        shakeAmount_ = 0.f;
        shakeOffset_ = { 0.f, 0.f };
        return;
    }
    shakeOffset_ = (*rng_).direction() * shakeAmount_;
    shakeAmount_ -= shakeAmount_ * shakeDecay_ * dt;
}

Vec2 Gfx::renderOrigin() const {
    return { std::floor(camera_.x + shakeOffset_.x),
             std::floor(camera_.y + shakeOffset_.y) };
}

bool Gfx::visibleWorld(const Vec2& center, float w, float h) const {
    Vec2 origin = renderOrigin();
    float left   = center.x - w * 0.5f - origin.x;
    float top    = center.y - h * 0.5f - origin.y;
    return !(left + w < 0.f || top + h < 0.f ||
             left > static_cast<float>(logicalW_) || top > static_cast<float>(logicalH_));
}

void Gfx::drawSprite(SpriteId id, const Vec2& worldPos, int col, int row,
                     bool flipX, SDL_Color tint, float fallbackSize) {
    const SpriteDef& d = assets_.def(id);
    if (!visibleWorld(worldPos, static_cast<float>(d.frameW), static_cast<float>(d.frameH)))
        return;

    Vec2 origin = renderOrigin();
    const int S = artScale_;
    SDL_Rect dst;
    dst.x = static_cast<int>(std::floor(worldPos.x - origin.x)) * S - d.pivotX;
    dst.y = static_cast<int>(std::floor(worldPos.y - origin.y)) * S - d.pivotY;
    dst.w = d.frameW;
    dst.h = d.frameH;

    SDL_Texture* tex = assets_.texture(id);
    if (!tex) {
        if (fallbackSize > 0.f) {
            int size = std::max(1, static_cast<int>(fallbackSize)) * S;
            dst.x = static_cast<int>(std::floor(worldPos.x - origin.x)) * S - size / 2;
            dst.y = static_cast<int>(std::floor(worldPos.y - origin.y)) * S - size / 2;
            dst.w = size;
            dst.h = size;
        }
        SDL_Color c{ modulate(d.fallback.r, tint.r),
                     modulate(d.fallback.g, tint.g),
                     modulate(d.fallback.b, tint.b),
                     modulate(d.fallback.a, tint.a) };
        setColor(c);
        SDL_RenderFillRect(renderer_, &dst);
        return;
    }

    SDL_Rect src;
    src.x = col * d.frameW;
    src.y = row * d.frameH;
    src.w = d.frameW;
    src.h = d.frameH;

    SDL_SetTextureColorMod(tex, tint.r, tint.g, tint.b);
    SDL_SetTextureAlphaMod(tex, tint.a);
    SDL_RenderCopyEx(renderer_, tex, &src, &dst, 0.0, nullptr,
                     flipX ? SDL_FLIP_HORIZONTAL : SDL_FLIP_NONE);
    SDL_SetTextureColorMod(tex, 255, 255, 255);
    SDL_SetTextureAlphaMod(tex, 255);
}

void Gfx::drawTile(SpriteId sheet, int tileIndex, int worldX, int worldY, int tileSize) {
    const SpriteDef& d = assets_.def(sheet);
    Vec2 origin = renderOrigin();

    const int S = artScale_;
    SDL_Rect dst;
    dst.x = (worldX - static_cast<int>(origin.x)) * S;
    dst.y = (worldY - static_cast<int>(origin.y)) * S;
    dst.w = tileSize * S;
    dst.h = tileSize * S;

    if (dst.x + dst.w < 0 || dst.y + dst.h < 0 ||
        dst.x > logicalW_ * S || dst.y > logicalH_ * S)
        return;

    SDL_Texture* tex = assets_.texture(sheet);
    if (!tex) {
        SDL_Color c = d.fallback;
        if (tileIndex>=0 && static_cast<size_t>(tileIndex)<tileColors_.size()) c=tileColors_[tileIndex];
        setColor(c);
        SDL_RenderFillRect(renderer_, &dst);
        return;
    }

    int cols = d.columns > 0 ? d.columns : 1;
    SDL_Rect src;
    src.x = (tileIndex % cols) * d.frameW;
    src.y = (tileIndex / cols) * d.frameH;
    src.w = d.frameW;
    src.h = d.frameH;
    SDL_RenderCopy(renderer_, tex, &src, &dst);
}

void Gfx::fillRectWorld(const Vec2& center, float w, float h, SDL_Color c) {
    if (!visibleWorld(center, w, h)) return;
    Vec2 origin = renderOrigin();
    const float S = static_cast<float>(artScale_);
    SDL_Rect r;
    r.x = static_cast<int>(std::floor((center.x - w * 0.5f - origin.x) * S));
    r.y = static_cast<int>(std::floor((center.y - h * 0.5f - origin.y) * S));
    r.w = static_cast<int>(w * S);
    r.h = static_cast<int>(h * S);
    if (r.w < 1) r.w = 1;
    if (r.h < 1) r.h = 1;
    setColor(c);
    SDL_RenderFillRect(renderer_, &r);
}

void Gfx::fillPixelWorld(const Vec2& pos, float size, SDL_Color c) {
    fillRectWorld(pos, size, size, c);
}

void Gfx::drawRectWorld(const Vec2& center, float w, float h, SDL_Color c) {
    if (!visibleWorld(center, w, h)) return;
    Vec2 origin = renderOrigin();
    const float S = static_cast<float>(artScale_);
    SDL_Rect r;
    r.x = static_cast<int>(std::floor((center.x - w * 0.5f - origin.x) * S));
    r.y = static_cast<int>(std::floor((center.y - h * 0.5f - origin.y) * S));
    r.w = std::max(1, static_cast<int>(w * S));
    r.h = std::max(1, static_cast<int>(h * S));
    setColor(c);
    SDL_RenderDrawRect(renderer_, &r);
}

void Gfx::drawLineScreen(int x1, int y1, int x2, int y2, SDL_Color c) {
    const int S = artScale_;
    setColor(c);
    SDL_RenderDrawLine(renderer_, x1 * S, y1 * S, x2 * S, y2 * S);
}

void Gfx::drawLineWorld(const Vec2& a, const Vec2& b, SDL_Color c) {
    Vec2 origin = renderOrigin();
    const int S = artScale_;
    setColor(c);
    SDL_RenderDrawLine(renderer_,
                       static_cast<int>(a.x - origin.x) * S,
                       static_cast<int>(a.y - origin.y) * S,
                       static_cast<int>(b.x - origin.x) * S,
                       static_cast<int>(b.y - origin.y) * S);
}

SDL_Texture* Gfx::createTargetTexture(int w, int h) {
    SDL_Texture* tex = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_RGBA8888,
                                         SDL_TEXTUREACCESS_TARGET, w, h);
    if (!tex) return nullptr;
    SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
    SDL_SetTextureScaleMode(tex, SDL_ScaleModeNearest);

    beginTargetDraw(tex);
    SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 0);
    SDL_RenderClear(renderer_);
    endTargetDraw();
    return tex;
}

void Gfx::beginTargetDraw(SDL_Texture* target) {
    SDL_RenderSetLogicalSize(renderer_, 0, 0);
    SDL_SetRenderTarget(renderer_, target);
}

void Gfx::endTargetDraw() {
    SDL_SetRenderTarget(renderer_, nullptr);
    SDL_RenderSetLogicalSize(renderer_, logicalW_ * artScale_, logicalH_ * artScale_);
    SDL_RenderSetIntegerScale(renderer_, SDL_TRUE);
}

void Gfx::blitTargetWorld(SDL_Texture* target, int texW, int texH) {
    if (!target) return;

    Vec2 origin = renderOrigin();
    SDL_Rect src{ static_cast<int>(origin.x), static_cast<int>(origin.y),
                  logicalW_, logicalH_ };
    SDL_Rect dst{ 0, 0, logicalW_, logicalH_ };

    if (src.x < 0) { dst.x -= src.x; dst.w += src.x; src.w += src.x; src.x = 0; }
    if (src.y < 0) { dst.y -= src.y; dst.h += src.y; src.h += src.y; src.y = 0; }
    if (src.x + src.w > texW) { int over = src.x + src.w - texW; src.w -= over; dst.w -= over; }
    if (src.y + src.h > texH) { int over = src.y + src.h - texH; src.h -= over; dst.h -= over; }
    if (src.w <= 0 || src.h <= 0) return;

    const int S = artScale_;
    dst.x *= S; dst.y *= S; dst.w *= S; dst.h *= S;
    SDL_RenderCopy(renderer_, target, &src, &dst);
}

void Gfx::fillRectScreen(int x, int y, int w, int h, SDL_Color c) {
    const int S = artScale_;
    SDL_Rect r{ x * S, y * S, w * S, h * S };
    setColor(c);
    SDL_RenderFillRect(renderer_, &r);
}

void Gfx::drawRectScreen(int x, int y, int w, int h, SDL_Color c) {
    const int S = artScale_;
    SDL_Rect r{ x * S, y * S, w * S, h * S };
    setColor(c);
    SDL_RenderDrawRect(renderer_, &r);
}

void Gfx::drawSpriteScreen(SpriteId id, int x, int y, int col, int row, SDL_Color tint) {
    const SpriteDef& d = assets_.def(id);
    SDL_Rect dst{ x * artScale_, y * artScale_, d.frameW, d.frameH };

    SDL_Texture* tex = assets_.texture(id);
    if (!tex) {
        SDL_Color c{ modulate(d.fallback.r, tint.r),
                     modulate(d.fallback.g, tint.g),
                     modulate(d.fallback.b, tint.b),
                     modulate(d.fallback.a, tint.a) };
        setColor(c);
        SDL_RenderFillRect(renderer_, &dst);
        return;
    }

    SDL_Rect src{ col * d.frameW, row * d.frameH, d.frameW, d.frameH };
    SDL_SetTextureColorMod(tex, tint.r, tint.g, tint.b);
    SDL_SetTextureAlphaMod(tex, tint.a);
    SDL_RenderCopy(renderer_, tex, &src, &dst);
    SDL_SetTextureColorMod(tex, 255, 255, 255);
    SDL_SetTextureAlphaMod(tex, 255);
}

void Gfx::configure(const JsonValue& config) {
    assets_.configure(config["assets"]);
    shakeDecay_=std::max(0.f,static_cast<float>(config["shake_decay"].asNumber()));
    shakeMax_=std::max(0.f,static_cast<float>(config["shake_max"].asNumber()));
    focusTravel_=std::clamp(static_cast<float>(config["focus_travel"].asNumber(0.3)),0.001f,0.5f);
    tileColors_.clear(); for(const auto& color:config["tile_colors"].array) tileColors_.push_back(readColor(color));
}
} // namespace scrp
