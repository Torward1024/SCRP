#include "scrp/Lighting.h"
#include "scrp/Grid.h"
#include "scrp/Gfx.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

namespace scrp {
namespace {

} // namespace

bool Lighting::buildGlow(Gfx& gfx) {
    glow_ = SDL_CreateTexture(gfx.raw(), SDL_PIXELFORMAT_ARGB8888,
                              SDL_TEXTUREACCESS_STATIC, glowSize_, glowSize_);
    if (!glow_) return false;

    std::vector<Uint32> pixels(glowSize_ * glowSize_);
    const float centre = (glowSize_ - 1) * 0.5f;
    for (int y = 0; y < glowSize_; ++y) {
        for (int x = 0; x < glowSize_; ++x) {
            const float dx = (x - centre) / centre;
            const float dy = (y - centre) / centre;
            const float d = std::sqrt(dx * dx + dy * dy);

            float a = 0.f;
            if (d < 1.f) {
                const float kSigma = sigma_;
                const float edge = std::exp(-1.f / (2.f * kSigma * kSigma));
                a = (std::exp(-(d * d) / (2.f * kSigma * kSigma)) - edge) / (1.f - edge);
            }

            const Uint8 v = static_cast<Uint8>(a * 255.f + 0.5f);
            pixels[y * glowSize_ + x] = (static_cast<Uint32>(v) << 24) | 0x00FFFFFFu;
        }
    }

    if (SDL_UpdateTexture(glow_, nullptr, pixels.data(),
                          glowSize_ * static_cast<int>(sizeof(Uint32))) != 0) {
        return false;
    }
    SDL_SetTextureBlendMode(glow_, SDL_BLENDMODE_ADD);

    SDL_SetTextureScaleMode(glow_, SDL_ScaleModeLinear);
    return true;
}

bool Lighting::init(Gfx& gfx) {
    shutdown();

    target_ = gfx.createTargetTexture(gfx.width(), gfx.height());
    if (!target_) {
        std::printf("[light] cannot create light map: %s; lighting disabled\n", SDL_GetError());
        return false;
    }
    SDL_SetTextureBlendMode(target_, SDL_BLENDMODE_MOD);

    addPremul_ = SDL_ComposeCustomBlendMode(
        SDL_BLENDFACTOR_ONE, SDL_BLENDFACTOR_ONE, SDL_BLENDOPERATION_ADD,
        SDL_BLENDFACTOR_ONE, SDL_BLENDFACTOR_ONE, SDL_BLENDOPERATION_ADD);

    scratch_ = gfx.createTargetTexture(gfx.width(), gfx.height());
    if (!scratch_) {
        std::printf("[light] cannot create scratch texture: %s; wall shadows disabled\n", SDL_GetError());
    }

    if (!buildGlow(gfx)) {
        std::printf("[light] cannot create gradient: %s; lighting disabled\n", SDL_GetError());
        shutdown();
        return false;
    }
    return true;
}

void Lighting::shutdown() {
    if (target_) SDL_DestroyTexture(target_);
    if (glow_) SDL_DestroyTexture(glow_);
    if (scratch_) SDL_DestroyTexture(scratch_);
    target_ = nullptr;
    glow_ = nullptr;
    scratch_ = nullptr;
    lights_.clear();
}

void Lighting::begin() {
    lights_.clear();
}

void Lighting::add(const Light& light) {
    if (light.radius <= 0.f || light.intensity <= 0.f) return;
    lights_.push_back(light);
}

bool Lighting::renderOccluded(Gfx& gfx, const Light& light, const SDL_Rect& dst) {
    if (!level_ || !scratch_ || light.radius < level_->tileSize()*2.f) return false;

    const Vec2 origin = gfx.renderOrigin();
    const int t = level_->tileSize();

    const int tx0 = static_cast<int>(std::floor((light.pos.x - light.radius) / t));
    const int tx1 = static_cast<int>(std::floor((light.pos.x + light.radius) / t));
    const int ty0 = static_cast<int>(std::floor((light.pos.y - light.radius) / t));
    const int ty1 = static_cast<int>(std::floor((light.pos.y + light.radius) / t));

    gfx.beginTargetDraw(scratch_);
    SDL_SetRenderDrawBlendMode(gfx.raw(), SDL_BLENDMODE_NONE);
    SDL_SetRenderDrawColor(gfx.raw(), 0, 0, 0, 0);
    SDL_RenderClear(gfx.raw());

    const float k = std::min(1.f, light.intensity);
    SDL_SetTextureBlendMode(glow_, SDL_BLENDMODE_BLEND);
    SDL_SetTextureColorMod(glow_,
                           static_cast<Uint8>(light.color.r * k),
                           static_cast<Uint8>(light.color.g * k),
                           static_cast<Uint8>(light.color.b * k));
    SDL_RenderCopy(gfx.raw(), glow_, nullptr, &dst);

    SDL_SetRenderDrawBlendMode(gfx.raw(), SDL_BLENDMODE_BLEND);

    const int gw = tx1 - tx0 + 1;
    const int gh = ty1 - ty0 + 1;
    shadowed_.assign(static_cast<size_t>(gw) * gh, 0);

    for (int ty = ty0; ty <= ty1; ++ty) {
        for (int tx = tx0; tx <= tx1; ++tx) {
            const Vec2 cell{ (tx + 0.5f) * t, (ty + 0.5f) * t };
            if ((cell - light.pos).length() > light.radius + t) continue;

            if (level_->isWall(tx, ty)) continue;
            if (level_->lineOfSight(light.pos, cell)) continue;

            shadowed_[static_cast<size_t>(ty - ty0) * gw + (tx - tx0)] = 1;
        }
    }

    auto isShadow = [&](int gx, int gy) -> int {
        if (gx < 0 || gy < 0 || gx >= gw || gy >= gh) return 0;
        return shadowed_[static_cast<size_t>(gy) * gw + gx];
    };

    for (int gy = 0; gy < gh; ++gy) {
        for (int gx = 0; gx < gw; ++gx) {
            if (!isShadow(gx, gy)) continue;

            const int around = isShadow(gx - 1, gy) + isShadow(gx + 1, gy) +
                               isShadow(gx, gy - 1) + isShadow(gx, gy + 1);
            const int diag = isShadow(gx - 1, gy - 1) + isShadow(gx + 1, gy - 1) +
                             isShadow(gx - 1, gy + 1) + isShadow(gx + 1, gy + 1);
            const float density = (2.f + around + diag * 0.5f) / 8.f;

            SDL_SetRenderDrawColor(gfx.raw(), 0, 0, 0,
                                   static_cast<Uint8>(std::min(1.f, density) * 255.f));
            SDL_Rect shadow{
                static_cast<int>((tx0 + gx) * t - origin.x),
                static_cast<int>((ty0 + gy) * t - origin.y),
                t, t,
            };
            SDL_RenderFillRect(gfx.raw(), &shadow);
        }
    }

    gfx.endTargetDraw();
    gfx.beginTargetDraw(target_);

    if (SDL_SetTextureBlendMode(scratch_, addPremul_) != 0) {
        SDL_SetTextureBlendMode(scratch_, SDL_BLENDMODE_ADD);
    }
    SDL_RenderCopy(gfx.raw(), scratch_, nullptr, nullptr);
    SDL_SetTextureBlendMode(glow_, SDL_BLENDMODE_ADD);
    return true;
}

void Lighting::render(Gfx& gfx) {
    if (!enabled()) return;

    const bool lit = ambient_.r < 255 || ambient_.g < 255 || ambient_.b < 255;
    if (!lit && lights_.empty()) return;

    gfx.beginTargetDraw(target_);
    SDL_SetRenderDrawBlendMode(gfx.raw(), SDL_BLENDMODE_NONE);
    SDL_SetRenderDrawColor(gfx.raw(), ambient_.r, ambient_.g, ambient_.b, 255);
    SDL_RenderClear(gfx.raw());

    const Vec2 origin = gfx.renderOrigin();
    for (const Light& l : lights_) {
        const float r = l.radius;
        SDL_Rect dst{
            static_cast<int>(l.pos.x - origin.x - r),
            static_cast<int>(l.pos.y - origin.y - r),
            static_cast<int>(r * 2.f),
            static_cast<int>(r * 2.f),
        };
        if (dst.x + dst.w < 0 || dst.y + dst.h < 0 ||
            dst.x > gfx.width() || dst.y > gfx.height()) {
            continue;
        }

        if (renderOccluded(gfx, l, dst)) continue;

        const float k = std::min(1.f, l.intensity);
        SDL_SetTextureColorMod(glow_,
                               static_cast<Uint8>(l.color.r * k),
                               static_cast<Uint8>(l.color.g * k),
                               static_cast<Uint8>(l.color.b * k));
        SDL_RenderCopy(gfx.raw(), glow_, nullptr, &dst);
    }

    gfx.endTargetDraw();

    SDL_RenderCopy(gfx.raw(), target_, nullptr, nullptr);
}

void Lighting::drawShadow(Gfx& gfx, const Vec2& worldPos, float radius, Uint8 alpha) const {
    if (!glow_ || radius <= 0.f) return;

    const Vec2 origin = gfx.renderOrigin();

    const int S = gfx.artScale();
    SDL_Rect dst{
        static_cast<int>((worldPos.x - origin.x - radius) * S),
        static_cast<int>((worldPos.y - origin.y - radius * 0.35f + radius * 0.45f) * S),
        static_cast<int>(radius * 2.f) * S,
        static_cast<int>(radius * 0.7f) * S,
    };
    if (dst.x + dst.w < 0 || dst.y + dst.h < 0 ||
        dst.x > gfx.deviceWidth() || dst.y > gfx.deviceHeight()) {
        return;
    }

    SDL_SetTextureBlendMode(glow_, SDL_BLENDMODE_BLEND);
    SDL_SetTextureColorMod(glow_, 0, 0, 0);
    SDL_SetTextureAlphaMod(glow_, alpha);
    SDL_RenderCopy(gfx.raw(), glow_, nullptr, &dst);
    SDL_SetTextureAlphaMod(glow_, 255);
    SDL_SetTextureColorMod(glow_, 255, 255, 255);
    SDL_SetTextureBlendMode(glow_, SDL_BLENDMODE_ADD);
}

float Lighting::brightnessAt(const Vec2& worldPos) const {
    float best = std::max({ ambient_.r, ambient_.g, ambient_.b }) / 255.f;

    for (const Light& l : lights_) {
        const float d = (l.pos - worldPos).length();
        if (d >= l.radius) continue;

        if (level_ && !level_->lineOfSight(l.pos, worldPos)) continue;

        const float t = d / l.radius;
        const float kSigma = sigma_;
        const float edge = std::exp(-1.f / (2.f * kSigma * kSigma));
        float a = (std::exp(-(t * t) / (2.f * kSigma * kSigma)) - edge) / (1.f - edge);
        best = std::min(1.f, best + a * std::min(1.f, l.intensity));
    }
    return best;
}

void Lighting::configure(const JsonValue& config) {
    glowSize_=std::clamp(config["glow_size"].asInt(96),4,1024);
    sigma_=std::clamp(static_cast<float>(config["sigma"].asNumber(0.42)),0.01f,1.f);
}
} // namespace scrp
