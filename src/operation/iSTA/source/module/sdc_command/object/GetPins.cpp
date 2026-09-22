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
#include "object/ObjectQuery.hpp"
#include "object/ObjectCommands.hpp"

namespace ista::sdc {

TclGetPins::TclGetPins(const char* cmd_name, ClientData client_data) : SdcTclCmd(cmd_name, client_data)
{
  addOption(new ecc::TclStringOption("pins", 1));
  addObjectQueryOptions(*this, true, true, true);
  addOption(new ecc::TclSwitchOption("-leaf"));
}

unsigned TclGetPins::exec()
{
  ecc::TclOption* objects = getOptionOrArg("pins");
  const ObjectQueryOptions options = getObjectQueryOptions(*this);
  if (const auto error = getObjectQueryError(options, objects->is_set_val())) return setTclError("get_pins " + *error), 0;
  const std::string patterns = objects->is_set_val() ? objects->getStringVal() : "*";
  std::vector<std::string> result = findObjects(STADM.getDatabase(), parseObjectPatterns(patterns, options.regexp), QueryObjectType::kPin, options);
  if (getOptionOrArg("-leaf")->is_set_val()) {
    const auto& pins = STADM.getDatabase().get_pin_map();
    std::erase_if(result, [&](const std::string& name) { return !pins.contains(name) || pins.at(name).get_is_port(); });
  }
  if (result.empty() && !getOptionOrArg("-quiet")->is_set_val()) {
    warn("no pins matched: " + patterns);
  }
  setResult(std::move(result));
  return 1;
}

}  // namespace ista::sdc
