#include "ProcessBuilder.hpp"

#include <algorithm>
#include <cmath>
#include <set>

namespace ircx::process_detail {
bool isOneOf(std::string_view key, std::initializer_list<std::string_view> keys)
{
  return std::find(keys.begin(), keys.end(), key) != keys.end();
}
Failure fail(const Field& field, std::string code, std::string message)
{
  return {std::move(code), field.position, field.key, std::move(message)};
}
const Field* find(const Field& field, std::string_view key)
{
  for (const auto& child : field.children) if (child.key == key) return &child;
  return nullptr;
}
Status uniqueFields(const Field& field, std::initializer_list<std::string_view> repeatable)
{
  std::set<std::string> seen;
  for (const auto& child : field.children) {
    if (!seen.insert(child.key).second && !isOneOf(child.key, repeatable))
      return fail(child, "duplicate_field", "duplicate " + child.key);
  }
  return {};
}
Result<double> scalar(const Field& field, const ProcessUnits& units, Dimension dimension)
{
  if (field.values.size() != 1 || !field.references.empty() || !field.children.empty() || !field.modifiers.empty())
    return fail(field, "invalid_value", "expected one numeric value for " + field.key);
  return units.convert(field.values.front(), dimension);
}
Status positive(double value, const Field& field)
{
  if (!std::isfinite(value) || value <= 0)
    return fail(field, "invalid_value", field.key + " must be positive and finite");
  return {};
}
Result<ProcessEffectType> effect(const Field& field)
{
  if (field.modifiers.size() > 1)
    return fail(field, "invalid_option", "conflicting effect options");
  ProcessEffectType result = ProcessEffectType::kBoth;
  for (const auto& option : field.modifiers) {
    if (option == "ETCH_FROM_TOP") {
      if (field.key != "ETCH_VS_WIDTH_AND_SPACING")
        return fail(field, "invalid_option", "ETCH_FROM_TOP requires a conductor etch table");
      return ProcessEffectType::kCapacitance;
    }
    const auto value = GetProcessEffectType()(option);
    if (value == ProcessEffectType::kNone || result != ProcessEffectType::kBoth)
      return fail(field, "invalid_option", "conflicting or unknown effect option");
    result = value;
  }
  return result;
}
namespace {
Result<std::vector<double>> axis(const Field* field, const ProcessUnits& units)
{
  if (!field)
    return std::vector<double>{0};  // Independent of the missing dimension.
  if (field->values.empty() || !field->children.empty() || !field->references.empty())
    return fail(*field, "invalid_table", "axis must be a nonempty numeric list");
  std::vector<double> values;
  for (const auto& q : field->values) {
    auto value = units.convert(q, Dimension::kLength);
    if (!value)
      return value.error;
    auto valid = positive(*value, *field);
    if (!valid)
      return valid.error;
    if (!values.empty() && *value <= values.back())
      return fail(*field, "invalid_table", "axis must be strictly increasing after unit conversion");
    values.push_back(*value);
  }
  return values;
}
}
Result<ProcessTable2D> table2D(const Field& field, const ProcessUnits& units, std::string_view row, std::string_view column, Dimension dimension,
                               bool positive_values)
{
  auto unique = uniqueFields(field);
  if (!unique)
    return unique.error;
  for (const auto& child : field.children)
    if (!isOneOf(child.key, {row, column, "VALUES"}))
      return fail(child, "invalid_table", "unexpected table field " + child.key);
  const auto* rows = find(field, row);
  const auto* columns = find(field, column);
  const auto* data = find(field, "VALUES");
  if ((!rows && !columns) || !data || !field.values.empty() || !field.references.empty())
    return fail(field, "invalid_table", "table requires an axis and VALUES");
  auto row_values = axis(rows, units);
  if (!row_values)
    return row_values.error;
  auto column_values = axis(columns, units);
  if (!column_values)
    return column_values.error;
  if (!data->children.empty() || !data->references.empty() || data->values.size() != row_values->size() * column_values->size())
    return fail(*data, "invalid_table", "matrix dimensions do not match axes");
  std::vector<double> values;
  for (const auto& q : data->values) {
    auto value = units.convert(q, dimension);
    if (!value)
      return value.error;
    if (positive_values) {
      auto valid = positive(*value, *data);
      if (!valid)
        return valid.error;
    }
    values.push_back(*value);
  }
  ProcessTable2D result;
  result.set_row_list(*row_values);
  result.set_column_list(*column_values);
  result.set_value_list(values);
  return result;
}
Status table1D(const Field& field, const ProcessUnits& units, Dimension key_dimension, Dimension value_dimension, ProcessTable1D& first, ProcessTable1D* second,
               bool positive_values)
{
  const std::size_t stride = second ? 3 : 2;
  if (field.values.empty() || field.values.size() % stride || !field.children.empty() || !field.references.empty() || !field.modifiers.empty())
    return fail(field, "invalid_table", "expected complete numeric rows");
  double previous = 0;
  for (std::size_t i = 0; i < field.values.size(); i += stride) {
    auto key = units.convert(field.values[i], key_dimension);
    if (!key)
      return key.error;
    auto valid = positive(*key, field);
    if (!valid)
      return valid;
    if (i && *key <= previous)
      return fail(field, "invalid_table", "axis must be strictly increasing after unit conversion");
    previous = *key;
    auto value = units.convert(field.values[i + 1], value_dimension);
    if (!value)
      return value.error;
    if (positive_values) {
      auto status = positive(*value, field);
      if (!status)
        return status;
    }
    first.add_entry(*key, *value);
    if (second) {
      auto other = units.convert(field.values[i + 2], value_dimension);
      if (!other)
        return other.error;
      second->add_entry(*key, *other);
    }
  }
  return {};
}
void ignored(const Field& field, std::vector<ProcessDiagnostic>& diagnostics)
{
  diagnostics.push_back({ProcessSeverity::kWarning,
                         "unused_metadata",
                         {},
                         field.position,
                         field.position,
                         field.key,
                         field.key + " is retained as source metadata and is not used by the current evaluator"});
}
void deferEvaluation(const Field& field, ProcessEffectType effect, std::string message, std::vector<ProcessEvaluationIssue>& issues,
                     std::vector<ProcessDiagnostic>& diagnostics)
{
  issues.push_back({field.key, effect, std::move(message)});
  diagnostics.push_back(
      {ProcessSeverity::kWarning, "unevaluated_model", {}, field.position, field.position, field.key, field.key + " is retained; " + issues.back().message});
}
Result<ProcessTechnology> buildTechnology(const Document& document, SyntaxKind kind, std::vector<ProcessDiagnostic>& diagnostics)
{
  ProcessTechnology model;
  auto units = kind == SyntaxKind::kTech ? readUnits(document) : Result<ProcessUnits>(ProcessUnits{});
  if (!units)
    return units.error;
  std::set<std::string> layers, globals;
  for (const auto& field : document.fields) {
    if (isOneOf(field.key, {"CONDUCTOR", "METAL", "VIA"})) {
      if (field.name.empty())
        return fail(field, "invalid_layer", "layer name must not be empty");
      if (!layers.insert(field.name).second)
        return fail(field, "duplicate_layer", "duplicate layer " + field.name);
      auto qualify = [&](Failure error) {
        error.field = field.key + "[" + field.name + "]." + error.field;
        return error;
      };
      if (field.key == "VIA") {
        auto via = buildVia(field, kind, *units, diagnostics);
        if (!via)
          return qualify(via.error);
        model.vias.push_back(std::move(*via));
      } else {
        auto conductor = buildConductor(field, kind, *units, diagnostics);
        if (!conductor)
          return qualify(conductor.error);
        model.conductors.push_back(std::move(*conductor));
      }
    } else if (field.key == "USE_SI_DENSITY") {
      if (!globals.insert(field.key).second)
        return fail(field, "duplicate_field", "duplicate " + field.key);
      if (field.references.size() != 1 || !field.values.empty() || !field.children.empty() || !field.modifiers.empty()
          || !isOneOf(upper(field.references.front()), {"YES", "NO"}))
        return fail(field, "invalid_value", "USE_SI_DENSITY requires YES or NO");
      model.use_si_density = upper(field.references.front()) == "YES";
    } else if (isOneOf(field.key, {"GLOBAL_TEMPERATURE", "HALF_NODE_SCALE_FACTOR"})) {
      if (!globals.insert(field.key).second)
        return fail(field, "duplicate_field", "duplicate " + field.key);
      auto value = scalar(field, *units);
      if (!value)
        return value.error;
      if (field.key == "GLOBAL_TEMPERATURE")
        model.nominal_temperature = *value;
      else {
        auto valid = positive(*value, field);
        if (!valid)
          return valid.error;
        model.geometry_scale = *value;
      }
    } else if (field.key == "UNITS" || field.key == "TECHNOLOGY") {
      // Syntax and units were validated before layer normalization.
    } else if (isOneOf(field.key, {"DIELECTRIC", "SUBSTRATE", "VIAMODEL", "VDD_PAD_RC", "GND_PAD_RC", "VDD_WIRE_BOND_OR_BUMP_RLC", "GND_WIRE_BOND_OR_BUMP_RLC",
                                   "VDD_PACKAGE_RLC", "VSS_PACKAGE_RLC"})) {
      ignored(field, diagnostics);
    } else
      return fail(field, "unknown_field", "unsupported process field " + field.key);
  }
  if (model.conductors.empty() && model.vias.empty())
    return Failure{"empty_process", {}, {}, "document contains no conductor or via resistance model"};
  std::size_t conductor_index = 0;
  for (const auto& field : document.fields) {
    if (!isOneOf(field.key, {"CONDUCTOR", "METAL"})) continue;
    auto& conductor = model.conductors[conductor_index++];
    const auto resolved = resolveThicknessPolynomial(conductor, field, model.use_si_density.value_or(false), *units);
    if (!resolved) return resolved.error;
    // Defaults refer to source etch declarations. Resolve them before splitting
    // a native table into the independent R/C runtime operations.
    if (kind == SyntaxKind::kTech) {
      const auto normalized = normalizeConductorEtch(conductor, field, *units);
      if (!normalized) return normalized.error;
    }
  }
  model.properties = document.fields;
  return model;
}
}  // namespace ircx::process_detail
