#pragma once
#include "CrossLayerOverlap.hpp"
namespace ircx::pattern {
inline std::pair<int32_t, int32_t> crossLayers(const std::vector<CrossLayerOverlap>& overlaps, int32_t lo, int32_t hi)
{
  int32_t below = 0, above = -1;
  for (const auto& overlap : overlaps) {
    if (overlap.get_start_coord() > lo || overlap.get_end_coord() < hi)
      continue;
    const int32_t b = overlap.get_below_layer_idx(), a = overlap.get_above_layer_idx();
    if (b > 0 && b > below)
      below = b;
    if (a > 0 && (above < 0 || a < above))
      above = a;
  }
  return {below, above};
}
}  // namespace ircx::pattern
