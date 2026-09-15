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
#include <set>

#include "RTInterface.hpp"
#include "tcl_rt.h"
#include "tcl_util.h"

namespace tcl {

TclRunERT::TclRunERT(const char* cmd_name) : TclCmd(cmd_name)
{
  _config_list.push_back(std::make_pair("-stage", ValueType::kString));
  _config_list.push_back(std::make_pair("-resolve_congestion", ValueType::kString));
  _config_list.push_back(std::make_pair("-enable_antenna_fix", ValueType::kInt));
  _config_list.push_back(std::make_pair("-antenna_max_iter", ValueType::kInt));
  _config_list.push_back(std::make_pair("-antenna_diode_cells", ValueType::kString));
  _config_list.push_back(std::make_pair("-antenna_report_dir", ValueType::kString));
  _config_list.push_back(std::make_pair("-antenna_search_radius", ValueType::kInt));
  _config_list.push_back(std::make_pair("-antenna_max_jog", ValueType::kInt));

  TclUtil::addOption(this, _config_list);
}

unsigned TclRunERT::exec()
{
  if (!check()) {
    return 0;
  }
  std::map<std::string, std::any> config_map = TclUtil::getConfigMap(this, _config_list);
  RTI.runERT(config_map);
  return 1;
}

}  // namespace tcl
