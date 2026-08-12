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
#include "TimingPath.hpp"

namespace ista {

class TimingPathEnd
{
 public:
  TimingPathEnd() = default;
  ~TimingPathEnd() = default;
  // getter
  std::string& get_end_point() { return _end_point; }
  std::vector<TimingPath>& get_timing_path_list() { return _timing_path_list; }
  // setter
  void set_end_point(const std::string& end_point) { _end_point = end_point; }
  void set_timing_path_list(const std::vector<TimingPath>& timing_path_list) { _timing_path_list = timing_path_list; }
  // function

 private:
  std::string _end_point;
  std::vector<TimingPath> _timing_path_list;
};

}  // namespace ista
