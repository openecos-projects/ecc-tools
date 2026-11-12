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
#include <utility>

#include "DataManager.hpp"
#include "exception/PathExceptionCommand.hpp"

namespace ista::sdc {

namespace {

// A register output is traversed by a path, but is not its launch boundary.
// Keep this validation local to false paths: max/min delay intentionally allow
// internal pins to segment a path, and -through may name register outputs.
bool isRegisterOutput(const Database& database, const ExceptionObject& object)
{
  if (object.type != ExceptionObjectType::kPin)
    return false;
  const auto& pins = database.network().pins();
  auto pin = pins.find(object.name);
  if (pin == pins.end() || pin->second.get_is_port())
    return false;
  const auto& constraints = database.sdc().get_port_constraint_map();
  auto constraint = constraints.find(object.name);
  if (constraint != constraints.end() && !constraint->second.get_input_delay_list().empty())
    return false;
  const auto& instances = database.network().instances();
  auto instance = instances.find(pin->second.get_instance_name());
  if (instance == instances.end() || !instance->second.get_is_sequential())
    return false;
  const auto& cells = database.liberty().get_cell_map();
  auto cell = cells.find(instance->second.get_cell_name());
  if (cell == cells.end())
    return false;
  const auto& ports = cell->second.get_port_map();
  auto port = ports.find(pin->second.get_pin_name());
  return port != ports.end() && port->second.get_is_output() && !port->second.get_is_input();
}

}  // namespace

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
  if (hasEmptySelector())
    return 1;
  auto from = exception.get_from_objects();
  const bool explicit_from = !from.empty();
  for (auto it = from.begin(); it != from.end();) {
    if (isRegisterOutput(database, *it)) {
      STALOG.warn(Loc::current(), "set_false_path ignores non-startpoint register output ", it->name);
      it = from.erase(it);
    } else {
      ++it;
    }
  }
  // An empty stored selector means every startpoint. Never broaden an invalid
  // command into a global exception, including when -reset_path was requested.
  if (explicit_from && from.empty())
    return 1;
  exception.set_from_objects(from);
  exception.set_type(TimingExceptionType::kFalsePath);
  applyAnalysisQualifiers(exception);
  if (getOptionOrArg("-reset_path")->is_set_val()) {
    database.sdc().resetPathExceptions(exception);
  }
  database.sdc().get_path_exception_list().push_back(std::move(exception));
  return 1;
}

}  // namespace ista::sdc
