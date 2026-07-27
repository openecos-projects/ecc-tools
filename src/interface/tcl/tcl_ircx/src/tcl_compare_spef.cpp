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
#include "RCXInterface.hpp"
#include "tcl_rcx.h"
#include "tcl_util.h"

namespace tcl {

// public

TclCompareSpef::TclCompareSpef(const char* cmd_name) : TclCmd(cmd_name)
{
  _config_list.push_back(std::make_pair("-test_file", ValueType::kString));
  _config_list.push_back(std::make_pair("-reference_file", ValueType::kString));
  _config_list.push_back(std::make_pair("-ccap_rel", ValueType::kString));
  _config_list.push_back(std::make_pair("-cores", ValueType::kInt));
  _config_list.push_back(std::make_pair("-tcap", ValueType::kDouble));
  _config_list.push_back(std::make_pair("-ccap", ValueType::kString));
  _config_list.push_back(std::make_pair("-res", ValueType::kDouble));
  _config_list.push_back(std::make_pair("-corner", ValueType::kString));
  _config_list.push_back(std::make_pair("-match", ValueType::kString));
  _config_list.push_back(std::make_pair("-net", ValueType::kString));
  _config_list.push_back(std::make_pair("-from_pin", ValueType::kString));
  _config_list.push_back(std::make_pair("-to_pin", ValueType::kString));
  _config_list.push_back(std::make_pair("-net_config", ValueType::kString));
  _config_list.push_back(std::make_pair("-timeout", ValueType::kInt));
  _config_list.push_back(std::make_pair("-delay", ValueType::kDouble));
  _config_list.push_back(std::make_pair("-output_dir", ValueType::kString));
  _config_list.push_back(std::make_pair("-compare_resistance", ValueType::kInt));
  _config_list.push_back(std::make_pair("-compare_capacitance", ValueType::kInt));
  _config_list.push_back(std::make_pair("-compare_delay", ValueType::kInt));
  _config_list.push_back(std::make_pair("-delay_pin_load", ValueType::kInt));

  TclUtil::addOption(this, _config_list);
}

unsigned TclCompareSpef::exec()
{
  if (!check()) {
    return 0;
  }
  std::map<std::string, std::any> config_map = TclUtil::getConfigMap(this, _config_list);
  RCXI.compareSpef(config_map);
  return 1;
}

// private

}  // namespace tcl
