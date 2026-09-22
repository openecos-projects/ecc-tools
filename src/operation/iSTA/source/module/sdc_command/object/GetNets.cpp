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

TclGetNets::TclGetNets(const char* cmd_name, ClientData client_data) : SdcTclCmd(cmd_name, client_data)
{
  addOption(new ecc::TclStringOption("nets", 1));
  addObjectQueryOptions(*this, true, true, true);
  addOption(new ecc::TclSwitchOption("-top_net_of_hierarchical_group"));
  addOption(new ecc::TclSwitchOption("-segments"));
  addOption(new ecc::TclStringOption("-boundary_type", 0));
}

unsigned TclGetNets::exec()
{
  ecc::TclOption* objects = getOptionOrArg("nets");
  const ObjectQueryOptions options = getObjectQueryOptions(*this);
  if (const auto error = getObjectQueryError(options, objects->is_set_val())) return setTclError("get_nets " + *error), 0;
  for (const char* option : {"-top_net_of_hierarchical_group", "-segments", "-boundary_type"}) {
    if (getOptionOrArg(option)->is_set_val()) {
      return setTclError(std::string("get_nets ") + option + " requires hierarchical net-segment support"), 0;
    }
  }
  const std::string patterns = objects->is_set_val() ? objects->getStringVal() : "*";
  std::vector<std::string> result = findObjects(STADM.getDatabase(), parseObjectPatterns(patterns, options.regexp), QueryObjectType::kNet, options);
  if (result.empty() && !getOptionOrArg("-quiet")->is_set_val()) {
    warn("no nets matched: " + patterns);
  }
  setResult(std::move(result));
  return 1;
}

}  // namespace ista::sdc
