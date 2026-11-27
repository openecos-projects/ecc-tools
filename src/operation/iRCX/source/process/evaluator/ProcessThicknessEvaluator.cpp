#include "ProcessThicknessEvaluator.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <numeric>

namespace ircx {
namespace {
bool positive(double value)
{
  return std::isfinite(value) && value > 0;
}

std::size_t coefficientTable(const std::vector<double>& ranges, double width)
{
  auto boundary = std::lower_bound(ranges.begin(), ranges.end(), width);
  if (boundary != ranges.end() && *boundary == width)
    return static_cast<std::size_t>(boundary - ranges.begin());
  if (boundary != ranges.begin()) {
    // Scaling and subtracting etch can put a mathematical boundary a few ULPs
    // above its stored value. This tolerance affects interval selection only,
    // and is many orders below layout precision; do not use a geometric epsilon.
    double rounded_boundary = *std::prev(boundary);
    for (int i = 0; i < 8; ++i)
      rounded_boundary = std::nextafter(rounded_boundary, std::numeric_limits<double>::infinity());
    if (width <= rounded_boundary)
      --boundary;
  }
  return static_cast<std::size_t>(boundary - ranges.begin());
}

ProcessResult<double> boundedDensity(const ProcessThicknessPolynomial& model, double width, const WireGeometryQuery& query)
{
  if (model.bounds_source == DensityBoundsSource::kAmbiguous)
    return std::string("polynomial density bounds for qualified etch tables require DENSITY_BOUNDS_VS_WIDTH");
  const auto& density = model.density_basis == GeometryBasis::kDrawn ? query.drawn_density : query.fabricated_density;
  if (!density)
    return std::string(model.density_basis == GeometryBasis::kDrawn ? "missing drawn metal density" : "missing fabricated metal density");
  if (!std::isfinite(*density) || *density < 0 || *density > 1)
    return std::string("metal density must be finite and within [0,1]");
  if (model.bounds_source == DensityBoundsSource::kEtchTable) {
    if (!model.etch_density_bounds)
      return std::string("missing etch axis extrema for polynomial density bounds");
    const auto& bounds = *model.etch_density_bounds;
    if (!positive(bounds.minimum_width) || !positive(bounds.minimum_spacing) || !positive(bounds.maximum_spacing)
        || bounds.minimum_spacing > bounds.maximum_spacing)
      return std::string("invalid etch axis extrema for polynomial density bounds");
    const long double effective_width = std::max(bounds.minimum_width, width);
    const double minimum = static_cast<double>(effective_width / (effective_width + bounds.maximum_spacing));
    const double maximum = static_cast<double>(effective_width / (effective_width + bounds.minimum_spacing));
    return std::clamp(*density, minimum, maximum);
  }
  if (model.bounds_source != DensityBoundsSource::kExplicit || model.density_bounds.empty())
    return std::string("polynomial density bounds require DENSITY_BOUNDS_VS_WIDTH or unqualified etch width and spacing axes");
  double previous_width = 0;
  for (const auto& entry : model.density_bounds) {
    if (!positive(entry.width) || entry.width <= previous_width || !std::isfinite(entry.minimum) || !std::isfinite(entry.maximum) || entry.minimum < 0
        || entry.maximum > 1 || entry.minimum > entry.maximum)
      return std::string("invalid polynomial density bounds");
    previous_width = entry.width;
  }
  const auto& bounds = model.density_bounds;
  const auto upper = std::lower_bound(bounds.begin(), bounds.end(), width, [](const DensityBounds& entry, double value) { return entry.width < value; });
  double minimum = bounds.back().minimum, maximum = bounds.back().maximum;
  if (upper == bounds.begin()) {
    minimum = upper->minimum;
    maximum = upper->maximum;
  } else if (upper != bounds.end()) {
    const auto& lower = *std::prev(upper);
    const double ratio = (width - lower.width) / (upper->width - lower.width);
    minimum = std::lerp(lower.minimum, upper->minimum, ratio);
    maximum = std::lerp(lower.maximum, upper->maximum, ratio);
  }
  if (!std::isfinite(minimum) || !std::isfinite(maximum) || minimum < 0 || maximum > 1 || minimum > maximum)
    return std::string("invalid polynomial density bounds");
  return std::clamp(*density, minimum, maximum);
}
}  // namespace

ProcessResult<double> evaluatePolynomialThickness(const ProcessThicknessPolynomial& model, double nominal_thickness, const WireGeometryQuery& drawn,
                                                  const EffectiveWireGeometry& etched)
{
  const double width = model.width_basis == GeometryBasis::kDrawn ? drawn.width : etched.width;
  if (!positive(width) || !positive(nominal_thickness))
    return std::string("invalid polynomial width or nominal thickness");
  double previous_width = 0;
  for (double threshold : model.width_ranges) {
    if (!positive(threshold) || threshold <= previous_width)
      return std::string("invalid polynomial width ranges");
    previous_width = threshold;
  }
  if (model.thickness_bounds
      && (!positive(model.thickness_bounds->first) || !positive(model.thickness_bounds->second) || model.thickness_bounds->first >= nominal_thickness
          || model.thickness_bounds->second <= nominal_thickness))
    return std::string("polynomial thickness bounds must enclose the nominal thickness");
  const auto table = coefficientTable(model.width_ranges, width);
  if (model.coefficients.size() != model.width_ranges.size() + 1 || model.density_orders.empty() || model.width_orders.empty())
    return std::string("invalid polynomial dimensions");
  const auto& coefficients = model.coefficients[table];
  if (coefficients.size() != model.density_orders.size() * model.width_orders.size())
    return std::string("polynomial coefficient count does not match its powers");
  bool needs_density = false;
  for (std::size_t i = 0; i < coefficients.size(); ++i)
    if (coefficients[i] != 0 && model.density_orders[i / model.width_orders.size()] != 0)
      needs_density = true;
  double density = 1;
  if (needs_density) {
    const auto value = boundedDensity(model, width, drawn);
    if (!value)
      return value.error;
    density = *value;
  }
  long double change = 0;
  for (std::size_t i = 0; i < model.density_orders.size(); ++i) {
    for (std::size_t j = 0; j < model.width_orders.size(); ++j) {
      const double coefficient = coefficients[i * model.width_orders.size() + j];
      if (coefficient == 0)
        continue;
      const auto term = coefficient * std::pow(static_cast<long double>(density), model.density_orders[i])
                        * std::pow(static_cast<long double>(width), model.width_orders[j]);
      if (!std::isfinite(term))
        return std::string("nonfinite polynomial term");
      change += term;
    }
  }
  double thickness = static_cast<double>(nominal_thickness * (1 + change));
  if (!std::isfinite(thickness))
    return std::string("nonfinite polynomial thickness");
  if (model.thickness_bounds)
    thickness = std::clamp(thickness, model.thickness_bounds->first, model.thickness_bounds->second);
  return thickness;
}

namespace {
using Polynomial = std::vector<long double>;
long double valueAt(const Polynomial& polynomial, long double x)
{
  long double result = 0;
  for (auto it = polynomial.rbegin(); it != polynomial.rend(); ++it)
    result = result * x + *it;
  return result;
}

// Derivative roots partition a polynomial into monotone intervals. This finds
// interior roots even when endpoints and regularly spaced samples agree.
std::vector<long double> realRoots(Polynomial polynomial, long double lo, long double hi)
{
  while (!polynomial.empty() && polynomial.back() == 0)
    polynomial.pop_back();
  if (polynomial.size() < 2 || lo >= hi)
    return {};
  if (polynomial.size() == 2) {
    const auto root = -polynomial[0] / polynomial[1];
    return root >= lo && root <= hi ? std::vector<long double>{root} : std::vector<long double>{};
  }
  Polynomial derivative(polynomial.size() - 1);
  for (std::size_t i = 1; i < polynomial.size(); ++i)
    derivative[i - 1] = i * polynomial[i];
  auto points = realRoots(std::move(derivative), lo, hi);
  points.insert(points.begin(), lo);
  points.push_back(hi);
  std::vector<long double> roots;
  for (auto x : points) {
    long double scale = 0;
    for (auto it = polynomial.rbegin(); it != polynomial.rend(); ++it)
      scale = scale * std::abs(x) + std::abs(*it);
    if (std::abs(valueAt(polynomial, x)) <= 64 * std::numeric_limits<long double>::epsilon() * scale)
      roots.push_back(x);
  }
  for (std::size_t i = 1; i < points.size(); ++i) {
    auto a = points[i - 1], b = points[i];
    const auto fa = valueAt(polynomial, a), fb = valueAt(polynomial, b);
    if (fa == 0 || fb == 0 || std::signbit(fa) == std::signbit(fb))
      continue;
    for (int iteration = 0; iteration < 128; ++iteration) {
      const auto mid = std::midpoint(a, b), fm = valueAt(polynomial, mid);
      if (mid == a || mid == b || fm == 0) {
        a = b = mid;
        break;
      }
      if (std::signbit(fa) == std::signbit(fm))
        a = mid;
      else
        b = mid;
    }
    roots.push_back(std::midpoint(a, b));
  }
  std::sort(roots.begin(), roots.end());
  roots.erase(std::unique(roots.begin(), roots.end()), roots.end());
  return roots;
}

ProcessResult<std::vector<long double>> densityRoots(std::map<int64_t, long double> terms, double lo, double hi)
{
  std::erase_if(terms, [](const auto& term) { return term.second == 0; });
  if (terms.empty())
    return std::vector<long double>{};
  // A positive power of density does not change roots on a positive interval.
  // Removing it also permits negative integer powers without a sampling grid.
  const auto first = terms.begin()->first, last = terms.rbegin()->first;
  if (last - first > 64)
    return std::string("spatial thickness polynomial exponent span exceeds 64");
  Polynomial polynomial(static_cast<std::size_t>(last - first) + 1);
  for (const auto& [power, coefficient] : terms)
    polynomial[power - first] = coefficient;
  return realRoots(std::move(polynomial), lo, hi);
}
}  // namespace

ProcessResult<std::vector<double>> thicknessDensityBreakpoints(const ProcessThicknessPolynomial& model, double nominal_thickness,
                                                               const WireGeometryQuery& drawn, const EffectiveWireGeometry& etched, double density_start,
                                                               double density_end, const std::vector<double>& thickness_levels)
{
  if (!std::isfinite(density_start) || !std::isfinite(density_end) || std::min(density_start, density_end) < 0 || std::max(density_start, density_end) > 1)
    return std::string("invalid thickness density interval");
  WireGeometryQuery query = drawn;
  auto& density = model.density_basis == GeometryBasis::kDrawn ? query.drawn_density : query.fabricated_density;
  for (double endpoint : {density_start, density_end}) {
    density = endpoint;
    const auto thickness = evaluatePolynomialThickness(model, nominal_thickness, query, etched);
    if (!thickness)
      return thickness.error;
    if (!positive(*thickness))
      return std::string("nonpositive thickness in the spatial interval");
  }
  const double width = model.width_basis == GeometryBasis::kDrawn ? drawn.width : etched.width;
  const auto& coefficients = model.coefficients[coefficientTable(model.width_ranges, width)];
  std::map<int64_t, long double> terms;
  bool needs_density = false;
  for (std::size_t i = 0; i < model.density_orders.size(); ++i)
    for (std::size_t j = 0; j < model.width_orders.size(); ++j) {
      const auto c = coefficients[i * model.width_orders.size() + j];
      if (c == 0)
        continue;
      needs_density |= model.density_orders[i] != 0;
      terms[model.density_orders[i]] += c * std::pow(static_cast<long double>(width), model.width_orders[j]);
    }
  terms[0] += 1;
  std::vector<double> result{std::min(density_start, density_end), std::max(density_start, density_end)};
  if (!needs_density || density_start == density_end)
    return result;
  density = result.front();
  const auto low = boundedDensity(model, width, query);
  density = result.back();
  const auto high = boundedDensity(model, width, query);
  if (!low)
    return low.error;
  if (!high)
    return high.error;
  auto append = [&](double d) {
    if (d > result.front() && d < std::max(density_start, density_end))
      result.push_back(d);
  };
  append(*low);
  append(*high);
  std::map<int64_t, long double> derivative;
  for (const auto& [power, coefficient] : terms)
    if (power != 0)
      derivative[power - 1] = power * coefficient;
  const auto extrema = densityRoots(std::move(derivative), *low, *high);
  if (!extrema)
    return extrema.error;
  for (auto root : *extrema)
    append(static_cast<double>(root));
  auto levels = thickness_levels;
  if (model.thickness_bounds) {
    levels.push_back(model.thickness_bounds->first);
    levels.push_back(model.thickness_bounds->second);
  }
  for (double level : levels) {
    if (!positive(level))
      return std::string("invalid thickness interpolation level");
    auto equation = terms;
    equation[0] -= static_cast<long double>(level) / nominal_thickness;
    const auto crossings = densityRoots(std::move(equation), *low, *high);
    if (!crossings)
      return crossings.error;
    for (auto root : *crossings)
      append(static_cast<double>(root));
  }
  std::sort(result.begin(), result.end());
  result.erase(std::unique(result.begin(), result.end()), result.end());
  for (double point : result) {
    density = point;
    const auto thickness = evaluatePolynomialThickness(model, nominal_thickness, query, etched);
    if (!thickness)
      return thickness.error;
    if (!positive(*thickness))
      return std::string("nonpositive thickness in the spatial interval");
  }
  return result;
}

ProcessResult<double> thicknessCurvatureBound(const ProcessThicknessPolynomial& model, double nominal_thickness, const WireGeometryQuery& drawn,
                                              const EffectiveWireGeometry& etched, double density_start, double density_end)
{
  if (!std::isfinite(density_start) || !std::isfinite(density_end) || std::min(density_start, density_end) < 0 || std::max(density_start, density_end) > 1)
    return std::string("invalid thickness curvature density interval");
  WireGeometryQuery query = drawn;
  auto& density = model.density_basis == GeometryBasis::kDrawn ? query.drawn_density : query.fabricated_density;
  for (double endpoint : {density_start, density_end}) {
    density = endpoint;
    const auto thickness = evaluatePolynomialThickness(model, nominal_thickness, query, etched);
    if (!thickness)
      return thickness.error;
    if (!positive(*thickness))
      return std::string("nonpositive thickness in the spatial interval");
  }
  const double width = model.width_basis == GeometryBasis::kDrawn ? drawn.width : etched.width;
  const auto& coefficients = model.coefficients[coefficientTable(model.width_ranges, width)];
  bool nonlinear = false;
  for (std::size_t i = 0; i < coefficients.size(); ++i) {
    const auto power = model.density_orders[i / model.width_orders.size()];
    nonlinear |= coefficients[i] != 0 && power != 0 && power != 1;
  }
  if (!nonlinear || density_start == density_end)
    return 0.0;
  density = density_start;
  const auto low = boundedDensity(model, width, query);
  density = density_end;
  const auto high = boundedDensity(model, width, query);
  if (!low)
    return low.error;
  if (!high)
    return high.error;
  long double curvature = 0;
  for (std::size_t i = 0; i < model.density_orders.size(); ++i) {
    const int64_t power = model.density_orders[i];
    if (power == 0 || power == 1)
      continue;
    for (std::size_t j = 0; j < model.width_orders.size(); ++j) {
      const auto coefficient = coefficients[i * model.width_orders.size() + j];
      if (coefficient == 0)
        continue;
      const long double factor = std::max(std::pow(static_cast<long double>(*low), power - 2), std::pow(static_cast<long double>(*high), power - 2));
      curvature
          += std::abs(coefficient * std::pow(static_cast<long double>(width), model.width_orders[j]) * static_cast<long double>(power) * (power - 1)) * factor;
    }
  }
  const double result = static_cast<double>(nominal_thickness * curvature * (*high - *low) * (*high - *low));
  if (!std::isfinite(result))
    return std::string("nonfinite thickness curvature bound");
  return result;
}

ProcessResult<EffectiveWireGeometry> finalizeWireGeometry(const ProcessConductor& conductor, const WireGeometryQuery& drawn, EffectiveWireGeometry etched,
                                                          ProcessEffectType effect)
{
  if (effect != ProcessEffectType::kResistance && effect != ProcessEffectType::kCapacitance)
    return std::string("geometry finalization requires one effect domain");
  if (const auto* issue = findEvaluationIssue(conductor.get_evaluation_issues(), effect))
    return "cannot finalize conductor " + conductor.get_layer_name() + ": " + issue->field + ": " + issue->message;
  if (conductor.get_thickness_polynomial()) {
    const auto thickness = evaluatePolynomialThickness(*conductor.get_thickness_polynomial(), conductor.get_thickness(), drawn, etched);
    if (!thickness)
      return thickness.error + " on " + conductor.get_layer_name();
    etched.thickness = *thickness;
  }
  // Top etch owns the sidewalls. Its capacitance evaluation remains checked by
  // the conductor's evaluation issue; the unchanged resistance area is usable.
  etched.side_tangent = conductor.get_has_top_etch() ? 0 : conductor.get_side_tangent();
  if (!positive(etched.width) || !positive(etched.thickness) || !std::isfinite(etched.center_shift) || !positive(etched.top_width())
      || !positive(etched.bottom_width()))
    return "invalid final wire cross section on " + conductor.get_layer_name();
  return etched;
}

}  // namespace ircx
