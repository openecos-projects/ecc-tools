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
// WHETHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
// MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
// See the Mulan PSL v2 for more details.
// ***************************************************************************************
#pragma once

#include "STAHeader.hpp"

namespace ista {

class PPModel
{
 public:
  PPModel() = default;
  ~PPModel() = default;
  // getter
  double get_minimum_clock_period() const { return _minimum_clock_period; }
  int32_t get_max_activity_pass_num() const { return _max_activity_pass_num; }
  std::vector<std::string>& get_seed_pin_list() { return _seed_pin_list; }
  std::vector<std::string>& get_sequential_instance_name_list() { return _sequential_instance_name_list; }
  // setter
  void set_minimum_clock_period(const double minimum_clock_period) { _minimum_clock_period = minimum_clock_period; }
  void set_max_activity_pass_num(const int32_t max_activity_pass_num) { _max_activity_pass_num = max_activity_pass_num; }
  void set_seed_pin_list(const std::vector<std::string>& seed_pin_list) { _seed_pin_list = seed_pin_list; }
  void set_sequential_instance_name_list(const std::vector<std::string>& sequential_instance_name_list)
  {
    _sequential_instance_name_list = sequential_instance_name_list;
  }
  // function

 private:
  double _minimum_clock_period = 1.0;
  int32_t _max_activity_pass_num = 50;
  std::vector<std::string> _seed_pin_list;
  std::vector<std::string> _sequential_instance_name_list;
};

}  // namespace ista
