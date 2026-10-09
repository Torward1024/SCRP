#pragma once
#include <SDL.h>
#include "scrp/Grid.h"
#include "scrp/Json.h"
#include <vector>
#include "scrp/Vec2.h"

namespace scrp {
class Gfx;

class Lighting {
public:
    Lighting() = default;
    Lighting(const Lighting&) = delete;
    Lighting& operator=(const Lighting&) = delete;
    struct Light {
        Vec2 pos;
        float radius = 0.f;
        SDL_Color color{ 255, 255, 255, 255 };
        float intensity = 1.f;
    };

    void configure(const JsonValue& config);
    bool init(Gfx& gfx);
    void shutdown();

    bool enabled() const { return target_ != nullptr && !disabled_; }
    void setEnabled(bool on) { disabled_ = !on; }

    void setLevel(const Grid* level) { level_ = level; }

    void setAmbient(SDL_Color c) { ambient_ = c; }
    SDL_Color ambient() const { return ambient_; }

    void begin();
    void add(const Light& light);
    void render(Gfx& gfx);

    float brightnessAt(const Vec2& worldPos) const;

    void drawShadow(Gfx& gfx, const Vec2& worldPos, float radius, Uint8 alpha) const;

    int lightCount() const { return static_cast<int>(lights_.size()); }

private:
    SDL_Texture* target_ = nullptr;   // light map matching the logical frame
    SDL_Texture* glow_ = nullptr;     // radial gradient built during initialization

    SDL_Texture* scratch_ = nullptr;
    SDL_BlendMode addPremul_ = SDL_BLENDMODE_ADD;
    const Grid* level_ = nullptr;

    std::vector<uint8_t> shadowed_;
    SDL_Color ambient_{ 255, 255, 255, 255 };
    std::vector<Light> lights_;
    int glowSize_=96;
    float sigma_=0.42f;
    bool disabled_ = false;

    bool buildGlow(Gfx& gfx);

    bool renderOccluded(Gfx& gfx, const Light& light, const SDL_Rect& dst);
};
} // namespace scrp
