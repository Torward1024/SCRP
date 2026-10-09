#pragma once
#include <SDL.h>
#include "scrp/Color.h"
#include <vector>
#include "scrp/Vec2.h"
#include "scrp/Json.h"
#include "scrp/Rng.h"

namespace scrp {
class Gfx;

class Decals {
public:
    Decals() = default;
    Decals(const Decals&) = delete;
    Decals& operator=(const Decals&) = delete;
    void configure(const JsonValue& config) { config_=config; }
    void setRng(Rng& rng) { rng_=&rng; }
    void stampRecipe(const std::string& recipe,const Vec2& pos,SDL_Color tint,float size=1.f);

    ~Decals();

    bool init(Gfx& gfx, int levelPxW, int levelPxH);
    void shutdown();
    void clear();

    bool ready() const { return target_ != nullptr; }
    int stampCount() const { return stamps_; }

    void stampRect(const Vec2& worldPos, float w, float h, SDL_Color color);

    void stampSplat(const Vec2& worldPos, float radius, SDL_Color color, int blobs = 5);

    void stampFootprint(const Vec2& worldPos, const Vec2& dir, SDL_Color color);

    void addFadingFootprint(const Vec2& worldPos, const Vec2& dir, SDL_Color color, float life);

    void addFadingMark(const Vec2& worldPos, float radius, SDL_Color color, float life);

    void update(float dt);
    void render(Gfx& gfx) const;

    void renderFading(Gfx& gfx) const;

    int fadingCount() const { return static_cast<int>(fading_.size()); }

private:
    struct FadingMark {
        Vec2 pos;
        Vec2 side;
        SDL_Color color;
        float life = 0.f;
        float maxLife = 1.f;
        float radius = 0.f;   // zero selects a footprint; positive values select a round mark
    };
    std::vector<FadingMark> fading_;

    JsonValue config_;
    Rng fallbackRng_;
    Rng* rng_=&fallbackRng_;
    Gfx* gfx_ = nullptr;
    SDL_Texture* target_ = nullptr;
    int width_ = 0;
    int height_ = 0;
    int stamps_ = 0;

    void beginStamp();
    void endStamp();
    void fill(int x, int y, int w, int h, SDL_Color c);
};
} // namespace scrp
