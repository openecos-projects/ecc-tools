#include "TimingConstraint.hpp"
namespace ipw {
namespace {
std::set<TransType> uncertaintyEdges(TransType edge)
{
  return edge == TransType::kNone ? std::set{TransType::kRise, TransType::kFall} : std::set{edge};
}
TransType uncertaintyEdge(const std::set<TransType>& edges)
{
  return edges.size() == 2 ? TransType::kNone : *edges.begin();
}
std::vector<TimingClockUncertainty> subtractUncertainty(
    const TimingClockUncertainty& existing,
    const TimingClockUncertainty& filter
)
{
  // Subtract a rectangle one axis at a time. At most five remainders per
  // record are needed; clock sets are never expanded into a Cartesian product.
  TimingClockUncertainty overlap = existing;
  std::vector<TimingClockUncertainty> result;
  const auto split = [&](const auto& selected, auto getter, auto setter) {
    const auto values = getter(overlap);
    auto common = values;
    std::erase_if(common, [&](const auto& value) { return !selected.contains(value); });
    if (common.empty())
      return false;
    auto remainder = values;
    std::erase_if(remainder, [&](const auto& value) { return common.contains(value); });
    if (!remainder.empty()) {
      auto piece = overlap;
      setter(piece, remainder);
      result.push_back(std::move(piece));
    }
    setter(overlap, common);
    return true;
  };
  const auto checks = [](const TimingClockUncertainty& u) {
    std::set<AnalysisType> values;
    if (u.get_setup())
      values.insert(AnalysisType::kMax);
    if (u.get_hold())
      values.insert(AnalysisType::kMin);
    return values;
  };
  if (!split(
          filter.get_from_clocks(),
          [](const auto& u) { return u.get_from_clocks(); },
          [](auto& u, const auto& v) { u.set_from_clocks(v); }
      )
      || !split(
          filter.get_to_clocks(),
          [](const auto& u) { return u.get_to_clocks(); },
          [](auto& u, const auto& v) { u.set_to_clocks(v); }
      )
      || !split(
          uncertaintyEdges(filter.get_from_trans_type()),
          [](const auto& u) { return uncertaintyEdges(u.get_from_trans_type()); },
          [](auto& u, const auto& v) { u.set_from_trans_type(uncertaintyEdge(v)); }
      )
      || !split(
          uncertaintyEdges(filter.get_to_trans_type()),
          [](const auto& u) { return uncertaintyEdges(u.get_to_trans_type()); },
          [](auto& u, const auto& v) { u.set_to_trans_type(uncertaintyEdge(v)); }
      )
      || !split(checks(filter), checks, [](auto& u, const auto& v) {
           u.set_setup(v.contains(AnalysisType::kMax));
           u.set_hold(v.contains(AnalysisType::kMin));
         }))
    return {existing};
  return result;
}
}  // namespace
void TimingConstraint::removeClockUncertainty(const TimingClockUncertainty& filter)
{
  std::vector<TimingClockUncertainty> updated;
  for (const auto& existing : _clock_uncertainty_list) {
    auto remaining = subtractUncertainty(existing, filter);
    updated.insert(updated.end(), std::make_move_iterator(remaining.begin()), std::make_move_iterator(remaining.end()));
  }
  _clock_uncertainty_list.swap(updated);
}
void TimingConstraint::setClockUncertainty(const TimingClockUncertainty& uncertainty)
{
  TimingConstraint updated;
  updated._clock_uncertainty_list = _clock_uncertainty_list;
  updated.removeClockUncertainty(uncertainty);
  updated._clock_uncertainty_list.push_back(uncertainty);
  _clock_uncertainty_list.swap(updated._clock_uncertainty_list);
}
}  // namespace ipw
