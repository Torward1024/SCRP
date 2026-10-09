#pragma once
#include <vector>
#include "scrp/Vec2.h"

#include "scrp/Grid.h"

namespace scrp {
namespace PathFinder {

bool find(const Grid& level, const Vec2& from, const Vec2& to,
          std::vector<Vec2>& out, int nodeBudget);

bool findCover(const Grid& level, const Vec2& from, const Vec2& threat,
               float searchRadius, Vec2& out);

} // namespace PathFinder
} // namespace scrp
