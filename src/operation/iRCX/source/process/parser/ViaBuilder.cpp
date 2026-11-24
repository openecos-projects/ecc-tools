#include "ProcessBuilder.hpp"

#include <cmath>

namespace ircx::process_detail {
namespace {
using D = Dimension;
Result<std::string> reference(const Field& field)
{
  if (field.references.size() != 1 || field.references.front().empty() || !field.values.empty() || !field.children.empty())
    return fail(field, "unsupported_physics", "exactly one connected layer is required");
  return field.references.front();
}
Result<ProcessViaEtchTable> viaEtch(const Field& field, const ProcessUnits& units)
{
  const auto* data = find(field, "VALUES");
  if (!data || data->values.size() % 2)
    return fail(field, "invalid_table", "via etch requires length/width pairs");
  auto length_field = field, width_field = field;
  for (auto& child : length_field.children) if (child.key == "VALUES") {
    child.values.clear();
    for (std::size_t i = 0; i < data->values.size(); i += 2) child.values.push_back(data->values[i]);
  }
  for (auto& child : width_field.children) if (child.key == "VALUES") {
    child.values.clear();
    for (std::size_t i = 1; i < data->values.size(); i += 2) child.values.push_back(data->values[i]);
  }
  ProcessViaEtchTable table;
  auto domain = effect(field);
  if (!domain)
    return domain.error;
  table.set_effect_type(*domain);
  auto length = table2D(length_field, units, "WIDTHS", "LENGTHS", D::kLength);
  if (!length)
    return length.error;
  table.set_length_table(*length);
  auto width = table2D(width_field, units, "WIDTHS", "LENGTHS", D::kLength);
  if (!width)
    return width.error;
  table.set_width_table(*width);
  return table;
}
}
Result<ProcessVia> buildVia(const Field& layer, SyntaxKind kind, const ProcessUnits& units, std::vector<ProcessDiagnostic>& diagnostics)
{
  auto unique = uniqueFields(layer, {"ETCH_VS_WIDTH_AND_LENGTH"});
  if (!unique)
    return unique.error;
  ProcessVia model;
  model.set_layer_name(layer.name);
  bool nominal_set = false, scalar_resistance = false, table_resistance = false, deferred_resistance = false;
  std::optional<double> width;
  for (const auto& field : layer.children) {
    const auto& key = field.key;
    if (isOneOf(key, {"FROM", "LOWERLAYER", "TO", "UPPERLAYER"})) {
      auto name = reference(field);
      if (!name)
        return name.error;
      if (key == "FROM" || key == "LOWERLAYER")
        model.set_from_layer_name(*name);
      else
        model.set_to_layer_name(*name);
    } else if (isOneOf(key, {"AREA", "WIDTH", "RPV", "RESISTANCE", "T0", "TNOM", "CRT1", "COEFF_RT1", "CRT2", "COEFF_RT2"})) {
      auto dimension = key == "AREA" ? D::kArea : key == "WIDTH" ? D::kLength : isOneOf(key, {"RPV", "RESISTANCE"}) ? D::kResistance : D::kScalar;
      auto value = scalar(field, units, dimension);
      if (!value)
        return value.error;
      if (isOneOf(key, {"AREA", "WIDTH", "RPV", "RESISTANCE"})) {
        auto valid = positive(*value, field);
        if (!valid)
          return valid.error;
      }
      if (key == "AREA")
        model.set_area(*value);
      else if (key == "WIDTH")
        width = *value;
      else if (key == "RPV" || key == "RESISTANCE") {
        model.set_resistance(*value);
        scalar_resistance = true;
      } else if (key == "T0" || key == "TNOM") {
        if (nominal_set)
          return fail(field, "duplicate_field", "reference temperature already specified");
        model.set_nominal_tmpr(*value);
        nominal_set = true;
      } else if (key == "CRT1" || key == "COEFF_RT1")
        model.set_tmpr_coefficient1(*value);
      else if (key == "CRT2" || key == "COEFF_RT2")
        model.set_tmpr_coefficient2(*value);
    } else if (key == "RPV_VS_AREA") {
      auto status = table1D(field, units, D::kArea, D::kResistance, model.get_resistance_by_area_table(), nullptr, true);
      if (!status)
        return status.error;
      table_resistance = true;
      if (kind == SyntaxKind::kTech) model.get_resistance_by_area_table().set_boundary_policy(ProcessTable1D::BoundaryPolicy::kReject);
    } else if (key == "CRT_VS_AREA") {
      auto status = table1D(field, units, D::kArea, D::kScalar, model.get_tmpr_coefficient1_by_area_table(), &model.get_tmpr_coefficient2_by_area_table());
      if (!status)
        return status.error;
    } else if (key == "ETCH_VS_WIDTH_AND_LENGTH") {
      auto table = viaEtch(field, units);
      if (!table)
        return table.error;
      model.get_etch_table_list().push_back(std::move(*table));
    } else if (key == "CAPACITIVE_ONLY_ETCH") {
      auto value = scalar(field, units, D::kLength);
      if (!value)
        return value.error;
      if (*value != 0)
        deferEvaluation(field, ProcessEffectType::kCapacitance, "evaluation of this via geometry model is not implemented", model.get_evaluation_issues(),
                        diagnostics);
    } else if (key == "ETCH_VS_CONTACT_AND_GATE_SPACINGS") {
      auto domain = effect(field);
      if (!domain)
        return domain.error;
      auto valid = validateDeferredModel(field, units);
      if (!valid)
        return valid.error;
      deferEvaluation(field, *domain, "evaluation of this via geometry model is not implemented", model.get_evaluation_issues(), diagnostics);
    } else if (key == "RPV_VS_NUM" || key == "RHO") {
      if (key == "RHO") {
        auto value = scalar(field, units, D::kResistivity);
        if (!value)
          return value.error;
        auto valid = positive(*value, field);
        if (!valid)
          return valid.error;
      } else {
        auto valid = validateDeferredModel(field, units);
        if (!valid)
          return valid.error;
      }
      deferred_resistance = true;
      deferEvaluation(field, ProcessEffectType::kResistance, "evaluation of this via resistance model is not implemented", model.get_evaluation_issues(),
                      diagnostics);
    } else if (isOneOf(key, {"T", "EM", "EM_TEMP_RATING", "TOPOLOGY_BASED_EM", "EMV_VS_AREA", "DIRECTION_BASED_EM_TEMP_RATING", "POLYNOMIAL_BASED_EM_DC",
                             "POLYNOMIAL_BASED_EM_RMS", "POLYNOMIAL_BASED_EM_PEAK", "TNOM_EM", "T_EM"}))
      ignored(field, diagnostics);
    else
      return fail(field, "unknown_field", "unsupported via field " + key);
  }
  if (width) {
    const double area = *width * *width;
    auto valid = positive(area, layer);
    if (!valid)
      return valid.error;
    if (model.get_area() > 0 && std::abs(area - model.get_area()) > 1e-9 * std::max(area, model.get_area()))
      return fail(layer, "conflicting_geometry", "WIDTH and AREA specify different reference cuts");
    model.set_area(area);
  }
  if (!scalar_resistance && !table_resistance && !deferred_resistance)
    return fail(layer, "missing_resistance", "via requires a resistance model");
  if (kind == SyntaxKind::kItf && scalar_resistance && table_resistance)
    return fail(layer, "conflicting_resistance", "via resistance specifications are mutually exclusive");
  if (table_resistance) model.set_resistance_model(ViaResistanceModel::kAreaTable);
  else {
    if (scalar_resistance && model.get_area() <= 0) {
      diagnostics.push_back({ProcessSeverity::kWarning,
                             "missing_reference_area",
                             {},
                             layer.position,
                             layer.position,
                             "VIA[" + layer.name + "].AREA",
                             "scalar via resistance evaluation requires a reference AREA or WIDTH"});
    }
    model.set_resistance_model(kind == SyntaxKind::kItf ? ViaResistanceModel::kReferenceArea : ViaResistanceModel::kReferenceCut);
  }
  return model;
}
}  // namespace ircx::process_detail
