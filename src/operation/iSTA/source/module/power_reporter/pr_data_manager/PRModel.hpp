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

class PRModel
{
 public:
  PRModel() = default;
  ~PRModel() = default;
  // getter
  std::string& get_power_report_file_path() { return _power_report_file_path; }
  std::string& get_instance_power_file_path() { return _instance_power_file_path; }
  // setter
  void set_power_report_file_path(const std::string& power_report_file_path) { _power_report_file_path = power_report_file_path; }
  void set_instance_power_file_path(const std::string& instance_power_file_path) { _instance_power_file_path = instance_power_file_path; }
  // function

 private:
  std::string _power_report_file_path;
  std::string _instance_power_file_path;
};

}  // namespace ista
