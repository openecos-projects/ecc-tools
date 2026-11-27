#include <cmath>
#include <iostream>

#include "ProcessEvaluator.hpp"
#include "ProcessSpatialEvaluator.hpp"

namespace {
int failures = 0;
void check(bool value, const char* message)
{
  if (!value) {
    ++failures;
    std::cerr << "FAIL: " << message << '\n';
  }
}
bool near(double a, double b)
{
  return std::abs(a - b) < 2e-8 * std::max(1.0, std::abs(b));
}
}  // namespace

int main()
{
  using namespace ircx;
  ProcessConductor conductor;
  conductor.set_layer_name("M1");
  conductor.set_thickness(2);
  conductor.set_resistivity(2);
  conductor.set_resistance_model(WireResistanceModel::kResistivity);
  ProcessThicknessPolynomial polynomial;
  polynomial.density_orders = {2};
  polynomial.width_orders = {0};
  polynomial.coefficients = {{1}};
  polynomial.bounds_source = DensityBoundsSource::kExplicit;
  polynomial.density_bounds = {{1, 0, 1}};
  conductor.get_thickness_polynomial() = polynomial;
  EffectiveWireSegment segment{0, 0, 10, {1, 2}};
  segment.drawn_density = std::pair{0.0, 1.0};
  const WireGeometryQuery drawn{1};
  auto result = integrateWireResistance(conductor, drawn, segment, 0, 10, 25, 25);
  check(result && near(*result, 10 * std::atan(1.0)), "integrate local thickness instead of thickness at average density");
  auto midpoint = evaluateSegmentGeometry(conductor, drawn, segment, 5, ProcessEffectType::kResistance);
  check(midpoint && near(midpoint->thickness, 2.5), "local geometry uses interpolated density");
  check(!evaluateSegmentGeometry(conductor, drawn, segment, 11, ProcessEffectType::kResistance), "reject queries beyond the profile");
  auto left = integrateWireResistance(conductor, drawn, segment, 0, 4, 25, 25);
  auto right = integrateWireResistance(conductor, drawn, segment, 4, 10, 25, 25);
  check(left && right && result && near(*left + *right, *result), "resistance is invariant under wire fragmentation");

  polynomial.density_bounds = {{1, 0.25, 0.75}};
  conductor.get_thickness_polynomial() = polynomial;
  result = integrateWireResistance(conductor, drawn, segment, 0, 10, 25, 25);
  const double clipped = 10 * (0.25 / (1 + 0.25 * 0.25) + std::atan(0.75) - std::atan(0.25) + 0.25 / (1 + 0.75 * 0.75));
  check(result && near(*result, clipped), "density clipping is applied locally before polynomial integration");

  polynomial.density_bounds = {{1, 0, 1}};
  polynomial.density_basis = GeometryBasis::kFabricated;
  conductor.get_thickness_polynomial() = polynomial;
  check(!integrateWireResistance(conductor, drawn, segment, 0, 10, 25, 25), "fabricated density cannot fall back to drawn density");
  segment.fabricated_density = std::pair{0.2, 0.8};
  result = integrateWireResistance(conductor, drawn, segment, 0, 10, 25, 25);
  check(result && near(*result, 10 * (std::atan(0.8) - std::atan(0.2)) / 0.6), "fabricated density selects its own spatial function");

  polynomial.density_basis = GeometryBasis::kDrawn;
  polynomial.thickness_bounds = std::pair{1.0, 3.0};
  conductor.get_thickness_polynomial() = polynomial;
  result = integrateWireResistance(conductor, drawn, segment, 0, 10, 25, 25);
  const double threshold = std::sqrt(0.5);
  check(result && near(*result, 10 * (std::atan(threshold) + (1 - threshold) / 1.5)), "absolute thickness bounds apply within the integral");
  polynomial.thickness_bounds.reset();
  polynomial.coefficients = {{-2}};
  conductor.get_thickness_polynomial() = polynomial;
  check(!integrateWireResistance(conductor, drawn, segment, 0, 10, 25, 25), "nonpositive local thickness is rejected");
  polynomial.coefficients = {{0}};
  conductor.get_thickness_polynomial() = polynomial;
  segment.drawn_density.reset();
  segment.fabricated_density.reset();
  result = integrateWireResistance(conductor, drawn, segment, 0, 10, 25, 25);
  check(result && near(*result, 10), "density-independent models do not require a density snapshot");
  check(!integrateWireResistance(conductor, drawn, segment, 10, 0, 25, 25), "reject reversed integration interval");

  // A narrow unclipped trough lies between all five initial quadrature samples.
  // The result must not depend on whether the caller happens to split there.
  conductor.set_thickness(1);
  conductor.set_resistivity(1);
  polynomial.density_orders = {0, 1, 2};
  polynomial.coefficients = {{1, -20, 100}};
  polynomial.thickness_bounds = std::pair{0.5, 2.0};
  conductor.get_thickness_polynomial() = polynomial;
  segment = {0, 0, 1, {1, 1}};
  segment.drawn_density = std::pair{0.0, 1.0};
  result = integrateWireResistance(conductor, drawn, segment, 0, 1, 25, 25);
  left = integrateWireResistance(conductor, drawn, segment, 0, 0.2, 25, 25);
  right = integrateWireResistance(conductor, drawn, segment, 0.2, 1, 25, 25);
  check(result && near(*result, 0.4 + std::atan(1.0) / 5), "resolve interior thickness clipping crossings");
  check(result && left && right && near(*result, *left + *right), "clipped polynomial is fragmentation invariant");
  polynomial.thickness_bounds.reset();
  polynomial.coefficients = {{-0.5, -20, 100}};
  conductor.get_thickness_polynomial() = polynomial;
  check(!integrateWireResistance(conductor, drawn, segment, 0, 1, 25, 25), "reject nonpositive thickness between integration samples");

  polynomial.coefficients = {{1, -20, 100}};
  polynomial.thickness_bounds = std::pair{0.5, 2.0};
  conductor.get_thickness_polynomial() = polynomial;
  segment.drawn_density = std::pair{1.0, 0.0};
  result = integrateWireResistance(conductor, drawn, segment, 0, 1, 25, 25);
  check(result && near(*result, 0.4 + std::atan(1.0) / 5), "decreasing density preserves clipped integral");
  polynomial.density_orders = {-1};
  polynomial.coefficients = {{1}};
  polynomial.thickness_bounds.reset();
  conductor.get_thickness_polynomial() = polynomial;
  segment.drawn_density = std::pair{0.25, 0.75};
  result = integrateWireResistance(conductor, drawn, segment, 0, 1, 25, 25);
  check(result && near(*result, 1 - 2 * std::log(1.75 / 1.25)), "negative density powers integrate on positive intervals");

  polynomial.density_orders = {1};
  polynomial.coefficients = {{1}};
  conductor.get_thickness_polynomial() = polynomial;
  conductor.set_resistance_model(WireResistanceModel::kResistivityByWidthThickness);
  auto& rho = conductor.get_resistivity_by_width_thickness_table();
  const std::vector<double> thicknesses{1, 1.09, 1.1, 1.11, 2}, resistivities{1, 1.09, 0.11, 1.11, 2};
  rho.set_row_list(thicknesses);
  rho.set_column_list({1});
  rho.set_value_list(resistivities);
  segment.drawn_density = std::pair{0.0, 1.0};
  double exact = 0;
  for (std::size_t i = 1; i < thicknesses.size(); ++i) {
    const double lo = thicknesses[i - 1], hi = thicknesses[i];
    const double slope = (resistivities[i] - resistivities[i - 1]) / (hi - lo);
    exact += slope * (hi - lo) + (resistivities[i - 1] - slope * lo) * std::log(hi / lo);
  }
  result = integrateWireResistance(conductor, drawn, segment, 0, 1, 25, 25);
  check(result && near(*result, exact), "resolve a narrow feature on the resistivity thickness axis");
  conductor.set_resistance_model(WireResistanceModel::kResistivity);

  // An interior maximum can create overlap even with valid endpoint and
  // midpoint cross sections. Neighbors need not share density breakpoints.
  polynomial.density_orders = {0, 1, 2};
  polynomial.coefficients = {{0, 2, -10}};
  conductor.get_thickness_polynomial() = polynomial;
  conductor.set_side_tangent(0.1);
  std::vector<WireGeometryInput> wires{{&conductor, 0, 1, {{0, 0.2, kNoWireNeighbor, 1}}}, {&conductor, 1.105, 1, {{0, 0.2, 0, kNoWireNeighbor}}}};
  EffectiveWireSegment a{0, 0, 0.2, {1, 1}};
  a.drawn_density = std::pair{0.0, 0.2};
  EffectiveWireSegment b = a;
  WireGeometryProfiles profiles{{a}, {b}};
  check(!validateWireGeometryProfiles(wires, profiles, ProcessEffectType::kResistance), "interior thickness maxima can create sidewall overlap");
  wires[1].center = 1.12;
  profiles[1][0].end = 0.08;
  profiles[1][0].drawn_density = std::pair{0.0, 0.08};
  b.start = 0.08;
  b.drawn_density = std::pair{0.08, 0.2};
  profiles[1].push_back(b);
  check(bool(validateWireGeometryProfiles(wires, profiles, ProcessEffectType::kResistance)), "validate neighbors with independent density partitions");
  profiles[0][0].source_span = 3;
  check(!validateWireGeometryProfiles(wires, profiles, ProcessEffectType::kResistance), "reject invalid profile source index");
  profiles[0][0].source_span = 0;
  wires[0].spans[0].upper_neighbor = 10;
  check(!validateWireGeometryProfiles(wires, profiles, ProcessEffectType::kResistance), "reject invalid neighbor before indexing");
  wires[0].spans[0].upper_neighbor = 1;
  wires[0].conductor = nullptr;
  check(!validateWireGeometryProfiles(wires, profiles, ProcessEffectType::kResistance), "reject null process conductor");

  polynomial.density_orders = {1};
  polynomial.coefficients = {{1}};
  conductor.get_thickness_polynomial() = polynomial;
  wires = {{&conductor, 0, 1, {{0, 1, kNoWireNeighbor, 1}}}, {&conductor, 1 + 0.1 * (1 + 4.0 / 9), 1, {{0, 1, 0, kNoWireNeighbor}}}};
  a = {0, 0, 1, {1, 1}};
  b = a;
  a.drawn_density = std::pair{0.0, 1.0};
  b.drawn_density = std::pair{0.8, 0.0};
  profiles = {{a}, {b}};
  check(bool(validateWireGeometryProfiles(wires, profiles, ProcessEffectType::kResistance)), "allow isolated sidewall contact at an interior position");
  wires[1].center -= 1e-8;
  check(!validateWireGeometryProfiles(wires, profiles, ProcessEffectType::kResistance), "reject real overlap near an interior contact");
  return failures ? 1 : 0;
}
