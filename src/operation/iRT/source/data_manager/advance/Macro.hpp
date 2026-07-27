// ***************************************************************************************
// Copyright (c) 2023-2025 Peng Cheng Laboratory
// Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences
// Copyright (c) 2023-2025 Beijing Institute of Open Source Chip
//
// iEDA is licensed under Mulan PSL v2.
// You can use this software according to the terms and conditions of Mulan PSL v2.
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

#include "PlanarRect.hpp"
#include "RTHeader.hpp"

namespace irt {

class Macro
{
 public:
  Macro() = default;
  ~Macro() = default;
  // getter
  std::string& get_inst_name() { return _inst_name; }
  PlanarRect& get_body_rect() { return _body_rect; }
  // setter
  void set_inst_name(const std::string& inst_name) { _inst_name = inst_name; }
  void set_body_rect(const PlanarRect& body_rect) { _body_rect = body_rect; }
  // function

 private:
  std::string _inst_name;
  PlanarRect _body_rect;
};

}  // namespace irt
