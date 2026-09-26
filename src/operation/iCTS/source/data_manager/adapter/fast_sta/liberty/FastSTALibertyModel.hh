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
 * @file FastSTALibertyModel.hh
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-05-19
 * @brief CTS fast STA Liberty arc and lookup-table data.
 */

#pragma once

#include <array>
#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include "FastSTA.hh"

namespace icts {

namespace fast_sta_dmp {
class DriverRampSolver;
}

enum class FastStaLibertyTableKind
{
  kCellDelay,
  kOutputSlew,
  kInternalPower
};

enum class FastStaLibertyAxisKind
{
  kInputSlew,
  kOutputLoad,
  kUnknown
};

struct FastStaLibertyAxis
{
  FastStaLibertyAxisKind kind = FastStaLibertyAxisKind::kUnknown;
  std::vector<double> values;
};

struct FastStaLibertyTable
{
  FastStaLibertyTableKind kind = FastStaLibertyTableKind::kCellDelay;
  FastStaTransition transition = FastStaTransition::kRise;
  std::vector<FastStaLibertyAxis> axes;
  std::vector<double> values;

  [[nodiscard]] auto empty() const -> bool { return values.empty(); }
  [[nodiscard]] auto valid() const -> bool;
  [[nodiscard]] auto lookup(double input_slew_ns, double output_load_pf) const -> std::optional<double>;

 private:
  friend class fast_sta_dmp::DriverRampSolver;

  // DriverRampSolver validates and borrows const tables for one synchronous solve.
  [[nodiscard]] auto lookupValidated(double input_slew_ns, double output_load_pf) const -> std::optional<double>;
};

struct FastStaLibertyArc
{
  std::string library_name = "";
  std::string from_port = "";
  std::string to_port = "";
  std::size_t variant_index = 0U;
  bool positive_unate = false;
  bool negative_unate = false;
  bool conditional = false;
  std::string when = "";
  bool edge_triggered = false;
  FastStaTransition trigger_transition = FastStaTransition::kRise;
  std::vector<FastStaLibertyTable> delay_tables;
  std::vector<FastStaLibertyTable> slew_tables;
  std::vector<FastStaLibertyTable> internal_power_tables;
};

struct FastStaClockGateModel
{
  std::string clock_port = "";
  std::string output_port = "";
  std::vector<std::string> enable_ports;
  std::vector<std::string> test_ports;
  std::string state = "";
  std::string inverted_state = "";
  std::string data_expression = "";
  std::string latch_enable_expression = "";
  std::string output_expression = "";
  bool latch_based = false;
};

struct FastStaLibertyCell
{
  std::string library_name = "";
  std::string cell_master = "";
  std::string input_port = "";
  std::string output_port = "";
  double input_cap_pf = 0.0;
  std::array<std::array<double, 2U>, 2U> input_cap_pf_by_timing{};
  double output_cap_limit_pf = 0.0;
  double input_slew_limit_ns = 0.0;
  double output_slew_limit_ns = 0.0;
  double input_threshold_rise = 0.5;
  double input_threshold_fall = 0.5;
  double output_threshold_rise = 0.5;
  double output_threshold_fall = 0.5;
  double slew_lower_threshold_rise = 0.3;
  double slew_lower_threshold_fall = 0.3;
  double slew_upper_threshold_rise = 0.7;
  double slew_upper_threshold_fall = 0.7;
  double slew_derate_from_library = 1.0;
  double area_um2 = 0.0;
  double voltage_v = 0.0;
  std::optional<double> leakage_power_w = std::nullopt;
  FastStaLibertyArc timing_arc{};
  std::vector<FastStaLibertyArc> timing_arcs;
  std::optional<FastStaClockGateModel> clock_gate = std::nullopt;
  bool input_cap_profile_available = false;
};

}  // namespace icts
