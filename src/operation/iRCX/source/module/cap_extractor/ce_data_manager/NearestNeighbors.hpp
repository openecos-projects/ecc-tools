#pragma once
#include <algorithm>
#include <cstdint>
#include <vector>
namespace ircx::pattern {
struct NearestItem
{
  std::int64_t lo, hi;
  std::size_t id;
  std::int64_t x0, x1;
};
struct NearestRun
{
  std::int64_t lo, hi;
  std::vector<std::size_t> ids;
};
inline std::vector<NearestRun> nearestRuns(std::int64_t lo, std::int64_t hi, const std::vector<NearestItem>& nearest_first)
{
  std::vector<NearestRun> runs{{lo, hi, {}}};
  for (const auto& item : nearest_first) {
    std::vector<NearestRun> next;
    for (const auto& r : runs) {
      const auto a = std::max(r.lo, item.lo), b = std::min(r.hi, item.hi);
      if (a >= b || !r.ids.empty()) {
        next.push_back(r);
        continue;
      }
      if (r.lo < a)
        next.push_back({r.lo, a, r.ids});
      auto ids = r.ids;
      ids.push_back(item.id);
      next.push_back({a, b, ids});
      if (b < r.hi)
        next.push_back({b, r.hi, r.ids});
    }
    runs = std::move(next);
    if (std::all_of(runs.begin(), runs.end(), [&](const auto& r) { return !r.ids.empty(); }))
      break;
  }
  std::vector<NearestRun> merged;
  for (auto& r : runs) {
    if (!merged.empty() && merged.back().hi == r.lo && merged.back().ids == r.ids)
      merged.back().hi = r.hi;
    else
      merged.push_back(std::move(r));
  }
  return merged;
}
}  // namespace ircx::pattern
