#include "scrp/FlowField.h"
#include "scrp/Grid.h"

#include <queue>

namespace scrp {
namespace {
constexpr int kDx[8] = { 1, -1, 0, 0,  1,  1, -1, -1 };
constexpr int kDy[8] = { 0, 0, 1, -1,  1, -1,  1, -1 };
}

bool FlowField::update(const Grid& level, const Vec2& target) {
    int tx = static_cast<int>(std::floor(target.x / level.tileSize()));
    int ty = static_cast<int>(std::floor(target.y / level.tileSize()));

    bool sameTarget = (tx == targetTx_ && ty == targetTy_);
    bool sameLevel = (grid_==&level && tileSize_==level.tileSize() && level.solidRevision() == solidRevision_ &&
                      level.widthTiles() == width_ && level.heightTiles() == height_);
    if (sameTarget && sameLevel && valid()) return false;

    rebuild(level, tx, ty);
    return true;
}

void FlowField::rebuild(const Grid& level, int tx, int ty) {
    grid_=&level; tileSize_=level.tileSize();
    width_ = level.widthTiles();
    height_ = level.heightTiles();
    targetTx_ = tx;
    targetTy_ = ty;
    solidRevision_ = level.solidRevision();
    ++rebuilds_;

    dist_.assign(static_cast<size_t>(width_) * height_, kUnreachable);
    if (tx < 0 || ty < 0 || tx >= width_ || ty >= height_) return;
    if (level.isWall(tx, ty)) return;   // a solid target has no flow field

    std::queue<int> open;
    size_t startIndex = static_cast<size_t>(ty) * width_ + tx;
    dist_[startIndex] = 0;
    open.push(static_cast<int>(startIndex));

    while (!open.empty()) {
        int index = open.front();
        open.pop();

        int x = index % width_;
        int y = index / width_;
        if(dist_[index]>=kUnreachable-1) continue;
        uint16_t next = static_cast<uint16_t>(dist_[index] + 1);

        for (int d = 0; d < 4; ++d) {     // propagate along cardinal neighbours
            int nx = x + kDx[d];
            int ny = y + kDy[d];
            if (nx < 0 || ny < 0 || nx >= width_ || ny >= height_) continue;
            if (level.isWall(nx, ny)) continue;

            size_t ni = static_cast<size_t>(ny) * width_ + nx;
            if (dist_[ni] <= next) continue;
            dist_[ni] = next;
            open.push(static_cast<int>(ni));
        }
    }
}

int FlowField::distanceAt(const Grid& level, const Vec2& pos) const {
    (void)level;
    if (!valid()) return -1;
    int x = static_cast<int>(std::floor(pos.x / level.tileSize()));
    int y = static_cast<int>(std::floor(pos.y / level.tileSize()));
    if (x < 0 || y < 0 || x >= width_ || y >= height_) return -1;
    uint16_t d = dist_[static_cast<size_t>(y) * width_ + x];
    return d == kUnreachable ? -1 : static_cast<int>(d);
}

Vec2 FlowField::directionAt(const Grid& level, const Vec2& pos) const {
    if (!valid()) return { 0.f, 0.f };

    int x = static_cast<int>(std::floor(pos.x / level.tileSize()));
    int y = static_cast<int>(std::floor(pos.y / level.tileSize()));
    if (x < 0 || y < 0 || x >= width_ || y >= height_) return { 0.f, 0.f };

    size_t here = static_cast<size_t>(y) * width_ + x;
    if (dist_[here] == kUnreachable) return { 0.f, 0.f };
    if (dist_[here] == 0) return { 0.f, 0.f };   // target reached

    int bestX = x, bestY = y;
    uint16_t best = dist_[here];

    for (int d = 0; d < 8; ++d) {
        int nx = x + kDx[d];
        int ny = y + kDy[d];
        if (nx < 0 || ny < 0 || nx >= width_ || ny >= height_) continue;
        if (level.isWall(nx, ny)) continue;

        if (d >= 4 && (level.isWall(x + kDx[d], y) || level.isWall(x, y + kDy[d]))) continue;

        uint16_t nd = dist_[static_cast<size_t>(ny) * width_ + nx];
        if (nd >= best) continue;
        best = nd;
        bestX = nx;
        bestY = ny;
    }

    if (bestX == x && bestY == y) return { 0.f, 0.f };

    Vec2 center{ (bestX + 0.5f) * level.tileSize(), (bestY + 0.5f) * level.tileSize() };
    return (center - pos).normalized();
}

} // namespace scrp
