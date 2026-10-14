#pragma once
#include <algorithm>
#include <cstdint>
#include <map>
#include <vector>
namespace ircx::pattern {
struct RankedItem
{
  std::int64_t lo, hi;
  std::size_t id, owner;
  std::int64_t x0, x1;
};
struct RankedRun
{
  std::int64_t lo, hi;
  std::vector<std::size_t> ids;
};
inline std::vector<RankedRun> rankedRuns(std::int64_t lo, std::int64_t hi, const std::vector<RankedItem>& nearest_first, std::size_t rank)
{
  std::vector<RankedRun> runs{{lo, hi, {}}};
  std::map<std::size_t, RankedItem> seen;
  for (const auto& item : nearest_first) {
    seen.emplace(item.id, item);
    std::vector<RankedRun> next;
    for (const auto& r : runs) {
      const auto a = std::max(r.lo, item.lo), b = std::min(r.hi, item.hi);
      bool duplicate = false;
      for (auto id : r.ids) {
        auto old = seen.at(id);
        duplicate |= old.owner == item.owner && old.x0 == item.x0 && old.x1 == item.x1;
      }
      if (a >= b || r.ids.size() >= rank || duplicate) {
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
    if (std::all_of(runs.begin(), runs.end(), [&](const auto& r) { return r.ids.size() == rank; }))
      break;
  }
  std::vector<RankedRun> merged;
  for (auto& r : runs) {
    if (!merged.empty() && merged.back().hi == r.lo && merged.back().ids == r.ids)
      merged.back().hi = r.hi;
    else
      merged.push_back(std::move(r));
  }
  return merged;
}
}  // namespace ircx::pattern
