// ***************************************************************************************
// Copyright (c) 2023-2025 Peng Cheng Laboratory
// Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences
// Copyright (c) 2023-2025 Beijing Institute of Open Source Chip
//
// iEDA is licensed under Mulan PSL v2.
// You can use this software according to the terms and conditions of the Mulan PSL v2.
// You may obtain a copy of the Mulan PSL v2 at:
// http://license.coscl.org.cn/MulanPSL2
//
// THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
// EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
// MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
//
// See the Mulan PSL v2 for more details.
// ***************************************************************************************
#include <cassert>
#include <cmath>

#include "VcdParser.hh"

int main()
{
  vcd::VcdReader vcd_reader;
  assert(vcd_reader.read(VCD_TEST_FILE_PATH));

  std::map<std::string, vcd::VcdSignalActivity>& signal_activity_map = vcd_reader.get_signal_activity_map();
  assert(signal_activity_map.count("top/a") == 1);
  assert(signal_activity_map.count("top/alias_a") == 1);
  assert(signal_activity_map.count("top/u0/bus[1]") == 1);
  assert(signal_activity_map.count("top/u0/bus[0]") == 1);
  assert(signal_activity_map.count("top/u0/hexbus[3]") == 1);

  assert(std::fabs(signal_activity_map["top/a"].get_transition_density() - 2.0 / 30.0) < 1E-9);
  assert(std::fabs(signal_activity_map["top/a"].get_static_probability() - 1.0 / 3.0) < 1E-9);
  assert(std::fabs(signal_activity_map["top/alias_a"].get_transition_density() - 2.0 / 30.0) < 1E-9);
  assert(std::fabs(signal_activity_map["top/alias_a"].get_static_probability() - 1.0 / 3.0) < 1E-9);
  assert(std::fabs(signal_activity_map["top/u0/bus[1]"].get_transition_density() - 1.5 / 30.0) < 1E-9);
  assert(std::fabs(signal_activity_map["top/u0/bus[0]"].get_static_probability() - 2.0 / 3.0) < 1E-9);
  assert(std::fabs(signal_activity_map["top/u0/hexbus[3]"].get_transition_density() - 2.0 / 30.0) < 1E-9);
  assert(std::fabs(signal_activity_map["top/u0/hexbus[3]"].get_static_probability() - 2.0 / 3.0) < 1E-9);
  return 0;
}
