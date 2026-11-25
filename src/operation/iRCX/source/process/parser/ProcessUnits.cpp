#include "ProcessUnits.hpp"

#include <cmath>
#include <map>

namespace ircx::process_detail {
namespace {
Result<double> suffixScale(const Quantity& q, Dimension dimension)
{
  static const std::map<std::string, double> prefixes{{"",1},{"t",1e12},{"g",1e9},{"M",1e6},{"me",1e6},
      {"k",1e3},{"m",1e-3},{"u",1e-6},{"n",1e-9},{"p",1e-12},{"f",1e-15}};
  auto suffix = q.suffix;
  if (dimension == Dimension::kLength) {
    if (suffix == "micron")
      return 1.0;
    if (suffix == "mil") return 25.4;
    if (suffix == "inch")
      return 25400.0;
    if (suffix.ends_with('m')) suffix.pop_back();
    if (auto p = prefixes.find(suffix); p != prefixes.end()) return p->second * 1e6;
  } else if (dimension == Dimension::kResistance) {
    if (suffix.ends_with("ohm")) suffix.resize(suffix.size() - 3);
    if (auto p = prefixes.find(suffix); p != prefixes.end()) return p->second;
  }
  return Failure("invalid_unit", q.position, {}, "unit suffix has the wrong dimension: " + q.suffix);
}
}
Result<double> ProcessUnits::convert(const Quantity& quantity, Dimension dimension) const
{
  double factor = 1.0;
  if (!quantity.suffix.empty()) {
    auto scale = suffixScale(quantity, dimension);
    if (!scale)
      return scale.error;
    factor = *scale;
  } else {
    switch (dimension) {
      case Dimension::kScalar: break;
      case Dimension::kLength: factor = length; break;
      case Dimension::kArea: factor = length * length; break;
      case Dimension::kResistance: factor = resistance; break;
      case Dimension::kResistivity: factor = resistance * length; break;
    }
  }
  const double result = quantity.value * factor;
  if (!std::isfinite(result))
    return Failure("invalid_number", quantity.position, {}, "nonfinite normalized value");
  return result;
}

Result<ProcessUnits> readUnits(const Document& document)
{
  ProcessUnits units;
  bool found = false;
  for (const auto& field : document.fields) {
    if (field.key != "UNITS") continue;
    if (found)
      return Failure("duplicate_field", field.position, field.key, "duplicate UNITS block");
    found = true;
    std::map<std::string, bool> seen;
    for (const auto& unit : field.children) {
      if (!seen.emplace(unit.key, true).second)
        return Failure("duplicate_field", unit.position, unit.key, "duplicate unit");
      if (unit.values.size() != 1)
        return Failure("invalid_unit", unit.position, unit.key, "unit requires one quantity");
      if (unit.key != "LENGTH" && unit.key != "RESISTANCE") continue;
      const auto& q = unit.values.front();
      const auto dimension = unit.key == "LENGTH" ? Dimension::kLength : Dimension::kResistance;
      // Unsuffixed declarations denote SI base units, unlike unsuffixed field values.
      auto factor = q.suffix.empty() ? Result<double>(dimension == Dimension::kLength ? 1e6 : 1.0) : suffixScale(q, dimension);
      if (!factor)
        return factor.error;
      const double scale = q.value * *factor;
      if (!std::isfinite(scale) || scale <= 0)
        return Failure("invalid_unit", q.position, unit.key, "unit scale must be positive and finite");
      if (dimension == Dimension::kLength) units.length = scale;
      else units.resistance = scale;
    }
  }
  return units;
}
}  // namespace ircx::process_detail
