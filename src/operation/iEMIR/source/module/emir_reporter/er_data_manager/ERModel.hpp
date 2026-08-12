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
#pragma once

#include "EMIRHeader.hpp"

namespace iemir {

class ERModel
{
 public:
  ERModel() = default;
  ~ERModel() = default;
  // getter
  std::string& get_ir_report_file_path() { return _ir_report_file_path; }
  std::string& get_em_report_file_path() { return _em_report_file_path; }
  // setter
  void set_ir_report_file_path(const std::string& ir_report_file_path) { _ir_report_file_path = ir_report_file_path; }
  void set_em_report_file_path(const std::string& em_report_file_path) { _em_report_file_path = em_report_file_path; }
  // function

 private:
  std::string _ir_report_file_path;
  std::string _em_report_file_path;
};

}  // namespace iemir
