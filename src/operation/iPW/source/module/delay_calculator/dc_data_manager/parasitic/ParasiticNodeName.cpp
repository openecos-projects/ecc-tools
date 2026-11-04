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
// WHETHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
// MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
// See the Mulan PSL v2 for more details.
// ***************************************************************************************
#include "ParasiticNodeName.hpp"

#include "DataManager.hpp"
#include "Database.hpp"
#include "ParasiticNet.hpp"
#include "PWHeader.hpp"

namespace ipw {

std::string getParasiticNodeName(ParasiticNet& parasitic_net, const std::string& pin_name)
{
  if (parasitic_net.get_node_capacitance_map().find(pin_name) != parasitic_net.get_node_capacitance_map().end()) {
    return pin_name;
  }

  std::string spef_pin_name = pin_name;
  std::replace(spef_pin_name.begin(), spef_pin_name.end(), ':', '/');
  if (parasitic_net.get_node_capacitance_map().find(spef_pin_name)
      != parasitic_net.get_node_capacitance_map().end()) {
    return spef_pin_name;
  }
  return "";
}

std::string getPinNameByParasiticNodeName(const std::string& node_name)
{
  Database& database = PWDM.getDatabase();
  if (database.get_pin_map().find(node_name) != database.get_pin_map().end()) {
    return node_name;
  }

  std::string pin_name = node_name;
  std::replace(pin_name.begin(), pin_name.end(), '/', ':');
  if (database.get_pin_map().find(pin_name) != database.get_pin_map().end()) {
    return pin_name;
  }
  return node_name;
}

}  // namespace ipw
