#include "scrp/PathFinder.h"
#include "scrp/Grid.h"
#include <algorithm>
#include <cmath>
#include <functional>
#include <queue>
#include <vector>

namespace scrp {
namespace {

constexpr int kDx[8] = { 1, -1, 0, 0,  1,  1, -1, -1 };
constexpr int kDy[8] = { 0, 0, 1, -1,  1, -1,  1, -1 };

constexpr int kCostStraight = 10;
constexpr int kCostDiagonal = 14;

struct Node {
    int f;
    int index;
    bool operator>(const Node& o) const {
        return f != o.f ? f > o.f : index > o.index;
    }
};

int octile(int ax, int ay, int bx, int by) {
    int dx = std::abs(ax - bx);
    int dy = std::abs(ay - by);
    return kCostStraight * (dx + dy) + (kCostDiagonal - 2 * kCostStraight) * std::min(dx, dy);
}

Vec2 tileCenter(const Grid& level, int tx, int ty) {
    return { (tx + 0.5f) * level.tileSize(), (ty + 0.5f) * level.tileSize() };
}

bool passable(const Grid& level, int tx, int ty) {
    return tx >= 0 && ty >= 0 && tx < level.widthTiles() && ty < level.heightTiles() &&
           !level.isWall(tx, ty);
}

bool diagonalOpen(const Grid& level, int x, int y, int d) {
    return d < 4 || (!level.isWall(x + kDx[d], y) && !level.isWall(x, y + kDy[d]));
}

} // namespace

bool PathFinder::find(const Grid& level, const Vec2& from, const Vec2& to,
                      std::vector<Vec2>& out, int nodeBudget) {
    out.clear();

    const int w = level.widthTiles();
    const int h = level.heightTiles();
    if (w <= 0 || h <= 0) return false;

    const int sx = static_cast<int>(std::floor(from.x / level.tileSize()));
    const int sy = static_cast<int>(std::floor(from.y / level.tileSize()));
    int gx = static_cast<int>(std::floor(to.x / level.tileSize()));
    int gy = static_cast<int>(std::floor(to.y / level.tileSize()));

    if (!passable(level, sx, sy) || !passable(level, gx, gy)) return false;
    if (sx == gx && sy == gy) return false;   // уже на месте, маршрут не нужен

    const size_t cells = static_cast<size_t>(w) * h;
    std::vector<int> gScore(cells, -1);
    std::vector<int> cameFrom(cells, -1);
    std::vector<bool> closed(cells, false);

    std::priority_queue<Node, std::vector<Node>, std::greater<Node>> open;
    const int start = sy * w + sx;
    const int goal = gy * w + gx;
    gScore[static_cast<size_t>(start)] = 0;
    open.push({ octile(sx, sy, gx, gy), start });

    int expanded = 0;
    bool found = false;

    while (!open.empty()) {
        const int index = open.top().index;
        open.pop();
        if (closed[static_cast<size_t>(index)]) continue;
        closed[static_cast<size_t>(index)] = true;

        if (index == goal) { found = true; break; }

        if (++expanded > nodeBudget) break;

        const int x = index % w;
        const int y = index / w;

        for (int d = 0; d < 8; ++d) {
            const int nx = x + kDx[d];
            const int ny = y + kDy[d];
            if (!passable(level, nx, ny)) continue;
            if (!diagonalOpen(level, x, y, d)) continue;

            const size_t ni = static_cast<size_t>(ny) * w + nx;
            if (closed[ni]) continue;

            const int step = d < 4 ? kCostStraight : kCostDiagonal;
            const int tentative = gScore[static_cast<size_t>(index)] + step;
            if (gScore[ni] >= 0 && tentative >= gScore[ni]) continue;

            gScore[ni] = tentative;
            cameFrom[ni] = index;
            open.push({ tentative + octile(nx, ny, gx, gy), static_cast<int>(ni) });
        }
    }

    if (!found) return false;

    std::vector<Vec2> reversed;
    for (int at = goal; at != start && at >= 0; at = cameFrom[static_cast<size_t>(at)]) {
        reversed.push_back(tileCenter(level, at % w, at / w));
    }
    if (reversed.empty()) return false;

    const std::vector<Vec2> path(reversed.rbegin(), reversed.rend());
    Vec2 anchor = from;
    for (size_t i = 0; i + 1 < path.size(); ++i) {
        if (level.lineOfSight(anchor, path[i + 1])) continue;
        out.push_back(path[i]);
        anchor = path[i];
    }
    out.push_back(path.back());   // цель остаётся всегда
    return true;
}

bool PathFinder::findCover(const Grid& level, const Vec2& from, const Vec2& threat,
                           float searchRadius, Vec2& out) {
    const int cx = static_cast<int>(std::floor(from.x / level.tileSize()));
    const int cy = static_cast<int>(std::floor(from.y / level.tileSize()));
    const int radius = std::max(1, static_cast<int>(searchRadius) / level.tileSize());
    const float threatDistNow = (from - threat).length();

    for (int r = 1; r <= radius; ++r) {
        for (int dy = -r; dy <= r; ++dy) {
            for (int dx = -r; dx <= r; ++dx) {
                if (std::max(std::abs(dx), std::abs(dy)) != r) continue;

                const int tx = cx + dx;
                const int ty = cy + dy;
                if (!passable(level, tx, ty)) continue;

                const Vec2 spot = tileCenter(level, tx, ty);

                if ((spot - threat).length() < threatDistNow) continue;

                if (level.lineOfSight(spot, threat)) continue;

                if (!level.lineOfSight(from, spot)) continue;

                out = spot;
                return true;
            }
        }
    }
    return false;
}
} // namespace scrp
