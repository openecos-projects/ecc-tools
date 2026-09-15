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
#include "SdcCommandUtils.hpp"
#include "SdcCommands.hpp"

namespace ista::sdc {

TclGetPorts::TclGetPorts(const char* cmd_name, ClientData client_data) : SdcTclCmd(cmd_name, client_data)
{
  addOption(new ecc::TclStringOption("ports", 1));
  addOption(new ecc::TclSwitchOption("-quiet"));
  addOption(new ecc::TclSwitchOption("-regexp"));
}

unsigned TclGetPorts::exec()
{
  ecc::TclOption* objects = getOptionOrArg("ports");
  const bool regexp = getOptionOrArg("-regexp")->is_set_val();
  const std::string patterns = objects->is_set_val() ? objects->getStringVal() : "*";
  std::vector<std::string> result = queryObjects(STADM.getDatabase(), queryPatterns(patterns, regexp), QueryObjectType::kPort, regexp);
  if (result.empty() && !getOptionOrArg("-quiet")->is_set_val()) {
    setTclError("no ports matched: " + patterns);
    return 0;
  }
  setResult(std::move(result));
  return 1;
}

}  // namespace ista::sdc
