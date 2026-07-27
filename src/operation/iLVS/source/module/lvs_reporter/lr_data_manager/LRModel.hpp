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
// MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE.
//
// See the Mulan PSL v2 for more details.
// ***************************************************************************************
#pragma once

#include "LVSHeader.hpp"

namespace ilvs {

class LRModel
{
 public:
  LRModel() = default;
  ~LRModel() = default;
  // getter
  std::string& get_rpt_file_path() { return _rpt_file_path; }
  std::string& get_json_file_path() { return _json_file_path; }
  // const getter
  const std::string& get_rpt_file_path() const { return _rpt_file_path; }
  const std::string& get_json_file_path() const { return _json_file_path; }
  // setter
  void set_rpt_file_path(const std::string& rpt_file_path) { _rpt_file_path = rpt_file_path; }
  void set_json_file_path(const std::string& json_file_path) { _json_file_path = json_file_path; }

 private:
  std::string _rpt_file_path;
  std::string _json_file_path;
};

}  // namespace ilvs
