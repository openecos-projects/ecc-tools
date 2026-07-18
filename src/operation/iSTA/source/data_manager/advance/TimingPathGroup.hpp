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
#pragma once

#include "STAHeader.hpp"
#include "TimingPathEnd.hpp"

namespace ista {

class TimingPathGroup
{
 public:
  TimingPathGroup() = default;
  ~TimingPathGroup() = default;
  // getter
  std::string& get_group_name() { return _group_name; }
  std::map<std::string, TimingPathEnd>& get_timing_path_end_map() { return _timing_path_end_map; }
  // setter
  void set_group_name(const std::string& group_name) { _group_name = group_name; }
  void set_timing_path_end_map(const std::map<std::string, TimingPathEnd>& timing_path_end_map) { _timing_path_end_map = timing_path_end_map; }
  // function

 private:
  std::string _group_name;
  std::map<std::string, TimingPathEnd> _timing_path_end_map;
};

}  // namespace ista
