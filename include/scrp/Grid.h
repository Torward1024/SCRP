#pragma once
#include "Vec2.h"
namespace scrp {
class Grid {
public:
    virtual ~Grid() = default;
    virtual int widthTiles() const = 0;
    virtual int heightTiles() const = 0;
    virtual int tileSize() const = 0;
    virtual bool isWall(int x, int y) const = 0;
    virtual unsigned solidRevision() const = 0;
    bool collides(const Vec2& pos, float radius) const;
    bool moveWithCollision(Vec2& pos, const Vec2& delta, float radius) const;
    bool lineOfSight(const Vec2& from, const Vec2& to) const;
    bool findFreeSpotNear(const Vec2& around, float radius, int maxRings, Vec2& out) const;
};
}
