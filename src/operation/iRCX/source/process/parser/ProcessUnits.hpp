#pragma once
#include "ProcessSyntax.hpp"

namespace ircx::process_detail {
enum class Dimension { kScalar, kLength, kArea, kResistance, kResistivity };
struct ProcessUnits
{
  double length = 1.0;      // microns per declared unit
  double resistance = 1.0;  // ohms per declared unit
  Result<double> convert(const Quantity& quantity, Dimension dimension) const;
};
Result<ProcessUnits> readUnits(const Document& document);
}  // namespace ircx::process_detail
