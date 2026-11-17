#pragma once

#include <filesystem>
#include <fstream>
#include <limits>
#include <sstream>
#include <unordered_map>

#include "GeometryDisplayWriter.h"

namespace ecc::geometry {

// Compatibility bridge for the prebuilt ICS55 extractor, which exposes SPEF
// rather than an in-memory RC graph. This runs once in the producer; viewers
// consume indexed geometry. Native iRCX writes from its graph directly.
inline void writeSpefDisplay(const std::filesystem::path& source)
{
  std::ifstream input(source);
  if (!input)
    throw std::runtime_error("Cannot open RCX SPEF: " + source.string());
  auto destination = source;
  destination.replace_extension(".gdisplay");
  GeometryDisplayWriter writer(destination,
                               {{"kind", "rcx"},
                                {"key", "rcx:" + source.stem().string()},
                                {"label", source.stem().string() + " · SPEF extraction"},
                                {"unit", "fF"},
                                {"source", "SPEF"}},
                               GeometryDisplayWriter::Encoding::kRecords);
  std::unordered_map<std::string, std::string> names;
  bool name_map = false;
  double scale = std::numeric_limits<double>::quiet_NaN();
  std::string line;
  while (std::getline(input, line)) {
    std::istringstream fields(line);
    std::string directive;
    if (!(fields >> directive) || directive.starts_with("//"))
      continue;
    if (directive == "*NAME_MAP") {
      name_map = true;
    } else if (directive == "*C_UNIT") {
      double value;
      std::string unit;
      if (!(fields >> value >> unit) || !std::isfinite(value) || value <= 0)
        throw std::runtime_error("Invalid SPEF capacitance units");
      if (unit == "FF")
        scale = value;
      else if (unit == "PF")
        scale = value * 1000;
      else if (unit == "NF")
        scale = value * 1000000;
      else if (unit == "UF")
        scale = value * 1000000000;
      else if (unit == "F")
        scale = value * 1.e15;
      else
        throw std::runtime_error("Unsupported SPEF capacitance units: " + unit);
      name_map = false;
    } else if (directive == "*D_NET") {
      std::string name;
      double value;
      if (!(fields >> name >> value) || !std::isfinite(value * scale) || value < 0)
        throw std::runtime_error("Invalid SPEF net capacitance");
      if (name.starts_with('*')) {
        const auto found = names.find(name);
        if (found == names.end())
          throw std::runtime_error("Unresolved SPEF net name: " + name);
        name = found->second;
      }
      writer.record({{"label", name}, {"nets", {name}}, {"value", value * scale}});
      name_map = false;
    } else if (name_map && directive.starts_with('*') && directive.size() > 1
               && directive.find_first_not_of("0123456789", 1) == std::string::npos) {
      std::string name;
      std::getline(fields >> std::ws, name);
      const auto last = name.find_last_not_of(" \t\r");
      if (last == std::string::npos)
        throw std::runtime_error("Empty SPEF net name");
      name.resize(last + 1);
      if (!names.emplace(directive, name).second)
        throw std::runtime_error("Duplicate SPEF name index");
    } else if (directive.starts_with('*')) {
      name_map = false;
    }
  }
  if (input.bad())
    throw std::runtime_error("Cannot read RCX SPEF: " + source.string());
  writer.finish();
}
}  // namespace ecc::geometry
