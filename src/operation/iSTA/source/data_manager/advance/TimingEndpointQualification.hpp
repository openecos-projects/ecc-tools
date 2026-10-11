// SPDX-License-Identifier: MulanPSL-2.0
#pragma once

#include <array>
#include <cstdint>

namespace ista {

enum class TimingMaxQualificationReason : int32_t
{
  kValid = 0,
  kNoOutputConstraint = 1,
  kNoCaptureClockState = 2,
  kInactiveCheck = 3,
  kNotMaxCheck = 4,
  kUnconstrainedGraphTerminal = 5,
  kUnsupportedConstraint = 6,
};

// Both arrays use data transitions [rise, fall], never capture-clock edges.
struct TimingMaxQualification
{
  std::array<bool, 2> valid{false, false};
  std::array<TimingMaxQualificationReason, 2> reason{
      TimingMaxQualificationReason::kUnconstrainedGraphTerminal,
      TimingMaxQualificationReason::kUnconstrainedGraphTerminal};
};

}  // namespace ista
