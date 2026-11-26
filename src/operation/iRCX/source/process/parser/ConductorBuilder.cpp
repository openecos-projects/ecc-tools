#include "ProcessBuilder.hpp"

#include <set>

namespace ircx::process_detail {
namespace {
using D = Dimension;
using R = WireResistanceModel;

Status selectResistance(ProcessConductor& model, const Field& layer, SyntaxKind kind, const std::vector<R>& candidates)
{
  if (candidates.empty())
    return fail(layer, "missing_resistance", "conductor requires a resistance model");
  if (kind == SyntaxKind::kItf && candidates.size() != 1)
    return fail(layer, "conflicting_resistance", "conductor resistance specifications are mutually exclusive");
  const std::set<R> present(candidates.begin(), candidates.end());
  if (present.contains(R::kResistivity) && candidates.size() != 1)
    return fail(layer, "conflicting_resistance", "scalar RHO cannot be combined with another resistance specification");
  // Resolve the table precedence once. Evaluation never inspects input syntax.
  for (R choice : {R::kResistivityByWidthThickness, R::kResistivityByWidthSpacing, R::kSheetByWidthSpacing,
                   R::kSheetByWidth, R::kResistivity, R::kSheet}) {
    if (present.contains(choice)) {
      model.set_resistance_model(choice);
      return {};
    }
  }
  return {};
}
}  // namespace

Status normalizeConductorEtch(ProcessConductor& model, const Field& layer, const ProcessUnits& units)
{
  const auto original = model.get_etch_table_list();
  model.get_etch_table_list().clear();
  model.set_etch(0);
  model.set_resistive_only_etch(0);
  model.set_capacitive_only_etch(0);
  for (auto domain : {ProcessEffectType::kResistance, ProcessEffectType::kCapacitance}) {
    const auto* scalar_field = find(layer, domain == ProcessEffectType::kResistance ? "RESISTIVE_ONLY_ETCH" : "CAPACITIVE_ONLY_ETCH");
    const ProcessEtchTable* selected = nullptr;
    for (const auto& table : original) {
      if (table.get_effect_type() != ProcessEffectType::kBoth && table.get_effect_type() != domain) continue;
      if (selected)
        return fail(layer, "conflicting_etch", "overlapping etch tables for the same effect");
      selected = &table;
    }
    // Resistance-only scalar takes priority over its table; the C table precedes its scalar.
    if (selected && (domain == ProcessEffectType::kCapacitance || !scalar_field)) {
      auto table = *selected;
      table.set_effect_type(domain);
      model.get_etch_table_list().push_back(std::move(table));
      continue;
    }
    if (!scalar_field) scalar_field = find(layer, "ETCH");
    if (!scalar_field) scalar_field = find(layer, "WIDTH_ADJUST");
    // This syntax specifies total width reduction; the runtime model stores reduction per side.
    auto value = scalar_field ? scalar(*scalar_field, units, D::kLength) : Result<double>(0.0);
    if (!value)
      return value.error;
    const double etch = *value / 2;
    if (domain == ProcessEffectType::kResistance) model.set_resistive_only_etch(etch);
    else model.set_capacitive_only_etch(etch);
  }
  return {};
}

Result<ProcessConductor> buildConductor(const Field& layer, SyntaxKind kind, const ProcessUnits& units, std::vector<ProcessDiagnostic>& diagnostics)
{
  auto unique = uniqueFields(layer, {"ETCH_VS_WIDTH_AND_SPACING", "THICKNESS_VS_WIDTH_AND_SPACING"});
  if (!unique)
    return unique.error;
  ProcessConductor model;
  model.set_layer_name(layer.name);
  std::vector<R> resistance;
  bool nominal_set = false;
  for (const auto& field : layer.children) {
    const auto& key = field.key;
    if (key == "LAYER_TYPE") {
      if (field.references.size() != 1 || field.references.front().empty() || !field.values.empty() || !field.children.empty() || !field.modifiers.empty())
        return fail(field, "invalid_value", "LAYER_TYPE requires one name");
      model.set_layer_type(field.references.front());
    } else if (isOneOf(key, {"THICKNESS", "RPSQ", "RESISTANCE", "RHO", "T0", "TNOM", "CRT1", "COEFF_RT1", "CRT2", "COEFF_RT2", "ETCH", "RESISTIVE_ONLY_ETCH",
                             "CAPACITIVE_ONLY_ETCH", "WIDTH_ADJUST"})) {
      const auto dimension = isOneOf(key, {"THICKNESS", "ETCH", "RESISTIVE_ONLY_ETCH", "CAPACITIVE_ONLY_ETCH", "WIDTH_ADJUST"}) ? D::kLength
                             : isOneOf(key, {"RPSQ", "RESISTANCE"})                                                             ? D::kResistance
                             : key == "RHO"                                                                                     ? D::kResistivity
                                                                                                                                : D::kScalar;
      auto value = scalar(field, units, dimension);
      if (!value)
        return value.error;
      if (isOneOf(key, {"THICKNESS", "RPSQ", "RESISTANCE", "RHO"})) {
        auto valid = positive(*value, field);
        if (!valid)
          return valid.error;
      }
      if (key == "THICKNESS")
        model.set_thickness(*value);
      else if (key == "RPSQ" || key == "RESISTANCE") {
        model.set_sheet_resistance(*value);
        resistance.push_back(R::kSheet);
      } else if (key == "RHO") {
        model.set_resistivity(*value);
        resistance.push_back(R::kResistivity);
      } else if (key == "T0" || key == "TNOM") {
        if (nominal_set)
          return fail(field, "duplicate_field", "reference temperature already specified");
        model.set_nominal_tmpr(*value);
        nominal_set = true;
      } else if (key == "CRT1" || key == "COEFF_RT1")
        model.set_tmpr_coefficient1(*value);
      else if (key == "CRT2" || key == "COEFF_RT2")
        model.set_tmpr_coefficient2(*value);
      else if (key == "ETCH")
        model.set_etch(*value);
      else if (key == "RESISTIVE_ONLY_ETCH")
        model.set_resistive_only_etch(*value);
      else if (key == "CAPACITIVE_ONLY_ETCH")
        model.set_capacitive_only_etch(*value);
    } else if (key == "RPSQ_VS_SI_WIDTH") {
      auto status = table1D(field, units, D::kLength, D::kResistance, model.get_sheet_resistance_by_width_table(), nullptr, true);
      if (!status)
        return status.error;
      resistance.push_back(R::kSheetByWidth);
    } else if (key == "CRT_VS_SI_WIDTH") {
      auto status = table1D(field, units, D::kLength, D::kScalar, model.get_tmpr_coefficient1_by_width_table(), &model.get_tmpr_coefficient2_by_width_table());
      if (!status)
        return status.error;
    } else if (key == "RHO_VS_SI_WIDTH_AND_THICKNESS") {
      auto table = table2D(field, units, "THICKNESS", "WIDTH", D::kResistivity, true);
      if (!table)
        return table.error;
      model.get_resistivity_by_width_thickness_table() = std::move(*table);
      resistance.push_back(R::kResistivityByWidthThickness);
    } else if (key == "RHO_VS_WIDTH_AND_SPACING") {
      auto table = table2D(field, units, "WIDTHS", "SPACINGS", D::kResistivity, true);
      if (!table)
        return table.error;
      model.get_resistivity_by_width_spacing_table() = std::move(*table);
      resistance.push_back(R::kResistivityByWidthSpacing);
    } else if (key == "RPSQ_VS_WIDTH_AND_SPACING") {
      auto table = table2D(field, units, "WIDTHS", "SPACINGS", D::kResistance, true);
      if (!table)
        return table.error;
      model.get_sheet_resistance_by_width_spacing_table() = std::move(*table);
      resistance.push_back(R::kSheetByWidthSpacing);
    } else if (key == "ETCH_VS_WIDTH_AND_SPACING" || key == "THICKNESS_VS_WIDTH_AND_SPACING") {
      ProcessEtchTable table;
      auto domain = effect(field);
      if (!domain)
        return domain.error;
      table.set_effect_type(*domain);
      auto values = table2D(field, units, "WIDTHS", "SPACINGS", key == "ETCH_VS_WIDTH_AND_SPACING" ? D::kLength : D::kScalar);
      if (!values)
        return values.error;
      if (!field.modifiers.empty() && field.modifiers.front() == "ETCH_FROM_TOP") {
        model.set_has_top_etch(true);
        deferEvaluation(field, ProcessEffectType::kCapacitance, "trapezoidal geometry evaluation is not implemented", model.get_evaluation_issues(),
                        diagnostics);
        continue;
      }
      table.set_table(*values);
      if (key == "ETCH_VS_WIDTH_AND_SPACING") model.get_etch_table_list().push_back(std::move(table));
      else {
        for (double delta : table.get_table().get_value_list())
          if (delta <= -1)
            return fail(field, "invalid_value", "fractional thickness change must be greater than -1");
        model.get_thickness_change_table_list().push_back(std::move(table));
      }
    } else if (key == "SIDE_TANGENT") {
      const auto value = scalar(field, units);
      if (!value) return value.error;
      model.set_side_tangent(*value);
    } else if (key == "POLYNOMIAL_BASED_THICKNESS_VARIATION") {
      auto polynomial = buildThicknessPolynomial(field, kind, units);
      if (!polynomial) return polynomial.error;
      model.get_thickness_polynomial() = std::move(*polynomial);
    } else if (isOneOf(key, {"CLAD_RPSQ", "CLAD_THICKNESS", "FILL_RATIO"})) {
      auto value = scalar(field, units);
      if (!value)
        return value.error;
      if (*value != 0)
        deferEvaluation(field, ProcessEffectType::kBoth, "evaluation of this geometry model is not implemented", model.get_evaluation_issues(), diagnostics);
    } else if (isOneOf(key, {"THICKNESS_VS_DENSITY", "DENSITY_BOX_WEIGHTING_FACTOR", "BOTTOM_THICKNESS_VS_SI_WIDTH",
                             "WIDTH_SI_VS_WIDTH_AND_SPACING", "WIDTH-SPACE"})) {
      auto domain = effect(field);
      if (!domain)
        return domain.error;
      auto valid = validateDeferredModel(field, units);
      if (!valid)
        return valid.error;
      if (key == "DENSITY_BOX_WEIGHTING_FACTOR" && find(layer, "POLYNOMIAL_BASED_THICKNESS_VARIATION"))
        continue;
      deferEvaluation(field, *domain, "evaluation of this geometry model is not implemented", model.get_evaluation_issues(), diagnostics);
    } else if (isOneOf(key, {"MINWIDTH",
                             "MINSPACE",
                             "WMIN",
                             "SMIN",
                             "PITCH",
                             "HEIGHT",
                             "ABOVE",
                             "EPSILON",
                             "MEASURED_FROM",
                             "GATE_TO_CONTACT_SMIN",
                             "GATE_TO_DIFFUSION_CAP",
                             "ILD_VS_WIDTH_AND_SPACING",
                             "T",
                             "CAP_DENSITY",
                             "FILL_SPACING",
                             "FILL_WIDTH",
                             "FILL_TYPE",
                             "DROP_FACTOR",
                             "EM",
                             "T_EM",
                             "TNOM_EM",
                             "EM_TEMP_RATING",
                             "EM_ADJUST",
                             "EM_THICKADJUST",
                             "WIDTH_BASED_EM",
                             "BLECH_JLC",
                             "EM_TIME_CURRENT",
                             "POLYNOMIAL_BASED_EM_DC",
                             "POLYNOMIAL_BASED_EM_RMS",
                             "POLYNOMIAL_BASED_EM_PEAK"})) {
      ignored(field, diagnostics);
    } else
      return fail(field, "unknown_field", "unsupported conductor field " + key);
  }
  if (model.get_thickness() <= 0)
    return fail(layer, "missing_thickness", "conductor requires a positive THICKNESS");
  auto selected = selectResistance(model, layer, kind, resistance);
  if (!selected)
    return selected.error;
  return model;
}
}  // namespace ircx::process_detail
