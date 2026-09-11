#pragma once

#include "PlanarCoord.hpp"
#include "RTHeader.hpp"
#include "TBTask.hpp"

namespace irt {

class PRTopologyCostCache
{
 public:
  explicit PRTopologyCostCache(TBSegmentCostQuery cost_query) : _cost_query(std::move(cost_query)) {}

  double getCost(const PlanarCoord& first, const PlanarCoord& second)
  {
    if (first == second) {
      return 0;
    }
    if (first.get_x() != second.get_x() && first.get_y() != second.get_y()) {
      return std::numeric_limits<double>::infinity();
    }
    int64_t span = std::abs(static_cast<int64_t>(first.get_x()) - second.get_x()) + std::abs(static_cast<int64_t>(first.get_y()) - second.get_y());
    if (span == 1) {
      return _cost_query(first, second);
    }

    PRSegmentKey key{std::min(first.get_x(), second.get_x()), std::min(first.get_y(), second.get_y()), std::max(first.get_x(), second.get_x()),
                     std::max(first.get_y(), second.get_y())};
    if (auto iter = _segment_cost_map.find(key); iter != _segment_cost_map.end()) {
      return iter->second;
    }

    double cost = _cost_query(first, second);
    _segment_cost_map.emplace(key, cost);
    return cost;
  }

 private:
  struct PRSegmentKey
  {
    int32_t ll_x;
    int32_t ll_y;
    int32_t ur_x;
    int32_t ur_y;

    bool operator==(const PRSegmentKey&) const = default;
  };

  struct PRSegmentKeyHash
  {
    size_t operator()(const PRSegmentKey& key) const
    {
      size_t seed = 0;
      for (int32_t value : {key.ll_x, key.ll_y, key.ur_x, key.ur_y}) {
        seed ^= std::hash<int32_t>{}(value) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
      }
      return seed;
    }
  };

  TBSegmentCostQuery _cost_query;
  std::unordered_map<PRSegmentKey, double, PRSegmentKeyHash> _segment_cost_map;
};

}  // namespace irt
