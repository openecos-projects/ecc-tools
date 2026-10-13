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

namespace ista::sdc {

TclSetFalsePath::TclSetFalsePath(const char* cmd_name, ClientData client_data) : TclPathException(cmd_name, client_data, true)
{
  addOption(new ecc::TclSwitchOption("-setup"));
  addOption(new ecc::TclSwitchOption("-hold"));
  addOption(new ecc::TclSwitchOption("-reset_path"));
}

unsigned TclSetFalsePath::exec()
{
  Database& database = STADM.getDatabase();
  TimingException exception = parsePathSelector(database, true);
  exception.set_type(TimingExceptionType::kFalsePath);
  applyAnalysisQualifiers(exception);
  if (getOptionOrArg("-reset_path")->is_set_val()) {
    resetPathExceptions(database, exception);
  }
  database.get_timing_constraint().get_path_exception_list().push_back(std::move(exception));
  return 1;
}

}  // namespace ista::sdc
