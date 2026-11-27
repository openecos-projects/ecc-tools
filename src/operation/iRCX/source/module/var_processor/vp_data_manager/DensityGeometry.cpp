#include "DensityGeometry.hpp"

#include <algorithm>
#include <cmath>
#include <numeric>

#include "ProcessSpatialEvaluator.hpp"

namespace ircx {
namespace {
ProcessResult<std::pair<double, double>> endpointDensity(const std::vector<DensitySpan>& profile, double start, double end)
{
  const auto it
      = std::lower_bound(profile.begin(), profile.end(), std::midpoint(start, end), [](const DensitySpan& span, double value) { return span.end < value; });
  if (it == profile.end() || it->start > start || it->end < end)
    return std::string("density profile does not cover the physical wire segment");
  auto at = [&](double position) { return std::lerp(it->density_start, it->density_end, (position - it->start) / (it->end - it->start)); };
  return std::pair{at(start), at(end)};
}
}  // namespace

ProcessResult<std::vector<EffectiveWireSegment>> finalizeDensityGeometry(const WireGeometryInput& wire, std::span<const EffectiveWireSegment> etched,
                                                                         bool horizontal, const MetalDensity* drawn, const MetalDensity* fabricated,
                                                                         ProcessEffectType effect)
{
  if (!wire.conductor || etched.empty())
    return std::string("missing wire or etch snapshot for density finalization");
  std::vector<EffectiveWireSegment> result;
  for (const auto& segment : etched) {
    std::vector<DensitySpan> drawn_profile, fabricated_profile;
    std::vector<double> boundaries{segment.start, segment.end};
    for (bool silicon : {false, true}) {
      const auto* index = silicon ? fabricated : drawn;
      if (!index || !wire.conductor->get_thickness_polynomial())
        continue;
      const double center = wire.center + (silicon ? segment.geometry.center_shift : 0);
      auto profile = index->profile(horizontal, center, segment.start, segment.end, wire.conductor->get_thickness_polynomial()->density_window);
      if (!profile)
        return profile.error;
      for (const auto& span : *profile) {
        boundaries.push_back(span.start);
        boundaries.push_back(span.end);
      }
      (silicon ? fabricated_profile : drawn_profile) = std::move(*profile);
    }
    std::sort(boundaries.begin(), boundaries.end());
    boundaries.erase(std::unique(boundaries.begin(), boundaries.end()), boundaries.end());
    for (std::size_t i = 1; i < boundaries.size(); ++i) {
      auto piece = segment;
      piece.start = boundaries[i - 1];
      piece.end = boundaries[i];
      for (bool silicon : {false, true}) {
        const auto& profile = silicon ? fabricated_profile : drawn_profile;
        if (profile.empty())
          continue;
        const auto densities = endpointDensity(profile, piece.start, piece.end);
        if (!densities)
          return densities.error;
        (silicon ? piece.fabricated_density : piece.drawn_density) = *densities;
      }
      // The stored sample is for constant-geometry consumers. Spatial consumers
      // reevaluate the polynomial from the retained density function.
      auto geometry = evaluateSegmentGeometry(*wire.conductor, {wire.width}, piece, std::midpoint(piece.start, piece.end), effect);
      if (!geometry)
        return geometry.error;
      piece.geometry = *geometry;
      result.push_back(std::move(piece));
    }
  }
  return result;
}
}  // namespace ircx
