// SPDX-License-Identifier: MulanPSL-2.0
#include "SdfFormatter.hpp"

#include <cctype>
#include <cmath>
#include <cstddef>
#include <iomanip>
#include <ios>
#include <ostream>
#include <sstream>
#include <string>
#include <vector>

#include "Utility.hpp"
namespace ista {
std::string SdfFormatter::checkName(TimingCheckType timing_check_type)
{
  switch (timing_check_type) {
    case TimingCheckType::kSetup:
      return "SETUP";
    case TimingCheckType::kHold:
      return "HOLD";
    case TimingCheckType::kRecovery:
      return "RECOVERY";
    case TimingCheckType::kRemoval:
      return "REMOVAL";
    default:
      return "";
  }
}

std::string SdfFormatter::pathName(Pin& pin)
{
  if (pin.get_is_port()) {
    return portName(pin);
  }

  Instance& instance = _network.instances()[pin.get_instance_name()];
  return STAUTIL.getString(instanceName(instance), "/", portName(pin));
}

std::string SdfFormatter::portName(Pin& pin)
{
  return escapeName(pin.get_pin_name());
}

std::string SdfFormatter::instanceName(Instance& instance)
{
  return escapeName(instance.get_instance_name());
}

std::string SdfFormatter::escapeName(std::string& name)
{
  std::string sdf_name;
  for (std::size_t index = 0; index < name.size(); index++) {
    char character = name[index];
    if (character == '\\' && index + 1 < name.size()) {
      sdf_name.push_back(character);
      sdf_name.push_back(name[++index]);
      continue;
    }
    bool is_name_character = std::isalnum(static_cast<unsigned char>(character)) || character == '_';
    if (!is_name_character) {
      sdf_name.push_back('\\');
    }
    sdf_name.push_back(character);
  }
  return sdf_name;
}

std::string SdfFormatter::condition(TimingArc& timing_arc)
{
  return timing_arc.get_sdf_cond();
}

std::string SdfFormatter::edgeName(TransType trans_type)
{
  if (trans_type == TransType::kFall) {
    return "negedge";
  }
  return "posedge";
}

void SdfFormatter::delay(std::ostream& sdf_file, SdfDelay& sdf_delay, bool explicit_edges)
{
  bool has_rise_delay = sdf_delay.get_rise_min_delay() || sdf_delay.get_rise_max_delay();
  bool has_fall_delay = sdf_delay.get_fall_min_delay() || sdf_delay.get_fall_max_delay();
  if (has_rise_delay) {
    double min_delay = sdf_delay.get_rise_min_delay() ? *sdf_delay.get_rise_min_delay() : *sdf_delay.get_rise_max_delay();
    double max_delay = sdf_delay.get_rise_max_delay() ? *sdf_delay.get_rise_max_delay() : min_delay;
    triple(sdf_file, min_delay, max_delay);
  } else {
    sdf_file << "()";
  }
  if (has_fall_delay) {
    double min_delay = sdf_delay.get_fall_min_delay() ? *sdf_delay.get_fall_min_delay() : *sdf_delay.get_fall_max_delay();
    double max_delay = sdf_delay.get_fall_max_delay() ? *sdf_delay.get_fall_max_delay() : min_delay;
    sdf_file << " ";
    triple(sdf_file, min_delay, max_delay);
  } else if (explicit_edges) {
    sdf_file << " ()";
  }
}

void SdfFormatter::triple(std::ostream& sdf_file, double min_delay, double max_delay)
{
  sdf_file << "(" << number(min_delay) << "::" << number(max_delay) << ")";
}

std::string SdfFormatter::number(double value)
{
  if (std::abs(value) < 5E-13) {
    value = 0.0;
  }
  std::ostringstream oss;
  oss << std::fixed << std::setprecision(10) << value;
  return oss.str();
}

}  // namespace ista
