#pragma once

#include <optional>
#include <string>
#include <vector>

#include "ProcessConductor.hpp"
#include "ProcessProperty.hpp"
#include "ProcessVia.hpp"

namespace ircx {

// Owning, layout-independent process data. Lengths are microns, resistance ohms.
struct ProcessTechnology
{
  std::string source_name;
  // Preserve the complete input, including expressions not normalized below.
  std::string source_text;
  std::vector<ProcessProperty> properties;
  std::optional<bool> use_si_density;
  double nominal_temperature = 25.0;
  double geometry_scale = 1.0;
  std::vector<ProcessConductor> conductors;
  std::vector<ProcessVia> vias;
};

}  // namespace ircx
