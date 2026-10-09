#pragma once
#include <cstdint>
#include "scrp/Vec2.h"

namespace scrp {
class Rng {
public:
    explicit Rng(uint32_t seed = 0x1234567u) : state_(seed ? seed : 0x1234567u) {}

    void seed(uint32_t s) { state_ = s ? s : 0x1234567u; }

    uint32_t next() {
        state_ ^= state_ << 13;
        state_ ^= state_ >> 17;
        state_ ^= state_ << 5;
        return state_;
    }

    float unit() { return static_cast<float>(next() & 0xFFFFFF) / 16777216.f; }

    float range(float min, float max) { return min + unit() * (max - min); }

    int rangeInt(int min, int max) {
        if (max <= min) return min;
        return min + static_cast<int>(next() % static_cast<uint32_t>(max - min + 1));
    }

    bool chance(float probability) { return unit() < probability; }

    Vec2 direction() {
        float a = unit() * 6.28318530718f;
        return { std::cos(a), std::sin(a) };
    }

    Vec2 cone(const Vec2& dir, float spread) {
        float base = std::atan2(dir.y, dir.x);
        float a = base + range(-spread, spread);
        return { std::cos(a), std::sin(a) };
    }

private:
    uint32_t state_;
};

} // namespace scrp
