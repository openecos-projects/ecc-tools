// SPDX-License-Identifier: MulanPSL-2.0
#pragma once
#include <string>

#include "common/TransType.hpp"
#include "liberty/TimingCheckType.hpp"
namespace ista {
struct ClockCheck
{
  std::string pin;
  std::string clock;
  TimingCheckType type = TimingCheckType::kNone;
  TransType edge = TransType::kRise;
  double required = 0.;
  double actual = 0.;  // Edge separation including common-path correction.
  double derate = 1.;
  double reconvergence = 0.;
  double slack() const { return actual - required; }
};
}  // namespace ista
