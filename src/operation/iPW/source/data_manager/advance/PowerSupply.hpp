// SPDX-License-Identifier: MulanPSL-2.0
#pragma once

#include <string>

#include "PowerValue.hpp"

namespace ipw {

struct PowerSupply
{
  double voltage = 0.0;
  std::string ground_pin;
};

struct SupplyPower
{
  PowerSupply supply;
  PowerValue power;
};

}  // namespace ipw
