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
#include "DataManager.hpp"
#include "Logger.hpp"
#include "SdcCommands.hpp"

namespace ista::sdc {

TclGetPorts::TclGetPorts(const char* cmd_name, ClientData client_data) : SdcTclCmd(cmd_name, client_data)
{
  addOption(new ecc::TclStringListOption("ports", 1));
}

unsigned TclGetPorts::exec()
{
  ecc::TclOption* port_option = getOptionOrArg("ports");
  if (!port_option->is_set_val()) {
    setTclError("get_ports requires a port list");
    return 0;
  }
  const std::vector<std::string> port_name_list = port_option->getStringList();
  if (port_name_list.empty()) {
    setTclError("get_ports requires at least one port name");
    return 0;
  }

  Database& database = STADM.getDatabase();
  std::vector<std::string> resolved_ports;
  resolved_ports.reserve(port_name_list.size());
  for (const std::string& port_name : port_name_list) {
    const auto pin_it = database.get_pin_map().find(port_name);
    if (pin_it == database.get_pin_map().end() || !pin_it->second.get_is_port()) {
      STALOG.warn(Loc::current(), "port '", port_name, "' does not exist");
      setTclError("port does not exist");
      return 0;
    }
    resolved_ports.push_back(port_name);
  }
  setResult(std::move(resolved_ports));
  return 1;
}

}  // namespace ista::sdc
