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

class Net
{
 public:
  Net() = default;
  ~Net() = default;
  // getter
  std::vector<std::string>& get_terminal_name_list() { return _terminal_name_list; }
  // const getter
  const std::vector<std::string>& get_terminal_name_list() const { return _terminal_name_list; }
  // setter
  void set_terminal_name_list(const std::vector<std::string>& terminal_name_list) { _terminal_name_list = terminal_name_list; }

 private:
  std::vector<std::string> _terminal_name_list;
};

}  // namespace ilvs
