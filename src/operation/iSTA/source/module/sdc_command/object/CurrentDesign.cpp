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

TclCurrentDesign::TclCurrentDesign(const char* cmd_name, ClientData client_data) : SdcTclCmd(cmd_name, client_data)
{
  addOption(new ecc::TclStringOption("design", 1));
}

unsigned TclCurrentDesign::exec()
{
  const std::string& design = STADM.getDatabase().get_design_name();
  ecc::TclOption* option = getOptionOrArg("design");
  if (option->is_set_val() && (design.empty() || design != option->getStringVal())) {
    setTclError("current_design can only select the loaded design '" + design + "'");
    return 0;
  }
  setResult(design.empty() ? std::vector<std::string>{} : std::vector<std::string>{design});
  return 1;
}

}  // namespace ista::sdc
