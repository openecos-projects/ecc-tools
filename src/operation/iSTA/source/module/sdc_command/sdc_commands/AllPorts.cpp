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
#include "SdcCommands.hpp"

namespace ista::sdc {

TclAllInputs::TclAllInputs(const char* cmd_name, ClientData client_data) : SdcTclCmd(cmd_name, client_data)
{
  addOption(new ecc::TclSwitchOption("-no_clocks"));
}

unsigned TclAllInputs::exec()
{
  Database& database = STADM.getDatabase();
  std::set<std::string> sources;
  if (getOptionOrArg("-no_clocks")->is_set_val()) {
    for (auto& [name, clock] : database.get_timing_constraint().get_clock_map()) {
      sources.insert(clock.get_source_list().begin(), clock.get_source_list().end());
    }
  }
  std::vector<std::string> ports;
  for (auto& [name, pin] : database.get_pin_map()) {
    if (pin.get_is_port() && !sources.contains(name) && (pin.get_direction() == PinDirection::kInput || pin.get_direction() == PinDirection::kInout)) {
      ports.push_back(name);
    }
  }
  std::sort(ports.begin(), ports.end());
  setResult(std::move(ports));
  return 1;
}

TclAllOutputs::TclAllOutputs(const char* cmd_name, ClientData client_data) : SdcTclCmd(cmd_name, client_data)
{
}

unsigned TclAllOutputs::exec()
{
  std::vector<std::string> ports;
  for (auto& [name, pin] : STADM.getDatabase().get_pin_map()) {
    if (pin.get_is_port() && (pin.get_direction() == PinDirection::kOutput || pin.get_direction() == PinDirection::kInout)) {
      ports.push_back(name);
    }
  }
  std::sort(ports.begin(), ports.end());
  setResult(std::move(ports));
  return 1;
}

}  // namespace ista::sdc
