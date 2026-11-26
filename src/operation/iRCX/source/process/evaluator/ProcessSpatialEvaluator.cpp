#include "ProcessSpatialEvaluator.hpp"

#include <algorithm>
#include <cmath>
#include <numeric>

#include "ProcessEvaluator.hpp"
#include "ProcessThicknessEvaluator.hpp"

namespace ircx {
ProcessResult<EffectiveWireGeometry> evaluateSegmentGeometry(const ProcessConductor& conductor, const WireGeometryQuery& drawn,
                                                             const EffectiveWireSegment& segment, double position, ProcessEffectType effect)
{
  if (!std::isfinite(segment.start) || !std::isfinite(segment.end) || segment.start >= segment.end || !std::isfinite(position) || position < segment.start
      || position > segment.end)
    return std::string("invalid spatial geometry interval or position");
  WireGeometryQuery query = drawn;
  const double fraction = (position - segment.start) / (segment.end - segment.start);
  for (bool fabricated : {false, true}) {
    const auto& density = fabricated ? segment.fabricated_density : segment.drawn_density;
    if (!density)
      continue;
    for (double value : {density->first, density->second})
      if (!std::isfinite(value) || value < 0 || value > 1)
        return std::string("invalid spatial metal density");
    (fabricated ? query.fabricated_density : query.drawn_density) = std::lerp(density->first, density->second, fraction);
  }
  return finalizeWireGeometry(conductor, query, segment.geometry, effect);
}

ProcessResult<std::vector<double>> segmentGeometryBreakpoints(const ProcessConductor& conductor, const WireGeometryQuery& drawn,
                                                              const EffectiveWireSegment& segment, ProcessEffectType effect,
                                                              const std::vector<double>& thickness_levels)
{
  for (double point : {segment.start, segment.end}) {
    const auto geometry = evaluateSegmentGeometry(conductor, drawn, segment, point, effect);
    if (!geometry)
      return geometry.error;
  }
  std::vector<double> positions{segment.start, segment.end};
  if (!conductor.get_thickness_polynomial())
    return positions;
  const auto& model = *conductor.get_thickness_polynomial();
  const auto& density = model.density_basis == GeometryBasis::kDrawn ? segment.drawn_density : segment.fabricated_density;
  if (!density || density->first == density->second)
    return positions;
  const auto points = thicknessDensityBreakpoints(model, conductor.get_thickness(), drawn, segment.geometry, density->first, density->second, thickness_levels);
  if (!points)
    return points.error;
  for (double point : *points) {
    const double ratio = (point - density->first) / (density->second - density->first);
    const double position = std::lerp(segment.start, segment.end, ratio);
    if (position > segment.start && position < segment.end)
      positions.push_back(position);
  }
  std::sort(positions.begin(), positions.end());
  positions.erase(std::unique(positions.begin(), positions.end()), positions.end());
  for (double point : positions) {
    const auto geometry = evaluateSegmentGeometry(conductor, drawn, segment, point, effect);
    if (!geometry)
      return geometry.error;
  }
  return positions;
}

ProcessResult<double> integrateWireResistance(const ProcessConductor& conductor, const WireGeometryQuery& drawn, const EffectiveWireSegment& segment,
                                              double start, double end, double temperature, double nominal_temperature)
{
  if (!std::isfinite(start) || !std::isfinite(end) || start >= end || start < segment.start || end > segment.end)
    return std::string("invalid spatial resistance interval");
  const auto breakpoints = segmentGeometryBreakpoints(conductor, drawn, segment, ProcessEffectType::kResistance,
                                                      conductor.get_resistance_model() == WireResistanceModel::kResistivityByWidthThickness
                                                          ? conductor.get_resistivity_by_width_thickness_table().get_row_list()
                                                          : std::vector<double>{});
  if (!breakpoints)
    return breakpoints.error;
  if (!conductor.get_thickness_polynomial())
    return evaluateWireResistanceFromGeometry(conductor, drawn, segment.geometry, end - start, temperature, nominal_temperature);
  auto resistance_per_length = [&](double point) -> ProcessResult<double> {
    const auto geometry = evaluateSegmentGeometry(conductor, drawn, segment, point, ProcessEffectType::kResistance);
    if (!geometry)
      return geometry.error;
    return evaluateWireResistanceFromGeometry(conductor, drawn, *geometry, 1, temperature, nominal_temperature);
  };
  auto simpson = [](double a, double b, double left, double middle, double right) { return (b - a) * (left / 6 + middle * (2.0 / 3) + right / 6); };
  // The absolute term is in ohms. Failure to attain the tolerance is reported;
  // reaching the recursion limit never silently accepts an unresolved interval.
  auto refine = [&](auto&& self, double a, double b, double fa, double fm, double fb, double coarse, double allowed_error, int depth) -> ProcessResult<double> {
    const double m = std::midpoint(a, b), q1 = std::midpoint(a, m), q3 = std::midpoint(m, b);
    const auto f1 = resistance_per_length(q1), f3 = resistance_per_length(q3);
    if (!f1)
      return f1.error;
    if (!f3)
      return f3.error;
    const double low = simpson(a, m, fa, *f1, fm), high = simpson(m, b, fm, *f3, fb);
    const double difference = low + high - coarse;
    if (!std::isfinite(low + high))
      return std::string("nonfinite spatial resistance integral");
    if (std::abs(difference) <= 15 * allowed_error)
      return low + high + difference / 15;
    if (depth == 0 || q1 == a || q3 == b)
      return std::string("spatial resistance integral did not converge");
    const auto first = self(self, a, m, fa, *f1, fm, low, allowed_error / 2, depth - 1);
    if (!first)
      return first.error;
    const auto second = self(self, m, b, fm, *f3, fb, high, allowed_error / 2, depth - 1);
    if (!second)
      return second.error;
    return *first + *second;
  };
  double total = 0;
  for (std::size_t i = 1; i < breakpoints->size(); ++i) {
    const double a = std::max(start, (*breakpoints)[i - 1]), b = std::min(end, (*breakpoints)[i]);
    if (a >= b)
      continue;
    const auto left = resistance_per_length(a), middle = resistance_per_length(std::midpoint(a, b)), right = resistance_per_length(b);
    if (!left)
      return left.error;
    if (!middle)
      return middle.error;
    if (!right)
      return right.error;
    const double estimate = simpson(a, b, *left, *middle, *right);
    const double tolerance = 1e-12 * ((b - a) / (end - start)) + 1e-9 * std::abs(estimate);
    const auto integral = refine(refine, a, b, *left, *middle, *right, estimate, tolerance, 24);
    if (!integral)
      return integral.error;
    total += *integral;
  }
  if (!std::isfinite(total) || total <= 0)
    return std::string("invalid spatial resistance integral");
  return total;
}
}  // namespace ircx
