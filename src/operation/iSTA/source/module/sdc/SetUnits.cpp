// SPDX-License-Identifier: MulanPSL-2.0
#include "SetUnits.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <map>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <string>

#include "DataManager.hpp"

namespace ista::sdc {

TclSetUnits::TclSetUnits(const char* cmd_name, ClientData client_data) : SdcTclCmd(cmd_name, client_data)
{
  for (const char* option : {"-time", "-capacitance", "-resistance", "-voltage", "-current", "-power", "-distance"}) {
    addOption(new ecc::TclStringOption(option, 0));
  }
}

unsigned TclSetUnits::exec()
{
  std::map<std::string, std::string> suffix_map
      = {{"-time", "s"}, {"-capacitance", "f"}, {"-resistance", "ohm"}, {"-voltage", "v"}, {"-current", "a"}, {"-power", "w"}, {"-distance", "m"}};
  bool matches = true;
  for (const auto& [option, value] : getOptionValueList()) {
    double declared_scale = parseUnitScale(value, suffix_map[option]);
    double active_scale = getUnitScale(option);
    // Check the reference library unit; do not rescale existing constraints.
    if (std::abs(declared_scale - active_scale) > 1e-6 * std::max(declared_scale, active_scale)) {
      std::ostringstream message;
      message << "set_units " << option << " " << value << " differs from the active iSTA unit scale " << active_scale << " (SI); units are unchanged";
      warn(message.str());
      matches = false;
    }
  }
  setResult(matches ? "1" : "0");
  return 1;
}

double TclSetUnits::parseUnitScale(const std::string& value, const std::string& suffix)
{
  if (value.size() < suffix.size()) {
    throw std::invalid_argument("invalid unit: " + value);
  }
  std::string actual_suffix = value.substr(value.size() - suffix.size());
  for (char& character : actual_suffix) {
    character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
  }
  std::string prefix = value.substr(0, value.size() - suffix.size());
  std::smatch match;
  if (actual_suffix != suffix || !std::regex_match(prefix, match, std::regex(R"(^([0-9]+(?:\.[0-9]*)?|\.[0-9]+)?([Mkmunpf])?$)"))) {
    throw std::invalid_argument("invalid unit: " + value);
  }
  std::map<std::string, double> prefix_scale_map = {{"", 1.0}, {"M", 1e6}, {"k", 1e3}, {"m", 1e-3}, {"u", 1e-6}, {"n", 1e-9}, {"p", 1e-12}, {"f", 1e-15}};
  double multiplier = match[1].matched ? std::stod(match[1].str()) : 1.0;
  double scale = multiplier * prefix_scale_map[match[2].str()];
  if (!std::isfinite(scale) || scale <= 0.0) {
    throw std::invalid_argument("invalid unit scale: " + value);
  }
  return scale;
}

double TclSetUnits::getUnitScale(const std::string& option)
{
  LibertyLibrary& library = STADM.getDatabase().liberty();
  if (option == "-time") {
    switch (library.get_time_unit()) {
      case TimingTimeUnit::kPS:
        return 1e-12;
      case TimingTimeUnit::kFS:
        return 1e-15;
      default:
        return 1e-9;
    }
  }
  if (option == "-capacitance") {
    switch (library.get_cap_unit()) {
      case TimingCapacitiveUnit::kFF:
        return 1e-15;
      case TimingCapacitiveUnit::kF:
        return 1.0;
      default:
        return 1e-12;
    }
  }
  if (option == "-resistance") {
    return library.get_resistance_unit() == TimingResistanceUnit::kkOHM ? 1e3 : 1.0;
  }
  if (option == "-voltage") {
    return parseUnitScale(library.get_voltage_unit_name().value_or("1V"), "v");
  }
  if (option == "-current") {
    return parseUnitScale(library.get_current_unit_name().value_or("1A"), "a");
  }
  if (option == "-power") {
    return 1.0;
  }
  return 1e-6;
}

}  // namespace ista::sdc
