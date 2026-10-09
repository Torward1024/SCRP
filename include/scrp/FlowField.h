#pragma once
#include <cstdint>
#include <vector>
#include "scrp/Vec2.h"

#include "scrp/Grid.h"

namespace scrp {
class FlowField {
public:
    bool update(const Grid& level, const Vec2& target);

    void invalidate() { targetTx_ = -1; }

    Vec2 directionAt(const Grid& level, const Vec2& pos) const;

    int distanceAt(const Grid& level, const Vec2& pos) const;

    bool valid() const { return targetTx_ >= 0; }

    int rebuilds() const { return rebuilds_; }

private:
    static constexpr uint16_t kUnreachable = 0xFFFF;

    std::vector<uint16_t> dist_;
    int width_ = 0;
    int height_ = 0;
    int targetTx_ = -1;
    int targetTy_ = -1;
    unsigned solidRevision_ = 0;
    const Grid* grid_=nullptr;
    int tileSize_=0;
    int rebuilds_ = 0;

    void rebuild(const Grid& level, int tx, int ty);
};
} // namespace scrp
