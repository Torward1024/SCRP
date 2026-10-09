#include "scrp/Decals.h"
#include "scrp/Gfx.h"
#include "scrp/Rng.h"
#include "scrp/Color.h"
#include <algorithm>
#include <cstdio>

namespace scrp {
Decals::~Decals() { shutdown(); }

bool Decals::init(Gfx& gfx, int levelPxW, int levelPxH) {
    shutdown();
    gfx_ = &gfx;

    if (levelPxW <= 0 || levelPxH <= 0) return false;
    if (levelPxW > config_["max_dimension"].asInt(4096) || levelPxH > config_["max_dimension"].asInt(4096)) {
        std::printf("[decals] canvas %dx%d exceeds limit %d; decals disabled\n",
                    levelPxW, levelPxH, config_["max_dimension"].asInt(4096));
        return false;
    }

    target_ = gfx.createTargetTexture(levelPxW, levelPxH);
    if (!target_) {
        std::printf("[decals] cannot create decal texture: %s\n", SDL_GetError());
        return false;
    }

    width_ = levelPxW;
    height_ = levelPxH;
    stamps_ = 0;
    return true;
}

void Decals::shutdown() {
    if (target_) {
        SDL_DestroyTexture(target_);
        target_ = nullptr;
    }
    fading_.clear();
    gfx_=nullptr;
    width_ = height_ = 0;
    stamps_ = 0;
}

void Decals::clear() {
    fading_.clear();
    if (!target_ || !gfx_) return;
    gfx_->beginTargetDraw(target_);
    SDL_SetRenderDrawColor(gfx_->raw(), 0, 0, 0, 0);
    SDL_RenderClear(gfx_->raw());
    gfx_->endTargetDraw();
    stamps_ = 0;
}

void Decals::beginStamp() {
    gfx_->beginTargetDraw(target_);
    SDL_SetRenderDrawBlendMode(gfx_->raw(), SDL_BLENDMODE_BLEND);
}

void Decals::endStamp() {
    gfx_->endTargetDraw();
}

void Decals::fill(int x, int y, int w, int h, SDL_Color c) {
    if (x + w <= 0 || y + h <= 0 || x >= width_ || y >= height_) return;
    SDL_Rect r{ x, y, w, h };
    SDL_SetRenderDrawColor(gfx_->raw(), c.r, c.g, c.b, c.a);
    SDL_RenderFillRect(gfx_->raw(), &r);
}

void Decals::stampRect(const Vec2& worldPos, float w, float h, SDL_Color color) {
    if (!target_) return;
    beginStamp();
    fill(static_cast<int>(worldPos.x - w * 0.5f), static_cast<int>(worldPos.y - h * 0.5f),
         std::max(1, static_cast<int>(w)), std::max(1, static_cast<int>(h)), color);
    endStamp();
    ++stamps_;
}

void Decals::stampSplat(const Vec2& worldPos, float radius, SDL_Color color, int blobs) {
    if (!target_) return;
    beginStamp();
    for (int i = 0; i < blobs; ++i) {
        Vec2 offset = (*rng_).direction() * (*rng_).range(0.f, radius);
        int size = (*rng_).rangeInt(1, std::max(1, static_cast<int>(radius * static_cast<float>(config_["splat_size_scale"].asNumber(0.6)))));
        SDL_Color c = color;
        c.a = static_cast<Uint8>((*rng_).rangeInt(config_["splat_alpha_min"].asInt(110), config_["splat_alpha_max"].asInt(200)));
        fill(static_cast<int>(worldPos.x + offset.x) - size / 2,
             static_cast<int>(worldPos.y + offset.y) - size / 2,
             size, size, c);
    }
    endStamp();
    stamps_ += blobs;
}

void Decals::stampRecipe(const std::string& recipe,const Vec2& pos,SDL_Color tint,float size) {
    if(!target_) return;
    const auto& definition=config_["recipes"][recipe.c_str()];
    if(!definition.isObject()) return;
    if(definition.has("splat")) {
        const auto& splat=definition["splat"];
        stampSplat(pos,static_cast<float>(splat["radius_scale"].asNumber())*size,tint,splat["blobs"].asInt());
        return;
    }
    beginStamp();
    for(const auto& op:definition["rects"].array) {
        SDL_Color color=op.has("color")?readColor(op["color"]):tint;
        if(op.has("alpha")) color.a=static_cast<Uint8>(std::clamp(op["alpha"].asInt(),0,255));
        fill(static_cast<int>(pos.x)+op["x"].asInt(),static_cast<int>(pos.y)+op["y"].asInt(),
             std::max(1,op["w"].asInt(1)),std::max(1,op["h"].asInt(1)),color);
    }
    for(const auto& op:definition["scatter"].array) for(int i=0;i<op["count"].asInt();++i) {
        auto offset=rng_->direction()*rng_->range(static_cast<float>(op["radius_min"].asNumber()),static_cast<float>(op["radius_max"].asNumber()));
        auto color=tint;
        color.a=static_cast<Uint8>(rng_->rangeInt(op["alpha_min"].asInt(255),op["alpha_max"].asInt(255)));
        fill(static_cast<int>(pos.x)+static_cast<int>(offset.x),static_cast<int>(pos.y)+static_cast<int>(offset.y),1,1,color);
    }
    endStamp(); ++stamps_;
}
void Decals::stampFootprint(const Vec2& worldPos, const Vec2& dir, SDL_Color color) {
    if (!target_) return;

    Vec2 side = dir.perp();
    int x = static_cast<int>(worldPos.x);
    int y = static_cast<int>(worldPos.y);

    beginStamp();
    fill(x, y, 2, 2, color);
    fill(x + static_cast<int>(side.x), y + static_cast<int>(side.y), 1, 1,
         { color.r, color.g, color.b, static_cast<Uint8>(color.a * 2 / 3) });
    endStamp();
    ++stamps_;
}

void Decals::addFadingFootprint(const Vec2& worldPos, const Vec2& dir, SDL_Color color,
                                float life) {
    if (life <= 0.f) return;
    const size_t capacity=static_cast<size_t>(std::max(1,config_["max_fading"].asInt(900)));
    if(fading_.size()>=capacity) fading_.erase(fading_.begin());
    fading_.push_back(FadingMark{ worldPos, dir.perp(), color, life, life, 0.f });
}

void Decals::addFadingMark(const Vec2& worldPos, float radius, SDL_Color color, float life) {
    if (life <= 0.f || radius <= 0.f) return;

    const size_t kMaxMarks = static_cast<size_t>(std::max(1,config_["max_fading"].asInt(900)));
    if (fading_.size() >= kMaxMarks) {
        fading_.erase(fading_.begin());
    }
    fading_.push_back(FadingMark{ worldPos, { 0.f, 0.f }, color, life, life, radius });
}

void Decals::update(float dt) {
    for (size_t i = 0; i < fading_.size();) {
        fading_[i].life -= dt;
        if (fading_[i].life <= 0.f) {
            fading_[i] = fading_.back();
            fading_.pop_back();
        } else {
            ++i;
        }
    }
}

void Decals::renderFading(Gfx& gfx) const {
    for (const FadingMark& mark : fading_) {
        float t = mark.life / mark.maxLife;
        float k = t * t;

        SDL_Color c = mark.color;
        c.a = static_cast<Uint8>(mark.color.a * k);

        if (mark.radius > 0.f) {
            const float grow = mark.radius * (0.6f + (1.f - t) * 0.6f);
            gfx.fillRectWorld(mark.pos, grow, grow, c);
            continue;
        }

        gfx.fillRectWorld(mark.pos, 2.f, 2.f, c);
        c.a = static_cast<Uint8>(mark.color.a * k * 2.f / 3.f);
        gfx.fillRectWorld(mark.pos + mark.side, 1.f, 1.f, c);
    }
}

void Decals::render(Gfx& gfx) const {
    if (!target_) return;
    gfx.blitTargetWorld(target_, width_, height_);
}
} // namespace scrp
