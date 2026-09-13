// ***************************************************************************************
// Copyright (c) 2023-2025 Peng Cheng Laboratory
// Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences
// Copyright (c) 2023-2025 Beijing Institute of Open Source Chip
//
// iEDA is licensed under Mulan PSL v2.
// You can use this software according to the terms and conditions of the Mulan PSL v2.
// You may obtain a copy of Mulan PSL v2 at:
// http://license.coscl.org.cn/MulanPSL2
//
// THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
// EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
// MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
//
// See the Mulan PSL v2 for more details.
// ***************************************************************************************
/**
 * @file FastSTAClockTiming.hh
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-05-19
 * @brief CTS fast STA slew, arrival, skew, and DMP timing records.
 */

#pragma once

#include <cstddef>
#include <limits>
#include <string>
#include <vector>

#include "FastSTA.hh"

namespace icts {

enum class FastStaDmpAlgorithm
{
  kCap,
  kPi,
  kZeroNearCap
};

struct FastStaTimingPoint
{
  double arrival_ns = 0.0;
  double slew_ns = 0.0;
  FastStaNodeId launch_node_id = kInvalidFastStaNodeId;
  FastStaNodeId launch_clock_node_id = kInvalidFastStaNodeId;
  FastStaTransition launch_clock_transition = FastStaTransition::kRise;
  // Physical launch insertion, including a separation-query delta, without event phase.
  double launch_clock_arrival_ns = 0.0;
  std::size_t driver_model_index = std::numeric_limits<std::size_t>::max();
  std::size_t driver_arc_variant_index = std::numeric_limits<std::size_t>::max();
  double driver_input_slew_ns = 0.0;
  std::size_t slew_driver_model_index = std::numeric_limits<std::size_t>::max();
  std::size_t slew_driver_arc_variant_index = std::numeric_limits<std::size_t>::max();
  double slew_driver_input_slew_ns = 0.0;
  FastStaNodeId stage_input_node_id = kInvalidFastStaNodeId;
  FastStaTransition stage_input_transition = FastStaTransition::kRise;
  double stage_input_arrival_ns = 0.0;
  double stage_input_slew_ns = 0.0;
  double stage_delay_ns = 0.0;
  bool driver_is_launch = false;
  bool slew_driver_is_launch = false;
  bool valid = false;
  std::string clock_name = "";
  FastStaTransition launch_data_transition = FastStaTransition::kRise;
  // One ordered-through automaton per exception; max() means its -from did not match.
  std::vector<std::size_t> exception_progress;
};

struct FastStaDmpDriverResult
{
  bool valid = false;
  bool driver_waveform_valid = false;
  FastStaDmpAlgorithm algorithm = FastStaDmpAlgorithm::kCap;
  FastStaTransition transition = FastStaTransition::kRise;
  std::string driver_library_name = "";
  std::string driver_cell_master = "";
  double ceff_pf = 0.0;
  double gate_delay_ns = 0.0;
  double driver_slew_ns = 0.0;
  double driver_waveform_delay_ns = 0.0;
  double ramp_start_ns = 0.0;
  double ramp_duration_ns = 0.0;
  double near_cap_pf = 0.0;
  double far_cap_pf = 0.0;
  double rpi_ns_per_pf = 0.0;
  double rd_ns_per_pf = 0.0;
  double input_threshold = 0.5;
  double output_threshold = 0.5;
  double slew_lower_threshold = 0.3;
  double slew_upper_threshold = 0.7;
  double slew_derate = 1.0;
  double pole1_per_ns = 0.0;
  double pole2_per_ns = 0.0;
  double zero1_per_ns = 0.0;
  double waveform_scale = 0.0;
  double waveform_offset = 0.0;
  double waveform_slope = 0.0;
  double first_pole_weight = 0.0;
  double second_pole_weight = 0.0;
};

struct FastStaDmpLoadResult
{
  bool valid = false;
  double wire_delay_ns = 0.0;
  double load_slew_ns = 0.0;
};

}  // namespace icts
