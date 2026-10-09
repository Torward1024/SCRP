#include "scrp/Anim.h"
#include <cmath>

namespace scrp {
namespace {
constexpr float kPi = 3.14159265358979f;

} // namespace

Facing8 facingFromVec(const Vec2& dir, Facing8 fallback) {
    if (dir.lengthSq() < 0.0001f) return fallback;

    float angle = std::atan2(dir.y, dir.x);           // -pi..pi
    if (angle < 0.f) angle += 2.f * kPi;              // 0..2pi

    int sector = static_cast<int>((angle + kPi / 8.f) / (kPi / 4.f)) % 8;

    static const Facing8 kMap[8] = {
        Facing8::E, Facing8::SE, Facing8::S, Facing8::SW,
        Facing8::W, Facing8::NW, Facing8::N, Facing8::NE
    };
    return kMap[sector];
}

Vec2 vecFromFacing(Facing8 f) {
    constexpr float d = 0.70710678f;
    switch (f) {
        case Facing8::S:  return {  0.f,  1.f };
        case Facing8::SW: return {   -d,    d };
        case Facing8::W:  return { -1.f,  0.f };
        case Facing8::NW: return {   -d,   -d };
        case Facing8::N:  return {  0.f, -1.f };
        case Facing8::NE: return {    d,   -d };
        case Facing8::E:  return {  1.f,  0.f };
        case Facing8::SE: return {    d,    d };
        default:          return {  0.f,  1.f };
    }
}

} // namespace scrp
