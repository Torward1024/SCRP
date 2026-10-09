#include "scrp/Particles.h"

#include "scrp/Gfx.h"
#include "scrp/Rng.h"
#include "scrp/Color.h"
#include <algorithm>

namespace scrp {
Particles::Particles(size_t capacity) : capacity_(capacity) {
    particles_.reserve(capacity_);
}

void Particles::clear() {
    particles_.clear();
    spriteFx_.clear();
}

void Particles::emit(const Particle& p) {
    if(capacity_==0 || p.maxLife<=0.f) return;
    if (particles_.size() >= capacity_) {
        particles_[0] = p;
        return;
    }
    particles_.push_back(p);
}

void Particles::update(float dt, const std::function<void(const Particle&)>& onDeath) {
    for (size_t i = 0; i < spriteFx_.size();) {
        SpriteFx& s = spriteFx_[i];
        s.time += dt;
        if (s.time * s.fps >= static_cast<float>(s.frames)) {
            s = spriteFx_.back();
            spriteFx_.pop_back();
        } else {
            ++i;
        }
    }

    for (size_t i = 0; i < particles_.size();) {
        Particle& p = particles_[i];
        p.life -= dt;
        if (p.life <= 0.f) {
            if(onDeath && !p.deathTag.empty()) onDeath(p);
            particles_[i] = particles_.back();
            particles_.pop_back();
            continue;
        }
        p.pos += p.vel * dt;
        p.vel -= p.vel * std::min(p.drag * dt, 1.f);
        ++i;
    }
}

namespace {
void drawSpriteFxFrame(Gfx& gfx, const Vec2& pos, SpriteId sprite, int frames,
                       float fps, float time, float size, SDL_Color tint) {
    int frame = std::min(frames - 1, static_cast<int>(time * fps));
    gfx.drawSprite(sprite, pos, frame, 0, false, tint, size);
}
}

void Particles::renderSpritesOver(Gfx& gfx) const {
    for (const SpriteFx& s : spriteFx_) {
        if (!s.overlay) continue;
        drawSpriteFxFrame(gfx, s.pos, s.sprite, s.frames, s.fps, s.time, s.size, s.tint);
    }
}

void Particles::render(Gfx& gfx) const {
    for (const SpriteFx& s : spriteFx_) {
        if (s.overlay) continue;
        drawSpriteFxFrame(gfx, s.pos, s.sprite, s.frames, s.fps, s.time, s.size, s.tint);
    }
    for (const Particle& p : particles_) {
        float t = p.life / p.maxLife;              // 1 -> 0
        float size = p.shrink ? std::max(1.f, p.size * t) : p.size;
        SDL_Color c = p.color;
        c.a = static_cast<Uint8>(std::clamp(t * static_cast<float>(p.color.a), 0.f, 255.f));
        gfx.fillPixelWorld(p.pos, size, c);
    }
}

void Particles::spawnFx(const FxDef& def, const Vec2& pos, const Vec2& dir, SDL_Color tint) {
    if (def.sprite.valid()) {
        SpriteFx s;
        s.pos = pos;
        s.sprite = def.sprite;
        s.frames = def.spriteFrames;
        s.fps = def.spriteFps;
        s.size = def.spriteSize;
        s.overlay = def.spriteOverlay;
        s.tint = tint;
        spriteFx_.push_back(s);
    }

    for (const FxEmitterDef& e : def.emitters) {
        Vec2 emitDir = e.directed ? dir : Vec2{ 0.f, 0.f };
        burst(pos, e.count, e.useTint ? tint : e.color, e.speedMin, e.speedMax,
              e.lifeMin, e.lifeMax, e.size, emitDir, e.spread);
    }
}

void Particles::burst(const Vec2& pos, int count, SDL_Color color,
                      float speedMin, float speedMax,
                      float lifeMin, float lifeMax,
                      float size, const Vec2& dir, float spread,
                      const std::string& deathTag) {
    bool directed = dir.lengthSq() > 0.0001f;
    for (int i = 0; i < count; ++i) {
        Particle p;
        p.pos = pos;
        Vec2 d = directed ? (*rng_).cone(dir, spread) : (*rng_).direction();
        p.vel = d * (*rng_).range(speedMin, speedMax);
        p.maxLife = (*rng_).range(lifeMin, lifeMax);
        p.life = p.maxLife;
        p.size = size;
        p.color = color;
        p.deathTag = deathTag;
        emit(p);
    }
}

void Particles::emitRecipe(const JsonValue& recipe,const Vec2& pos,const Vec2& dir,SDL_Color tint,const std::string& deathTag) {
    if(!recipe.isObject()) return;
    const auto& speed=recipe["speed"]; const auto& life=recipe["life"];
    const auto color=recipe["use_tint"].asBool()?tint:readColor(recipe["color"],tint);
    for(int i=0;i<recipe["count"].asInt(1);++i) {
        Particle particle; particle.pos=pos;
        Vec2 direction=dir;
        if(recipe["perpendicular"].asBool()) direction=dir.perp()*(rng_->chance(0.5f)?1.f:-1.f);
        const float spread=static_cast<float>(recipe["spread"].asNumber());
        direction=direction.lengthSq()>0.0001f?rng_->cone(direction,spread):rng_->direction();
        particle.vel=direction*rng_->range(static_cast<float>(speed.at(0).asNumber()),static_cast<float>(speed.at(1).asNumber()));
        particle.maxLife=rng_->range(static_cast<float>(life.at(0).asNumber()),static_cast<float>(life.at(1).asNumber()));
        particle.life=particle.maxLife; particle.color=color; particle.size=static_cast<float>(recipe["size"].asNumber(1));
        particle.drag=static_cast<float>(recipe["drag"].asNumber(3)); particle.shrink=recipe["shrink"].asBool(true);
        particle.deathTag=deathTag; emit(particle);
    }
}
} // namespace scrp
