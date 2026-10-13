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
// MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE.
//
// See the Mulan PSL v2 for more details.
// ***************************************************************************************
#include "DataManager.hpp"
#include "SdcCommands.hpp"

namespace ista::sdc {

TclCurrentDesign::TclCurrentDesign(const char* cmd_name, ClientData client_data) : SdcTclCmd(cmd_name, client_data)
{
  addOption(new ecc::TclStringOption("design", 1));
}

unsigned TclCurrentDesign::exec()
{
  const std::string& linked_design = DataManager::getInst().getDatabase().get_design_name();
  ecc::TclOption* design_option = getOptionOrArg("design");
  if (!design_option->is_set_val()) {
    setResult(linked_design);
    return 1;
  }

  const std::string requested_design = design_option->getStringVal();
  if (requested_design.empty()) {
    setTclError("current_design requires a non-empty design name");
    return 0;
  }
  if (linked_design.empty()) {
    setTclError("current_design cannot validate a design before a top design is linked");
    return 0;
  }
  if (requested_design != linked_design) {
    setTclError("current_design does not match the linked top design");
    return 0;
  }
  return 1;
}

}  // namespace ista::sdc
