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
// WHETHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
// MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
// See the Mulan PSL v2 for more details.
// ***************************************************************************************
#pragma once

#include <string>
#include <unordered_map>
#include <vector>

#include "PowerGroupType.hpp"

namespace ipw {

class TimingCell;

// Per-power-arc quantities that only depend on the netlist, the library and the
// (already propagated) slews: the peak flow used to recompute them for every
// VCD window, so they are prepared once per run instead.
struct PAArcStaticContext
{
  double energy_rise = 0.0;
  double energy_fall = 0.0;
  bool is_active_clock_pin = false;
};

struct PAInstanceStaticContext
{
  PowerGroupType group = PowerGroupType::kNone;
  double voltage = 0.0;
  TimingCell* timing_cell = nullptr;
  std::vector<PAArcStaticContext> arc_context_list;              // aligned with TimingCell::get_power_arc_list()
  std::unordered_map<std::string, double> port_output_load_map;  // per output port
  // 该实例的 port -> pin 索引表（来自 Database::get_instance_port_pin_index_list()，
  // 顺序与 cell port_map 一致）；热路径据此避免拼 "instance:port" 字符串。
  const std::vector<Database::InstancePortPinIndex>* port_pin_index_list = nullptr;
};

}  // namespace ipw
