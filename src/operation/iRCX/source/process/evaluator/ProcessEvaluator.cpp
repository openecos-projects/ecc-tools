#include "ProcessEvaluator.hpp"

#include <cmath>
#include <limits>

namespace ircx {
namespace {
bool positive(double value)
{
  return std::isfinite(value) && value > 0;
}
double spacing(double value)
{
  return value < 0 ? std::numeric_limits<double>::infinity() : value;
}
bool applies(ProcessEffectType table, ProcessEffectType query) { return table == ProcessEffectType::kBoth || table == query; }
ProcessResult<double> temperatureAdjusted(double base, double temperature, double nominal, double first, double second, const std::string& description)
{
  const double delta = temperature - nominal;
  const double factor = 1 + first * delta + second * delta * delta;
  if (!positive(factor))
    return std::string("invalid resistance temperature factor");
  const double resistance = base * factor;
  if (!positive(resistance))
    return "invalid " + description;
  return resistance;
}
}
ProcessResult<EffectiveWireGeometry> evaluateWireGeometry(const ProcessConductor& conductor, const WireGeometryQuery& query, ProcessEffectType domain)
{
  if (domain != ProcessEffectType::kResistance && domain != ProcessEffectType::kCapacitance)
    return std::string("geometry query requires one effect domain");
  const auto& name = conductor.get_layer_name();
  if (const auto* issue = findEvaluationIssue(conductor.get_evaluation_issues(), domain))
    return "cannot evaluate conductor " + name + ": " + issue->field + ": " + issue->message;
  if (!positive(query.width))
    return "invalid drawn width on " + name;
  if (!positive(conductor.get_thickness()))
    return "invalid thickness on " + name;
  if (!std::isfinite(query.lower_spacing) || !std::isfinite(query.upper_spacing))
    return std::string("nonfinite wire spacing");
  EffectiveWireGeometry result{query.width, conductor.get_thickness()};
  const double lower = spacing(query.lower_spacing), upper = spacing(query.upper_spacing);
  for (const auto& table : conductor.get_etch_table_list()) {
    if (!applies(table.get_effect_type(), domain)) continue;
    const double lower_etch = table.get_table().query(result.width, lower).value_or(0);
    const double upper_etch = table.get_table().query(result.width, upper).value_or(0);
    result.width = result.width - lower_etch - upper_etch;
    if (!positive(result.width))
      return "invalid width after etch table on " + name;
    result.center_shift += (lower_etch - upper_etch) / 2;
  }
  const double scalar = conductor.get_etch() + (domain == ProcessEffectType::kResistance
      ? conductor.get_resistive_only_etch() : conductor.get_capacitive_only_etch());
  result.width -= 2 * scalar;
  if (!positive(result.width))
    return "invalid width after scalar etch on " + name;
  double fractional_change = 0;
  for (const auto& table : conductor.get_thickness_change_table_list()) {
    if (applies(table.get_effect_type(), domain))
      fractional_change += table.get_table().query(query.width, std::min(lower, upper)).value_or(0);
  }
  result.thickness *= 1 + fractional_change;
  if (!positive(result.thickness))
    return "invalid effective thickness on " + name;
  return result;
}
ProcessResult<double> evaluateWireResistance(const ProcessConductor& conductor, const WireGeometryQuery& query, double length, double temperature,
                                             double nominal_temperature)
{
  const auto geometry = evaluateWireGeometry(conductor, query, ProcessEffectType::kResistance);
  if (!geometry)
    return geometry.error;
  const auto& name = conductor.get_layer_name();
  if (!positive(length))
    return "invalid wire length on " + name;
  const double width = geometry->width, thickness = geometry->thickness;
  const double space = std::min(spacing(query.lower_spacing), spacing(query.upper_spacing));
  double rho = 0, sheet = 0;
  std::optional<double> table;
  bool rho_table = false;
  switch (conductor.get_resistance_model()) {
    case WireResistanceModel::kResistivity: rho = conductor.get_resistivity(); break;
    case WireResistanceModel::kSheet: sheet = conductor.get_sheet_resistance(); break;
    case WireResistanceModel::kSheetByWidth:
      table = conductor.get_sheet_resistance_by_width_table().query(width);
      break;
    case WireResistanceModel::kSheetByWidthSpacing:
      table = conductor.get_sheet_resistance_by_width_spacing_table().query(width, space);
      break;
    case WireResistanceModel::kResistivityByWidthSpacing:
      table = conductor.get_resistivity_by_width_spacing_table().query(width, space);
      rho_table = true;
      break;
    case WireResistanceModel::kResistivityByWidthThickness:
      table = conductor.get_resistivity_by_width_thickness_table().query(thickness, width);
      rho_table = true;
      break;
    case WireResistanceModel::kNone:
      return "missing resistance model on " + name;
  }
  if (conductor.get_resistance_model() != WireResistanceModel::kResistivity && conductor.get_resistance_model() != WireResistanceModel::kSheet) {
    if (!table)
      return "missing or out-of-range resistance table on " + name;
    if (!positive(*table))
      return "invalid resistance value on " + name;
    (rho_table ? rho : sheet) = *table;
  }
  const double base = rho > 0 ? rho * length / (width * thickness)
      : sheet * length / width * conductor.get_thickness() / thickness;
  double first = 0, second = 0;
  conductor.query_tmpr_coefficient(width, first, second);
  const double nominal = conductor.get_has_nominal_tmpr() ? conductor.get_nominal_tmpr() : nominal_temperature;
  return temperatureAdjusted(base, temperature, nominal, first, second, "wire resistance on " + name);
}
ProcessResult<double> evaluateViaResistance(const ProcessVia& via, double width, double length, double temperature, double nominal_temperature)
{
  const auto& name = via.get_layer_name();
  if (const auto* issue = findEvaluationIssue(via.get_evaluation_issues(), ProcessEffectType::kResistance))
    return "cannot evaluate via " + name + ": " + issue->field + ": " + issue->message;
  if (!positive(width))
    return "invalid via width on " + name;
  if (!positive(length))
    return "invalid via length on " + name;
  const auto etch = via.query_etch(ProcessEffectType::kResistance, width, length);
  if (!etch)
    return etch.error;
  width -= 2 * etch->get_width();
  length -= 2 * etch->get_length();
  if (!positive(width))
    return "invalid via width after etch on " + name;
  if (!positive(length))
    return "invalid via length after etch on " + name;
  const double area = width * length;
  if (!positive(area))
    return "invalid via area on " + name;
  auto base = via.query_resistance(area);
  if (!base)
    return base.error;
  if (!positive(*base))
    return "invalid resistance value on " + name;
  double first = 0, second = 0;
  via.query_tmpr_coefficient(area, first, second);
  const double nominal = via.get_has_nominal_tmpr() ? via.get_nominal_tmpr() : nominal_temperature;
  return temperatureAdjusted(*base, temperature, nominal, first, second, "via resistance on " + name);
}
}  // namespace ircx
