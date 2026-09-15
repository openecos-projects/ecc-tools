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

TclSetCaseAnalysis::TclSetCaseAnalysis(const char* cmd_name, ClientData client_data) : SdcTclCmd(cmd_name, client_data)
{
  addOption(new ecc::TclStringOption("value", 1));
  addOption(new ecc::TclStringListOption("objects", 1));
}

unsigned TclSetCaseAnalysis::exec()
{
  auto& data_manager = DataManager::getInst();

  ecc::TclOption* value_option = getOptionOrArg("value");
  ecc::TclOption* object_option = getOptionOrArg("objects");
  if (!value_option->is_set_val() || !object_option->is_set_val()) {
    setTclError("set_case_analysis requires a value and an object collection");
    return 0;
  }

  const std::string value = value_option->getStringVal();
  if (value != "0" && value != "1") {
    setTclError("set_case_analysis value must be 0 or 1");
    return 0;
  }

  const bool case_value = value == "1";
  auto& case_analysis_map = data_manager.getDatabase().get_timing_constraint().get_case_analysis_map();
  for (const std::string& pin_name : resolveObjectList(data_manager.getDatabase(), object_option->getStringList())) {
    case_analysis_map[pin_name] = case_value;
  }
  return 1;
}

}  // namespace ista::sdc
