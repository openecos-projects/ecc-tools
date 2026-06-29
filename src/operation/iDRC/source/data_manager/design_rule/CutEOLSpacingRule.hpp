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

#include "DRCHeader.hpp"

#include <string>
#include <vector>

namespace idrc {

class CutEOLSpacingRule
{
 public:
  struct CutClass
  {
    std::string name;
    int32_t width = -1;
    int32_t length = -1;
    int32_t cuts = -1;
    std::string orient;
  };

  struct EOLToClass
  {
    std::string class_name;
    int32_t cut_spacing1 = -1;
    int32_t cut_spacing2 = -1;
  };

  CutEOLSpacingRule() = default;
  ~CutEOLSpacingRule() = default;
  int32_t eol_spacing = -1;
  int32_t eol_prl = -1;
  int32_t eol_prl_spacing = -1;
  int32_t eol_width = -1;
  int32_t smaller_overhang = -1;
  int32_t equal_overhang = -1;
  int32_t side_ext = -1;
  int32_t backward_ext = -1;
  int32_t span_length = -1;
  std::string cutclass_name1;
  std::vector<EOLToClass> to_class_list;
  std::vector<CutClass> cutclass_list;
};

}  // namespace idrc
