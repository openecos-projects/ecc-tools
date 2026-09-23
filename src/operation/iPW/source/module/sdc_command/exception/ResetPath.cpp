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
#include "exception/PathExceptionCommand.hpp"

namespace ipw::sdc {

TclResetPath::TclResetPath(const char* cmd_name, ClientData client_data) : TclPathException(cmd_name, client_data, false)
{
  addOption(new ecc::TclSwitchOption("-setup"));
  addOption(new ecc::TclSwitchOption("-hold"));
}

unsigned TclResetPath::exec()
{
  Database& database = PWDM.getDatabase();
  TimingException filter = parsePathSelector(database, true);
  applyAnalysisQualifiers(filter);
  resetPathExceptions(database, filter);
  return 1;
}

}  // namespace ipw::sdc
