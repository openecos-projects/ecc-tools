// SPDX-License-Identifier: MulanPSL-2.0
#pragma once

#include <algorithm>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "TransType.hpp"

namespace ipw {
enum class ElectricalRule
{
  kTransition,
  kCapacitance
};
enum class LimitObject
{
  kDesign,
  kPort,
  kPin,
  kLibPin,
  kClock
};

// Non-clock objects apply to both path kinds and both edges. Clock limits
// are expanded to individual path/edge selectors when they are installed.
struct ElectricalLimit
{
  ElectricalRule rule;
  LimitObject object;
  std::string name;
  bool clock_path = false;
  TransType edge = TransType::kNone;
  double value = 0.;
  bool force = false;

  bool sameSelector(const ElectricalLimit& other) const
  {
    return rule == other.rule && object == other.object && name == other.name && clock_path == other.clock_path
           && edge == other.edge;
  }
};

class ElectricalLimits
{
 public:
  void set(ElectricalLimit limit)
  {
    auto found = std::find_if(_limits.begin(), _limits.end(), [&](const auto& old) { return old.sameSelector(limit); });
    if (found == _limits.end())
      _limits.push_back(std::move(limit));
    else
      *found = std::move(limit);
  }
  const std::vector<ElectricalLimit>& entries() const { return _limits; }
  void removeClock(const std::string& name)
  {
    std::erase_if(_limits, [&](const auto& limit) {
      return limit.object == LimitObject::kClock && limit.name == name;
    });
  }
  std::optional<double> forcedLimit(ElectricalRule rule, std::string_view pin, bool port) const
  {
    for (const auto& constraint : _limits)
      if (constraint.rule == rule && constraint.force && constraint.name == pin
          && constraint.object == (port ? LimitObject::kPort : LimitObject::kPin))
        return constraint.value;
    return std::nullopt;
  }
  std::optional<double> limit(
      ElectricalRule rule,
      std::string_view pin,
      bool port,
      std::string_view lib_pin,
      std::string_view clock,
      bool clock_path,
      TransType edge
  ) const
  {
    if (auto forced = forcedLimit(rule, pin, port))
      return forced;
    std::optional<double> result;
    for (const auto& constraint : _limits) {
      if (constraint.rule != rule)
        continue;
      bool matches = false;
      switch (constraint.object) {
        case LimitObject::kDesign:
          matches = true;
          break;
        case LimitObject::kPort:
          matches = port && constraint.name == pin;
          break;
        case LimitObject::kPin:
          matches = !port && constraint.name == pin;
          break;
        case LimitObject::kLibPin:
          matches = constraint.name == lib_pin;
          break;
        case LimitObject::kClock:
          matches = constraint.name == clock && constraint.clock_path == clock_path && constraint.edge == edge;
          break;
      }
      if (matches && (!result || constraint.value < *result))
        result = constraint.value;
    }
    return result;
  }

 private:
  std::vector<ElectricalLimit> _limits;
};
}  // namespace ipw
