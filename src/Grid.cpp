#include "scrp/Grid.h"
#include <algorithm>
#include <cmath>
#include <limits>
namespace scrp {
bool Grid::collides(const Vec2& pos, float radius) const {
    if(tileSize()<=0 || radius<0.f) return true;
    int minTx = static_cast<int>(std::floor((pos.x - radius) / tileSize()));
    int maxTx = static_cast<int>(std::floor((pos.x + radius) / tileSize()));
    int minTy = static_cast<int>(std::floor((pos.y - radius) / tileSize()));
    int maxTy = static_cast<int>(std::floor((pos.y + radius) / tileSize()));
    for (int ty = minTy; ty <= maxTy; ++ty) {
        for (int tx = minTx; tx <= maxTx; ++tx) {
            if (isWall(tx, ty)) return true;
        }
    }
    return false;
}

bool Grid::moveWithCollision(Vec2& pos, const Vec2& delta, float radius) const {
    bool blocked = false;

    Vec2 next = pos;
    next.x += delta.x;
    if (!collides(next, radius)) pos.x = next.x;
    else if (std::fabs(delta.x) > 0.0001f) blocked = true;

    next = pos;
    next.y += delta.y;
    if (!collides(next, radius)) pos.y = next.y;
    else if (std::fabs(delta.y) > 0.0001f) blocked = true;

    return blocked;
}

bool Grid::lineOfSight(const Vec2& from,const Vec2& to) const {
    if(tileSize()<=0) return false;
    const float cell=static_cast<float>(tileSize());
    int x=static_cast<int>(std::floor(from.x/cell)),y=static_cast<int>(std::floor(from.y/cell));
    const int endX=static_cast<int>(std::floor(to.x/cell)),endY=static_cast<int>(std::floor(to.y/cell));
    if(isWall(x,y)||isWall(endX,endY)) return false;
    const double dx=to.x-from.x,dy=to.y-from.y;
    const int sx=dx>0?1:-1,sy=dy>0?1:-1;
    const double infinity=std::numeric_limits<double>::infinity();
    double nextX=dx==0?infinity:((x+(sx>0?1:0))*cell-from.x)/dx;
    double nextY=dy==0?infinity:((y+(sy>0?1:0))*cell-from.y)/dy;
    const double deltaX=dx==0?infinity:cell/std::abs(dx),deltaY=dy==0?infinity:cell/std::abs(dy);
    while(x!=endX||y!=endY) {
        if(std::abs(nextX-nextY)<1e-10) {
            if(isWall(x+sx,y)||isWall(x,y+sy)) return false;
            x+=sx;y+=sy;nextX+=deltaX;nextY+=deltaY;
        } else if(nextX<nextY) {x+=sx;nextX+=deltaX;}
        else {y+=sy;nextY+=deltaY;}
        if(isWall(x,y)) return false;
    }
    return true;
}

bool Grid::findFreeSpotNear(const Vec2& around, float radius, int maxRings, Vec2& out) const {
    if (!collides(around, radius)) { out = around; return true; }

    int cx = static_cast<int>(std::floor(around.x / tileSize()));
    int cy = static_cast<int>(std::floor(around.y / tileSize()));
    for (int ring = 1; ring <= maxRings; ++ring) {
        for (int dy = -ring; dy <= ring; ++dy) {
            for (int dx = -ring; dx <= ring; ++dx) {
                if (std::abs(dx) != ring && std::abs(dy) != ring) continue;
                Vec2 candidate{ (cx + dx + 0.5f) * tileSize(), (cy + dy + 0.5f) * tileSize() };
                if (!collides(candidate, radius)) { out = candidate; return true; }
            }
        }
    }
    return false;
}

}
