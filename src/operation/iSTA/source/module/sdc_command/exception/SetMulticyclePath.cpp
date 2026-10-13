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

TclSetMulticyclePath::TclSetMulticyclePath(const char* cmd_name, ClientData client_data) : TclPathException(cmd_name, client_data, true)
{
  addOption(new ecc::TclSwitchOption("-setup"));
  addOption(new ecc::TclSwitchOption("-hold"));
  addOption(new ecc::TclSwitchOption("-start"));
  addOption(new ecc::TclSwitchOption("-end"));
  addOption(new ecc::TclSwitchOption("-reset_path"));
  addOption(new ecc::TclIntOption("path_multiplier", 1));
}

unsigned TclSetMulticyclePath::exec()
{
  if (!getOptionOrArg("path_multiplier")->is_set_val()) {
    setTclError("set_multicycle_path requires a path multiplier");
    return 0;
  }
  const bool start = getOptionOrArg("-start")->is_set_val();
  const bool end = getOptionOrArg("-end")->is_set_val();
  if (start && end) {
    setTclError("set_multicycle_path cannot use -start with -end");
    return 0;
  }

  Database& database = STADM.getDatabase();
  TimingException exception = parsePathSelector(database, false);
  exception.set_type(TimingExceptionType::kMulticycle);
  const bool setup = getOptionOrArg("-setup")->is_set_val();
  const bool hold = getOptionOrArg("-hold")->is_set_val();
  const int32_t multiplier = getOptionOrArg("path_multiplier")->getIntVal();
  exception.set_setup_multiplier(setup || !hold ? std::optional<int32_t>(multiplier) : std::nullopt);
  exception.set_hold_multiplier(hold ? std::optional<int32_t>(multiplier) : (!setup ? std::optional<int32_t>(0) : std::nullopt));
  exception.set_setup(exception.get_setup_multiplier().has_value());
  exception.set_hold(exception.get_hold_multiplier().has_value());
  exception.set_setup_use_end_clock(!start);
  exception.set_hold_use_end_clock(end);

  if (getOptionOrArg("-reset_path")->is_set_val()) {
    TimingException reset_filter = exception;
    applyAnalysisQualifiers(reset_filter);
    resetPathExceptions(database, reset_filter);
  }
  database.get_timing_constraint().get_path_exception_list().push_back(std::move(exception));
  return 1;
}

}  // namespace ista::sdc
