#include <cmath>
#include <set>

#include "ProcessBuilder.hpp"

namespace ircx::process_detail {
namespace {
using D = Dimension;

Status numericValues(const Field& field, const ProcessUnits& units, D dimension, bool allow_modifiers = false)
{
  if (field.values.empty() || !field.references.empty() || !field.children.empty() || !field.name.empty())
    return fail(field, "invalid_table", "expected a nonempty numeric list");
  if (!allow_modifiers && !field.modifiers.empty())
    return fail(field, "invalid_option", "numeric list does not accept modifiers");
  for (const auto& quantity : field.values) {
    auto value = units.convert(quantity, dimension);
    if (!value)
      return value.error;
  }
  return {};
}

Status numericRows(const Field& field, const ProcessUnits& units, std::initializer_list<D> dimensions, bool allow_modifiers = false)
{
  if (field.values.empty() || field.values.size() % dimensions.size() || !field.references.empty() || !field.children.empty() || !field.name.empty())
    return fail(field, "invalid_table", "expected complete numeric rows");
  if (!allow_modifiers && !field.modifiers.empty())
    return fail(field, "invalid_option", "table does not accept modifiers");
  std::size_t index = 0;
  while (index < field.values.size()) {
    for (auto dimension : dimensions) {
      auto value = units.convert(field.values[index++], dimension);
      if (!value)
        return value.error;
    }
  }
  return {};
}

Status increasingWidths(const Field& field, const ProcessUnits& units)
{
  auto valid = numericValues(field, units, D::kLength);
  if (!valid)
    return valid;
  double previous = 0;
  for (const auto& quantity : field.values) {
    const double width = *units.convert(quantity, D::kLength);
    if (width <= previous)
      return fail(field, "invalid_table", "widths must be positive and strictly increasing");
    previous = width;
  }
  return {};
}

Status polynomial(const Field& field, const ProcessUnits& units)
{
  auto unique = uniqueFields(field, {"POLYNOMIAL_COEFFICIENTS"});
  if (!unique)
    return unique;
  if (!field.values.empty() || !field.references.empty() || !field.modifiers.empty())
    return fail(field, "invalid_table", "polynomial requires property blocks");
  const Field* density = nullptr;
  const Field* width = nullptr;
  const Field* ranges = nullptr;
  std::size_t coefficient_count = 0;
  for (const auto& child : field.children) {
    if (isOneOf(child.key, {"DENSITY_POLYNOMIAL_ORDERS", "SI_DENSITY_POLYNOMIAL_ORDERS", "WIDTH_POLYNOMIAL_ORDERS", "SI_WIDTH_POLYNOMIAL_ORDERS"})) {
      const bool density_orders = isOneOf(child.key, {"DENSITY_POLYNOMIAL_ORDERS", "SI_DENSITY_POLYNOMIAL_ORDERS"});
      auto*& orders = density_orders ? density : width;
      if (orders)
        return fail(child, "duplicate_field", "polynomial orders already specified");
      orders = &child;
      auto valid = numericValues(child, units, D::kScalar);
      if (!valid)
        return valid;
      std::set<double> seen;
      for (const auto& quantity : child.values)
        if (std::floor(quantity.value) != quantity.value || !seen.insert(quantity.value).second)
          return fail(child, "invalid_table", "polynomial orders must be distinct integers");
    } else if (child.key == "WIDTH_RANGES") {
      ranges = &child;
      auto valid = increasingWidths(child, units);
      if (!valid)
        return valid;
    } else if (child.key == "POLYNOMIAL_COEFFICIENTS") {
      ++coefficient_count;
      auto valid = numericValues(child, units, D::kScalar);
      if (!valid)
        return valid;
    } else if (child.key == "DENSITY_BOUNDS_VS_WIDTH") {
      auto valid = numericRows(child, units, {D::kLength, D::kScalar, D::kScalar});
      if (!valid)
        return valid;
      double previous = 0;
      for (std::size_t i = 0; i < child.values.size(); i += 3) {
        const double bound_width = *units.convert(child.values[i], D::kLength);
        if (bound_width <= previous || child.values[i + 1].value > child.values[i + 2].value)
          return fail(child, "invalid_table", "density bounds require increasing widths and ordered bounds");
        previous = bound_width;
      }
    } else if (child.key == "THICKNESS_BOUNDS") {
      auto valid = numericValues(child, units, D::kLength);
      if (!valid)
        return valid;
      if (child.values.size() != 2 || *units.convert(child.values[0], D::kLength) >= *units.convert(child.values[1], D::kLength))
        return fail(child, "invalid_table", "thickness bounds require one minimum and one larger maximum");
    } else {
      return fail(child, "unknown_field", "unsupported polynomial field " + child.key);
    }
  }
  if (!density || !width)
    return fail(field, "invalid_table", "polynomial requires density and width orders");
  if (coefficient_count != (ranges ? ranges->values.size() + 1 : 1))
    return fail(field, "invalid_table", "expected one coefficient table per width interval");
  for (const auto& child : field.children)
    if (child.key == "POLYNOMIAL_COEFFICIENTS" && child.values.size() != density->values.size() * width->values.size())
      return fail(child, "invalid_table", "coefficient count does not match polynomial orders");
  return {};
}

Status contactEtch(const Field& field, const ProcessUnits& units)
{
  auto unique = uniqueFields(field);
  if (!unique)
    return unique;
  auto domain = effect(field);
  if (!domain)
    return domain.error;
  if (*domain != ProcessEffectType::kCapacitance)
    return fail(field, "invalid_option", "contact etch requires CAPACITIVE_ONLY");
  if (!field.values.empty() || !field.references.empty())
    return fail(field, "invalid_table", "contact etch requires table fields");
  auto validate_table = [&](const Field& table) -> Status {
    if (!find(table, "CONTACT_TO_CONTACT_SPACINGS") || !find(table, "GATE_TO_CONTACT_SPACINGS"))
      return fail(table, "invalid_table", "contact etch requires both spacing axes");
    auto values = table2D(table, units, "CONTACT_TO_CONTACT_SPACINGS", "GATE_TO_CONTACT_SPACINGS", D::kLength);
    if (!values)
      return values.error;
    return {};
  };
  const auto* count = find(field, "NUMBER_OF_TABLES");
  if (!count)
    return validate_table(field);
  auto number = scalar(*count, units);
  if (!number)
    return number.error;
  if (*number < 1 || std::floor(*number) != *number || *number != static_cast<double>(field.children.size() - 1))
    return fail(*count, "invalid_table", "NUMBER_OF_TABLES must match the named table count");
  for (const auto& table : field.children) {
    if (&table == count)
      continue;
    if (!table.modifiers.empty())
      return fail(table, "invalid_option", "named contact table does not accept modifiers");
    auto valid = validate_table(table);
    if (!valid)
      return valid;
  }
  return {};
}
}  // namespace

Status validateDeferredModel(const Field& field, const ProcessUnits& units)
{
  if (field.key == "POLYNOMIAL_BASED_THICKNESS_VARIATION")
    return polynomial(field, units);
  if (field.key == "ETCH_VS_CONTACT_AND_GATE_SPACINGS")
    return contactEtch(field, units);
  if (field.key == "WIDTH_SI_VS_WIDTH_AND_SPACING") {
    auto table = table2D(field, units, "WIDTHS", "SPACINGS", D::kLength, true);
    if (!table)
      return table.error;
    return {};
  }
  if (field.key == "RPV_VS_NUM") {
    ProcessTable1D table;
    auto valid = table1D(field, units, D::kScalar, D::kResistance, table, nullptr, true);
    if (!valid)
      return valid;
    for (std::size_t i = 0; i < field.values.size(); i += 2)
      if (std::floor(field.values[i].value) != field.values[i].value)
        return fail(field, "invalid_table", "via counts must be integers");
    return {};
  }
  if (field.key == "THICKNESS_VS_DENSITY")
    return numericRows(field, units, {D::kScalar, D::kScalar}, true);
  if (field.key == "BOTTOM_THICKNESS_VS_SI_WIDTH")
    return numericRows(field, units, {D::kLength, D::kLength}, true);
  if (field.key == "DENSITY_BOX_WEIGHTING_FACTOR")
    return numericRows(field, units, {D::kLength, D::kScalar});
  if (field.key == "WIDTH-SPACE")
    return numericRows(field, units, {D::kLength, D::kLength});
  return fail(field, "unknown_field", "unsupported deferred model " + field.key);
}
}  // namespace ircx::process_detail
