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
// See the Mulan PSL v2 for more details.
// ***************************************************************************************
#pragma once

#include "RCXHeader.hpp"

namespace ircx {

class Corner
{
 public:
  Corner() = default;
  ~Corner() = default;
  // getter
  std::string& get_corner_name() { return _corner_name; }
  std::vector<double>& get_tmpr_list() { return _tmpr_list; }
  std::string& get_itf_file_path() { return _itf_file_path; }
  std::string& get_captab_file_path() { return _captab_file_path; }
  // setter
  void set_corner_name(const std::string& corner_name) { _corner_name = corner_name; }
  void set_tmpr_list(const std::vector<double>& tmpr_list) { _tmpr_list = tmpr_list; }
  void set_itf_file_path(const std::string& itf_file_path) { _itf_file_path = itf_file_path; }
  void set_captab_file_path(const std::string& captab_file_path) { _captab_file_path = captab_file_path; }
  // function

 private:
  std::string _corner_name;
  std::vector<double> _tmpr_list;
  std::string _itf_file_path;
  std::string _captab_file_path;
};

}  // namespace ircx
