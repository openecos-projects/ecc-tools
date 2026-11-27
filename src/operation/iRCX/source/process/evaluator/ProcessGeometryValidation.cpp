#include <algorithm>
#include <cmath>
#include <numeric>

#include "ProcessEvaluator.hpp"
#include "ProcessSpatialEvaluator.hpp"

namespace ircx {
namespace {
bool positive(double value)
{
  return std::isfinite(value) && value > 0;
}

ProcessResult<void, std::string> validateInputs(std::span<const WireGeometryInput> wires, const WireGeometryProfiles& profiles)
{
  if (wires.size() != profiles.size())
    return std::string("wire geometry profile count mismatch");
  for (std::size_t i = 0; i < wires.size(); ++i) {
    const auto& wire = wires[i];
    if (!wire.conductor || !positive(wire.width) || !std::isfinite(wire.center) || wire.spans.empty())
      return std::string("invalid wire geometry input");
    double previous = -std::numeric_limits<double>::infinity();
    for (const auto& span : wire.spans) {
      if (!std::isfinite(span.start) || !std::isfinite(span.end) || span.start >= span.end || span.start < previous)
        return std::string("invalid or unsorted wire span");
      previous = span.end;
      for (auto neighbor : {span.lower_neighbor, span.upper_neighbor})
        if (neighbor != kNoWireNeighbor && (neighbor >= wires.size() || neighbor == i || wires[neighbor].conductor != wire.conductor))
          return std::string("invalid neighbor index or mismatched process layer");
    }
    std::size_t source = 0;
    double position = wire.spans.front().start;
    for (const auto& segment : profiles[i]) {
      if (source >= wire.spans.size() || segment.source_span != source || segment.start != position || !std::isfinite(segment.end)
          || segment.end <= segment.start || segment.end > wire.spans[source].end)
        return std::string("geometry profile does not cover its source spans in order");
      position = segment.end;
      if (position == wire.spans[source].end && ++source < wire.spans.size())
        position = wire.spans[source].start;
    }
    if (source != wire.spans.size())
      return std::string("incomplete wire geometry profile");
  }
  return {};
}

double combinedWidth(const EffectiveWireGeometry& a, const EffectiveWireGeometry& b, double ta, double tb)
{
  const double bottom = a.width + b.width - a.side_tangent * ta - b.side_tangent * tb;
  const double top = bottom + 2 * std::min(ta, tb) * (a.side_tangent + b.side_tangent);
  return std::max(bottom, top);
}

ProcessResult<void, std::string> checkPair(const WireGeometryInput& a, const WireGeometryInput& b, const EffectiveWireSegment& sa,
                                           const EffectiveWireSegment& sb, double distance, double start, double end, ProcessEffectType effect)
{
  auto at = [&](const WireGeometryInput& wire, const EffectiveWireSegment& segment, double position) {
    return evaluateSegmentGeometry(*wire.conductor, {wire.width}, segment, position, effect);
  };
  auto curvature = [&](const WireGeometryInput& wire, const EffectiveWireSegment& segment) -> ProcessResult<double> {
    const auto& model = wire.conductor->get_thickness_polynomial();
    if (!model)
      return 0.0;
    const auto& density = model->density_basis == GeometryBasis::kDrawn ? segment.drawn_density : segment.fabricated_density;
    if (!density)
      return 0.0;  // A required missing density was rejected by the breakpoint pass.
    const auto first = at(wire, segment, start), last = at(wire, segment, end);
    if (!first)
      return first.error;
    if (!last)
      return last.error;
    if (first->thickness == last->thickness)
      return 0.0;  // Monotone and therefore constant on this piece.
    const double d0 = std::lerp(density->first, density->second, (start - segment.start) / (segment.end - segment.start));
    const double d1 = std::lerp(density->first, density->second, (end - segment.start) / (segment.end - segment.start));
    return thicknessCurvatureBound(*model, wire.conductor->get_thickness(), {wire.width}, segment.geometry, d0, d1);
  };
  const auto ca = curvature(a, sa), cb = curvature(b, sb);
  if (!ca)
    return ca.error;
  if (!cb)
    return cb.error;
  auto check = [&](auto&& self, double lo, double hi, int depth) -> ProcessResult<void, std::string> {
    const auto a0 = at(a, sa, lo), a1 = at(a, sa, hi), b0 = at(b, sb, lo), b1 = at(b, sb, hi);
    if (!a0)
      return a0.error;
    if (!a1)
      return a1.error;
    if (!b0)
      return b0.error;
    if (!b1)
      return b1.error;
    const auto [amin, amax] = std::minmax(a0->thickness, a1->thickness);
    const auto [bmin, bmax] = std::minmax(b0->thickness, b1->thickness);
    // Thickness is monotone on this interval. The combined sidewall width is
    // piecewise linear in both thicknesses, with its only crease at ta == tb.
    double upper = 0;
    for (double ta : {amin, amax})
      for (double tb : {bmin, bmax})
        upper = std::max(upper, combinedWidth(*a0, *b0, ta, tb));
    const double diagonal_lo = std::max(amin, bmin), diagonal_hi = std::min(amax, bmax);
    if (diagonal_lo <= diagonal_hi)
      for (double t : {diagonal_lo, diagonal_hi})
        upper = std::max(upper, combinedWidth(*a0, *b0, t, t));
    // Also bound the joint path, preserving correlation between both wires.
    // Linear interpolation error is at most max(|t''|)*h^2/8. In particular,
    // affine thicknesses admit exact clearance at a non-dyadic contact point.
    double joint = std::max(combinedWidth(*a0, *b0, a0->thickness, b0->thickness), combinedWidth(*a1, *b1, a1->thickness, b1->thickness));
    const double difference0 = a0->thickness - b0->thickness, difference1 = a1->thickness - b1->thickness;
    if (difference0 * difference1 < 0) {
      const double ratio = difference0 / (difference0 - difference1);
      const double t = std::lerp(a0->thickness, a1->thickness, ratio);
      joint = std::max(joint, combinedWidth(*a0, *b0, t, t));
    }
    const double fraction = (hi - lo) / (end - start), sum = 2 * std::abs(a0->side_tangent + b0->side_tangent);
    joint += ((*ca) * (std::abs(a0->side_tangent) + sum) + (*cb) * (std::abs(b0->side_tangent) + sum)) * fraction * fraction / 8;
    upper = std::min(upper, joint);
    if (distance - upper / 2 >= -1e-12)
      return {};
    const double mid = std::midpoint(lo, hi);
    for (double position : {lo, mid, hi}) {
      const auto ga = at(a, sa, position), gb = at(b, sb, position);
      if (!ga)
        return ga.error;
      if (!gb)
        return gb.error;
      if (distance - combinedWidth(*ga, *gb, ga->thickness, gb->thickness) / 2 < -1e-12)
        return "final sidewalls overlap on " + a.conductor->get_layer_name();
    }
    if (depth == 0 || mid == lo || mid == hi)
      return std::string("sidewall clearance validation did not converge");
    const auto first = self(self, lo, mid, depth - 1);
    if (!first)
      return first.error;
    return self(self, mid, hi, depth - 1);
  };
  return check(check, start, end, 64);
}
}  // namespace

ProcessResult<void, std::string> validateWireGeometryProfiles(std::span<const WireGeometryInput> wires, const WireGeometryProfiles& profiles,
                                                              ProcessEffectType effect)
{
  if (effect != ProcessEffectType::kResistance && effect != ProcessEffectType::kCapacitance)
    return std::string("geometry validation requires one effect domain");
  const auto valid = validateInputs(wires, profiles);
  if (!valid)
    return valid.error;
  std::vector<std::vector<std::vector<double>>> breakpoints(wires.size());
  for (std::size_t i = 0; i < wires.size(); ++i)
    for (const auto& segment : profiles[i]) {
      auto points = segmentGeometryBreakpoints(*wires[i].conductor, {wires[i].width}, segment, effect);
      if (!points)
        return points.error;
      breakpoints[i].push_back(std::move(*points));
    }
  for (std::size_t i = 0; i < wires.size(); ++i) {
    const auto& wire = wires[i];
    for (std::size_t j = 0; j < profiles[i].size(); ++j) {
      const auto& segment = profiles[i][j];
      const auto& span = wire.spans[segment.source_span];
      for (bool lower : {true, false}) {
        const auto neighbor_index = lower ? span.lower_neighbor : span.upper_neighbor;
        if (neighbor_index == kNoWireNeighbor)
          continue;
        const auto& neighbor = wires[neighbor_index];
        const double drawn_distance = lower ? wire.center - neighbor.center : neighbor.center - wire.center;
        // Existing contacts are not new intersections created by this model.
        if (drawn_distance - (wire.width + neighbor.width) / 2 <= 1e-12)
          continue;
        const auto& adjacent = profiles[neighbor_index];
        auto it = std::lower_bound(adjacent.begin(), adjacent.end(), segment.start, [](const auto& item, double value) { return item.end <= value; });
        double covered = segment.start;
        for (; it != adjacent.end() && it->start < segment.end; ++it) {
          const double start = std::max(segment.start, it->start), end = std::min(segment.end, it->end);
          if (start != covered)
            return std::string("missing neighbor cross section");
          covered = end;
          const auto& a = segment.geometry;
          const auto& b = it->geometry;
          const double distance = drawn_distance + (lower ? a.center_shift - b.center_shift : b.center_shift - a.center_shift);
          std::vector<double> cuts{start, end};
          for (const auto* points : {&breakpoints[i][j], &breakpoints[neighbor_index][it - adjacent.begin()]})
            for (double point : *points)
              if (point > start && point < end)
                cuts.push_back(point);
          std::sort(cuts.begin(), cuts.end());
          cuts.erase(std::unique(cuts.begin(), cuts.end()), cuts.end());
          for (std::size_t k = 1; k < cuts.size(); ++k) {
            const auto clearance = checkPair(wire, neighbor, segment, *it, distance, cuts[k - 1], cuts[k], effect);
            if (!clearance)
              return clearance.error;
          }
        }
        if (covered != segment.end)
          return std::string("missing neighbor cross section");
      }
    }
  }
  return {};
}
}  // namespace ircx
