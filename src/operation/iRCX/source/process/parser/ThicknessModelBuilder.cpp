#include "ProcessBuilder.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace ircx::process_detail {

Result<ProcessThicknessPolynomial> buildThicknessPolynomial(const Field& field, SyntaxKind kind, const ProcessUnits& units)
{
  const auto valid = validateDeferredModel(field, units);
  if (!valid) return valid.error;
  ProcessThicknessPolynomial model;
  for (const auto& child : field.children) {
    const auto& key = child.key;
    if (isOneOf(key, {"DENSITY_POLYNOMIAL_ORDERS", "SI_DENSITY_POLYNOMIAL_ORDERS", "WIDTH_POLYNOMIAL_ORDERS", "SI_WIDTH_POLYNOMIAL_ORDERS"})) {
      const bool density = isOneOf(key, {"DENSITY_POLYNOMIAL_ORDERS", "SI_DENSITY_POLYNOMIAL_ORDERS"});
      auto& orders = density ? model.density_orders : model.width_orders;
      for (const auto& quantity : child.values) {
        if (quantity.value < std::numeric_limits<int>::min() || quantity.value > std::numeric_limits<int>::max())
          return fail(child, "invalid_table", "polynomial exponent is outside the supported integer range");
        orders.push_back(static_cast<int>(quantity.value));
      }
      (density ? model.density_basis : model.width_basis) = key.starts_with("SI_") ? GeometryBasis::kFabricated : GeometryBasis::kDrawn;
    } else if (key == "WIDTH_RANGES") {
      for (const auto& value : child.values) model.width_ranges.push_back(*units.convert(value, Dimension::kLength));
    } else if (key == "DENSITY_BOUNDS_VS_WIDTH") {
      model.bounds_source = DensityBoundsSource::kExplicit;
      for (std::size_t i = 0; i < child.values.size(); i += 3) {
        const double minimum = child.values[i + 1].value, maximum = child.values[i + 2].value;
        if (minimum < 0 || maximum > 1)
          return fail(child, "invalid_table", "density bounds must be within [0,1]");
        model.density_bounds.push_back({*units.convert(child.values[i], Dimension::kLength), minimum, maximum});
      }
    } else if (key == "THICKNESS_BOUNDS") {
      const double minimum = *units.convert(child.values[0], Dimension::kLength);
      const double maximum = *units.convert(child.values[1], Dimension::kLength);
      if (minimum <= 0) return fail(child, "invalid_table", "minimum thickness must be positive");
      model.thickness_bounds = {minimum, maximum};
    }
  }
  // Convert after collecting all powers; property order must not affect units.
  for (const auto& child : field.children) {
    if (child.key != "POLYNOMIAL_COEFFICIENTS") continue;
    auto& values = model.coefficients.emplace_back();
    for (std::size_t i = 0; i < child.values.size(); ++i) {
      const auto density_index = i / model.width_orders.size(), width_index = i % model.width_orders.size();
      const auto source_index = kind == SyntaxKind::kTech ? width_index * model.density_orders.size() + density_index : i;
      const double coefficient = child.values[source_index].value;
      const double normalized = coefficient == 0 ? 0 : coefficient / std::pow(units.length, model.width_orders[i % model.width_orders.size()]);
      if (!std::isfinite(normalized) || (coefficient != 0 && normalized == 0))
        return fail(child, "invalid_table", "polynomial coefficient cannot be represented after unit conversion");
      values.push_back(normalized);
    }
  }
  return model;
}

Status resolveThicknessPolynomial(ProcessConductor& conductor, const Field& layer, bool use_si_density, const ProcessUnits& units)
{
  if (!conductor.get_thickness_polynomial()) return {};
  auto& model = *conductor.get_thickness_polynomial();
  const auto& field = *find(layer, "POLYNOMIAL_BASED_THICKNESS_VARIATION");
  if (use_si_density) {
    if (model.width_basis == GeometryBasis::kFabricated || model.density_basis == GeometryBasis::kFabricated)
      return fail(field, "conflicting_geometry_basis", "USE_SI_DENSITY=YES cannot be combined with SI_ polynomial orders");
    model.density_basis = GeometryBasis::kFabricated;
  }
  for (const auto& child : layer.children)
    if (isOneOf(child.key, {"THICKNESS_VS_DENSITY", "THICKNESS_VS_DENSITY_AND_WIDTH", "THICKNESS_VS_WIDTH_AND_SPACING"}))
      return fail(child, "conflicting_thickness", "polynomial thickness cannot be combined with " + child.key);
  if (const auto* window = find(layer, "DENSITY_BOX_WEIGHTING_FACTOR")) {
    if (window->values.size() != 2 || window->values[1].value != 1)
      return fail(*window, "invalid_table", "polynomial thickness requires one density window with unit weight");
    const auto size = units.convert(window->values[0], Dimension::kLength);
    if (!size) return size.error;
    if (*size <= 0 || *size > 500)
      return fail(*window, "invalid_table", "density window size must be within (0,500] micrometres");
    model.density_window = *size;
  }
  if (model.thickness_bounds && (model.thickness_bounds->first >= conductor.get_thickness() || model.thickness_bounds->second <= conductor.get_thickness()))
    return fail(field, "invalid_table", "thickness bounds must enclose the nominal thickness");
  if (model.bounds_source == DensityBoundsSource::kExplicit) return {};
  // Keep source axis extrema: the two density limits depend on query width.
  const auto& tables = conductor.get_etch_table_list();
  if (conductor.get_has_top_etch()
      || std::any_of(tables.begin(), tables.end(), [](const auto& table) { return table.get_effect_type() != ProcessEffectType::kBoth; })) {
    model.bounds_source = DensityBoundsSource::kAmbiguous;
    return {};
  }
  EtchDensityBounds bounds{std::numeric_limits<double>::infinity(), std::numeric_limits<double>::infinity(), 0};
  for (const auto& table : tables) {
    const auto& widths = table.get_table().get_row_list();
    const auto& spaces = table.get_table().get_column_list();
    // A zero singleton is the normalized sentinel for an absent axis.
    if (!widths.empty() && widths.front() > 0)
      bounds.minimum_width = std::min(bounds.minimum_width, widths.front());
    if (!spaces.empty() && spaces.front() > 0) {
      bounds.minimum_spacing = std::min(bounds.minimum_spacing, spaces.front());
      bounds.maximum_spacing = std::max(bounds.maximum_spacing, spaces.back());
    }
  }
  if (!std::isfinite(bounds.minimum_width) || !std::isfinite(bounds.minimum_spacing) || !std::isfinite(bounds.maximum_spacing)
      || bounds.maximum_spacing <= 0)
    return {};
  model.etch_density_bounds = bounds;
  model.bounds_source = DensityBoundsSource::kEtchTable;
  return {};
}

}  // namespace ircx::process_detail
