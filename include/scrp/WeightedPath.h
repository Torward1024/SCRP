#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <queue>
#include <tuple>
#include <unordered_map>
#include <vector>

namespace scrp::PathFinder {

// Visit outgoing edges with emit(destination, nonnegative cost). The estimate
// must be a lower bound on the remaining cost; zero selects Dijkstra's search.
// Nodes are returned after the start, up to and including the goal. Among paths
// of equal cost the search minimizes steps, then follows neighbor emission order.
template<class Neighbors, class Estimate>
bool findWeighted(std::size_t start, std::size_t goal, Neighbors neighbors,
                  Estimate estimate, std::vector<std::size_t>& path) {
    path.clear();
    if (start == goal) return true;
    struct Record { std::uint64_t cost; std::size_t steps, parent; };
    struct Entry {
        std::uint64_t bound, cost;
        std::size_t steps, sequence, node;
        bool operator<(const Entry& other) const {
            return std::tie(bound, steps, sequence) > std::tie(other.bound, other.steps, other.sequence);
        }
    };
    constexpr auto maximum = std::numeric_limits<std::uint64_t>::max();
    std::unordered_map<std::size_t, Record> best;
    std::priority_queue<Entry> open;
    best.emplace(start, Record{0, 0, start});
    open.push({std::uint64_t(estimate(start)), 0, 0, 0, start});
    std::size_t sequence = 0;
    while (!open.empty()) {
        const auto here = open.top();
        open.pop();
        const auto& known = best.at(here.node);
        if (here.cost != known.cost || here.steps != known.steps) continue;
        if (here.node == goal) {
            for (auto node = goal; node != start; node = best.at(node).parent) path.push_back(node);
            std::reverse(path.begin(), path.end());
            return true;
        }
        neighbors(here.node, [&](std::size_t next, std::uint64_t edge) {
            if (edge > maximum - here.cost) return;
            const Record candidate{here.cost + edge, here.steps + 1, here.node};
            auto [it, inserted] = best.try_emplace(next, candidate);
            if (!inserted) {
                if (std::tie(it->second.cost, it->second.steps) <= std::tie(candidate.cost, candidate.steps)) return;
                it->second = candidate;
            }
            const auto h = std::uint64_t(estimate(next));
            open.push({candidate.cost + std::min(h, maximum - candidate.cost), candidate.cost,
                       candidate.steps, ++sequence, next});
        });
    }
    return false;
}
} // namespace scrp::PathFinder
