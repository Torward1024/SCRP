#pragma once
#include <SDL.h>
#include "scrp/Color.h"
#include <string>
#include <vector>
#include "scrp/Vec2.h"
#include "scrp/Fx.h"
#include "scrp/Rng.h"
#include "scrp/Json.h"
#include <functional>

namespace scrp {
class Gfx;

class Particles {
public:
    struct Particle;
    void setRng(Rng& rng) { rng_=&rng; }
    void emitRecipe(const JsonValue& recipe,const Vec2& pos,const Vec2& dir,SDL_Color tint=Col::White,const std::string& deathTag={});
    explicit Particles(size_t capacity = 768);

    void setCapacity(size_t capacity) { capacity_=capacity; clear(); particles_.reserve(capacity); }
    void clear();
    void update(float dt, const std::function<void(const Particle&)>& onDeath = {});
    void render(Gfx& gfx) const;

    size_t count() const { return particles_.size(); }

    void burst(const Vec2& pos, int count, SDL_Color color,
               float speedMin, float speedMax,
               float lifeMin, float lifeMax,
               float size, const Vec2& dir = { 0.f, 0.f }, float spread = 3.14159f,
               const std::string& deathTag = {});

    void spawnFx(const FxDef& def, const Vec2& pos, const Vec2& dir,
                 SDL_Color tint = { 255, 255, 255, 255 });

    void renderSpritesOver(Gfx& gfx) const;

public:
    struct Particle {
        Vec2 pos;
        Vec2 vel;
        float life = 0.f;
        float maxLife = 1.f;
        float size = 1.f;
        float drag = 3.f;
        SDL_Color color{ 255, 255, 255, 255 };
        bool shrink = true;
        std::string deathTag;
    };

private:
    Rng fallbackRng_;
    Rng* rng_=&fallbackRng_;
    struct SpriteFx {
        Vec2 pos;
        SpriteId sprite;
        int frames = 1;
        float fps = 12.f;
        float time = 0.f;
        float size = 16.f;
        bool overlay = false;
        SDL_Color tint{ 255, 255, 255, 255 };
    };
    std::vector<SpriteFx> spriteFx_;

    std::vector<Particle> particles_;
    size_t capacity_;

public:
    void emit(const Particle& p);
};
} // namespace scrp
