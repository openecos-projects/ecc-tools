// SPDX-License-Identifier: MulanPSL-2.0
#pragma once

#include <string>

#include "TimingCell.hpp"

namespace ipw {

std::string resolvePowerSupply(TimingCell& cell, const std::string& pg_pin, const std::string& signal_pin = "");
double supplyVoltage(TimingCell& cell, const std::string& pg_pin);

}  // namespace ipw
