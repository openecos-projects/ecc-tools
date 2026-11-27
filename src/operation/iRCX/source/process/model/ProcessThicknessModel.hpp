#pragma once

#include <optional>
#include <utility>
#include <vector>

namespace ircx {

enum class GeometryBasis { kDrawn, kFabricated };
enum class DensityBoundsSource { kExplicit, kEtchTable, kMissing, kAmbiguous };

struct DensityBounds
{
  double width;
  double minimum;
  double maximum;
};

struct EtchDensityBounds
{
  double minimum_width;
  double minimum_spacing;
  double maximum_spacing;
};

// Canonical units are micrometres. Coefficients retain the declared exponent
// order: rows are density powers and columns are width powers. Each coefficient
// has already been normalized for the corresponding width exponent.
struct ProcessThicknessPolynomial
{
  GeometryBasis width_basis = GeometryBasis::kDrawn;
  GeometryBasis density_basis = GeometryBasis::kDrawn;
  std::vector<int> density_orders;
  std::vector<int> width_orders;
  std::vector<double> width_ranges;
  std::vector<std::vector<double>> coefficients;
  DensityBoundsSource bounds_source = DensityBoundsSource::kMissing;
  std::vector<DensityBounds> density_bounds;
  std::optional<EtchDensityBounds> etch_density_bounds;
  std::optional<std::pair<double, double>> thickness_bounds;
  double density_window = 50.0;
};

}  // namespace ircx
