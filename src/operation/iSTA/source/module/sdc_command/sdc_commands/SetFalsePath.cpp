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

TclSetFalsePath::TclSetFalsePath(const char* cmd_name, ClientData client_data) : SdcTclCmd(cmd_name, client_data)
{
  addOption(new ecc::TclStringListOption("-from", 0));
  addOption(new ecc::TclStringListOption("-to", 0));
  addOption(new ecc::TclSwitchOption("-setup"));
  addOption(new ecc::TclSwitchOption("-hold"));
  addOption(new ecc::TclStringOption("-comment", 0));
}

unsigned TclSetFalsePath::exec()
{
  Database& database = STADM.getDatabase();
  TimingException exception;
  const bool setup = getOptionOrArg("-setup")->is_set_val();
  const bool hold = getOptionOrArg("-hold")->is_set_val();
  exception.set_setup(setup || !hold);
  exception.set_hold(hold || !setup);
  if (getOptionOrArg("-from")->is_set_val()) {
    exception.set_from_objects(resolveExceptionObjects(database, getOptionOrArg("-from")->getStringList()));
  }
  if (getOptionOrArg("-to")->is_set_val()) {
    exception.set_to_objects(resolveExceptionObjects(database, getOptionOrArg("-to")->getStringList()));
  }
  database.get_timing_constraint().get_false_path_list().push_back(std::move(exception));
  return 1;
}

}  // namespace ista::sdc
