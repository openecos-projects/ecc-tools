// SPDX-License-Identifier: MulanPSL-2.0
#pragma once

#include <array>
#include <cstdint>
#include <string>

namespace ista {

enum class TimingMinQualificationReason : int32_t
{
  kValid = 0,
  kNoOutputConstraint = 1,
  kNoCaptureClockState = 2,
  kInactiveCheck = 3,
  kNotMinCheck = 4,
  kUnconstrainedGraphTerminal = 5,
  kUnsupportedConstraint = 6,
  kNoMinPathState = 7,
  kDisabledTransition = 8,
};

struct TimingMinQualification
{
  std::array<bool, 2> valid{false, false};
  std::array<TimingMinQualificationReason, 2> reason{
      TimingMinQualificationReason::kUnconstrainedGraphTerminal,
      TimingMinQualificationReason::kUnconstrainedGraphTerminal,
  };
};

struct TimingMinLaunchData
{
  std::string start_point;
  std::string launch_pin;
  std::string launch_clock;
  std::string path_tag;
  int32_t source_type = 0;
  int32_t data_transition = 0;
  int32_t launch_transition = 0;
  double arrival_ps = 0.0;
  double slew_ps = 0.0;
  double launch_time_ps = 0.0;
  double launch_slew_ps = 0.0;
};

struct TimingMinContextData
{
  std::string start_point;
  std::string end_point;
  std::string launch_clock;
  std::string launch_pin;
  std::string capture_clock;
  std::string capture_pin;
  std::string path_tag;
  int32_t source_type = 0;
  int32_t check_type = 0;
  int32_t data_transition = 0;
  int32_t launch_transition = 0;
  int32_t capture_transition = 0;
  int32_t capture_pin_transition = 0;
  int32_t capture_edge_index = -1;
  int32_t selected_check_arc_idx = -1;
  double arrival_ps = 0.0;
  double required_ps = 0.0;
  double normal_required_ps = 0.0;
  double data_slew_ps = 0.0;
  double launch_time_ps = 0.0;
  double launch_clock_arrival_ps = 0.0;
  double launch_clock_slew_ps = 0.0;
  double capture_edge_ps = 0.0;
  double capture_arrival_ps = 0.0;
  double capture_slew_ps = 0.0;
  double check_time_ps = 0.0;
  double uncertainty_ps = 0.0;
  double cppr_ps = 0.0;
};

}  // namespace ista
