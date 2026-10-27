// SPDX-License-Identifier: MulanPSL-2.0
#pragma once

#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>

#include "common/TimingCaseValue.hpp"
#include "common/TransType.hpp"

namespace ista {
// Computed logic values. The independent driver role is used only for inout
// pins; its absence means unknown, never a fallback to the receiver value.
class SimValues
{
 public:
  std::optional<TimingCaseValue> value(std::string_view pin, bool driver = false) const
  {
    const auto it = _pins.find(pin);
    if (it == _pins.end())
      return std::nullopt;
    return driver ? it->second.driver : it->second.receiver;
  }
  void setValue(const std::string& pin, TimingCaseValue value, bool driver = false)
  {
    auto& values = _pins[pin];
    (driver ? values.driver : values.receiver) = value;
  }
  std::optional<bool> driverDisabled(std::string_view pin) const
  {
    const auto it = _pins.find(pin);
    return it == _pins.end() ? std::nullopt : it->second.driver_disabled;
  }
  bool isDriverDisabled(std::string_view pin) const { return driverDisabled(pin) == true; }
  void setDriverDisabled(const std::string& pin, std::optional<bool> disabled) { _pins[pin].driver_disabled = disabled; }
  bool allowsTransition(std::string_view pin, TransType transition, bool driver = false) const
  {
    const auto logic = value(pin, driver);
    if (!logic)
      return true;
    switch (*logic) {
      case TimingCaseValue::kNone:
        return true;
      case TimingCaseValue::kRise:
        return transition == TransType::kRise;
      case TimingCaseValue::kFall:
        return transition == TransType::kFall;
      case TimingCaseValue::kZero:
      case TimingCaseValue::kOne:
      case TimingCaseValue::kStatic:
        return false;
    }
    return true;
  }
  void clear() { _pins.clear(); }

 private:
  struct PinValues
  {
    std::optional<TimingCaseValue> receiver;
    std::optional<TimingCaseValue> driver;
    // nullopt preserves an unresolved three-state control, distinct from a
    // definitely enabled output (false) and a high-impedance output (true).
    std::optional<bool> driver_disabled;
  };
  std::map<std::string, PinValues, std::less<>> _pins;
};
}  // namespace ista
