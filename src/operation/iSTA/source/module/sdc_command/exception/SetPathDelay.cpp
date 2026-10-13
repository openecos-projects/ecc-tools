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

namespace {

bool hasInternalProbePoint(Database& database, const TimingException& exception)
{
  const auto is_internal = [&database](const std::string& object) {
    const auto pin = database.get_pin_map().find(object);
    return (pin != database.get_pin_map().end() && !pin->second.get_is_port()) || database.get_instance_map().contains(object)
           || database.get_net_map().contains(object);
  };
  return std::any_of(exception.get_from_objects().begin(), exception.get_from_objects().end(), is_internal)
         || std::any_of(exception.get_to_objects().begin(), exception.get_to_objects().end(), is_internal);
}

}  // namespace

TclSetPathDelay::TclSetPathDelay(const char* cmd_name, ClientData client_data) : TclPathException(cmd_name, client_data, true)
{
  addOption(new ecc::TclSwitchOption("-ignore_clock_latency"));
  addOption(new ecc::TclSwitchOption("-reset_path"));
  addOption(new ecc::TclSwitchOption("-probe"));
  addOption(new ecc::TclDoubleOption("delay", 1));
}

unsigned TclSetPathDelay::executePathDelay(TimingExceptionType type)
{
  if (!getOptionOrArg("delay")->is_set_val()) {
    throw std::invalid_argument(std::string(get_cmd_name()) + " requires a delay value");
  }
  Database& database = STADM.getDatabase();
  TimingException exception = parsePathSelector(database, false);
  exception.set_type(type);
  exception.set_setup(type == TimingExceptionType::kMaxDelay);
  exception.set_hold(type == TimingExceptionType::kMinDelay);
  exception.set_delay(getOptionOrArg("delay")->getDoubleVal());
  exception.set_ignore_clock_latency(getOptionOrArg("-ignore_clock_latency")->is_set_val());
  exception.set_probe(getOptionOrArg("-probe")->is_set_val());
  if (exception.get_probe() && hasInternalProbePoint(database, exception)) {
    throw std::invalid_argument(std::string(get_cmd_name()) + " -probe on internal path points is not supported yet");
  }
  if (getOptionOrArg("-reset_path")->is_set_val()) {
    TimingException reset_filter = exception;
    reset_filter.set_setup(true);
    reset_filter.set_hold(true);
    resetPathExceptions(database, reset_filter);
  }
  database.get_timing_constraint().get_path_exception_list().push_back(std::move(exception));
  return 1;
}

TclSetMaxDelay::TclSetMaxDelay(const char* cmd_name, ClientData client_data) : TclSetPathDelay(cmd_name, client_data) {}

unsigned TclSetMaxDelay::exec()
{
  return executePathDelay(TimingExceptionType::kMaxDelay);
}

TclSetMinDelay::TclSetMinDelay(const char* cmd_name, ClientData client_data) : TclSetPathDelay(cmd_name, client_data) {}

unsigned TclSetMinDelay::exec()
{
  return executePathDelay(TimingExceptionType::kMinDelay);
}

}  // namespace ista::sdc
