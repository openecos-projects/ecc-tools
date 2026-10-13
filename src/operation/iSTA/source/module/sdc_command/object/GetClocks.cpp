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

TclGetClocks::TclGetClocks(const char* cmd_name, ClientData client_data) : SdcTclCmd(cmd_name, client_data)
{
  addOption(new ecc::TclStringOption("clocks", 1));
  addObjectQueryOptions(*this, false, false, false);
}

unsigned TclGetClocks::exec()
{
  ecc::TclOption* objects = getOptionOrArg("clocks");
  const ObjectQueryOptions options = getObjectQueryOptions(*this);
  if (const auto error = getObjectQueryError(options, false)) return setTclError("get_clocks " + *error), 0;
  const std::string patterns = objects->is_set_val() ? objects->getStringVal() : "*";
  std::vector<std::string> result = findObjects(STADM.getDatabase(), parseObjectPatterns(patterns, options.regexp), QueryObjectType::kClock, options);
  if (result.empty() && !getOptionOrArg("-quiet")->is_set_val()) {
    warn("no clocks matched: " + patterns);
  }
  setResult(std::move(result));
  return 1;
}

}  // namespace ista::sdc
