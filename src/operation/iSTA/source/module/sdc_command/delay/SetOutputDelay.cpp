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
#include "delay/DelayCommands.hpp"

namespace ista::sdc {

TclSetOutputDelay::TclSetOutputDelay(const char* cmd_name, ClientData client_data) : SdcTclCmd(cmd_name, client_data)
{
  addOption(new ecc::TclStringOption("-clock", 0));
  addOption(new ecc::TclStringOption("-reference_pin", 0));
  addOption(new ecc::TclSwitchOption("-rise"));
  addOption(new ecc::TclSwitchOption("-fall"));
  addOption(new ecc::TclSwitchOption("-clock_fall"));
  addOption(new ecc::TclSwitchOption("-min"));
  addOption(new ecc::TclSwitchOption("-max"));
  addOption(new ecc::TclSwitchOption("-add_delay"));
  addOption(new ecc::TclSwitchOption("-level_sensitive"));
  addOption(new ecc::TclSwitchOption("-network_latency_included"));
  addOption(new ecc::TclSwitchOption("-source_latency_included"));
  addOption(new ecc::TclDoubleOption("delay", 1));
  addOption(new ecc::TclStringListOption("objects", 1));
}

namespace {

std::vector<AnalysisType> getAnalysisTypes(bool set_min, bool set_max)
{
  if (set_min && !set_max) {
    return {AnalysisType::kMin};
  }
  if (set_max && !set_min) {
    return {AnalysisType::kMax};
  }
  return {AnalysisType::kMin, AnalysisType::kMax};
}

std::vector<TransType> getTransitionTypes(bool rise, bool fall)
{
  if (rise && !fall) {
    return {TransType::kRise};
  }
  if (fall && !rise) {
    return {TransType::kFall};
  }
  return {TransType::kRise, TransType::kFall};
}

std::vector<std::string> findClockNames(Database& database, ecc::TclOption* clock_option)
{
  if (clock_option == nullptr || !clock_option->is_set_val()) {
    return {""};
  }
  const std::set<std::string> clocks = findClocks(database, parseObjectPatterns(clock_option->getStringVal(), false));
  return {clocks.begin(), clocks.end()};
}

}  // namespace

unsigned TclSetOutputDelay::exec()
{
  auto& data_manager = DataManager::getInst();

  ecc::TclOption* delay_option = getOptionOrArg("delay");
  ecc::TclOption* object_option = getOptionOrArg("objects");
  if (!delay_option->is_set_val() || !object_option->is_set_val()) {
    setTclError("set_output_delay requires a delay and an object collection");
    return 0;
  }

  ecc::TclOption* clock_option = getOptionOrArg("-clock");
  const double delay_value = delay_option->getDoubleVal();
  const bool set_min = getOptionOrArg("-min")->is_set_val();
  const bool set_max = getOptionOrArg("-max")->is_set_val();
  const bool rise = getOptionOrArg("-rise")->is_set_val();
  const bool fall = getOptionOrArg("-fall")->is_set_val();
  Database& database = data_manager.getDatabase();
  const std::vector<std::string> clocks = findClockNames(database, clock_option);
  const std::vector<std::string> ports = getPortPinNames(database, object_option->getStringList());
  if (ports.empty()) {
    setTclError("set_output_delay requires at least one output port");
    return 0;
  }
  for (const std::string& port_name : ports) {
    Pin& pin = database.get_pin_map().at(port_name);
    if (!pin.get_is_port() || (pin.get_direction() != PinDirection::kOutput && pin.get_direction() != PinDirection::kInout)) {
      setTclError("set_output_delay requires output ports; '" + port_name + "' is not an output port");
      return 0;
    }
  }
  std::vector<TimingIoDelay> delays;
  for (const std::string& clock_name : clocks) {
    for (AnalysisType analysis_type : getAnalysisTypes(set_min, set_max)) {
      for (TransType trans_type : getTransitionTypes(rise, fall)) {
        TimingIoDelay delay;
        delay.set_clock_name(clock_name);
        delay.set_delay(delay_value);
        delay.set_analysis_type(analysis_type);
        delay.set_trans_type(trans_type);
        delay.set_clock_trans_type(getOptionOrArg("-clock_fall")->is_set_val() ? TransType::kFall : TransType::kRise);
        delay.set_level_sensitive(getOptionOrArg("-level_sensitive")->is_set_val());
        delay.set_network_latency_included(getOptionOrArg("-network_latency_included")->is_set_val());
        delay.set_source_latency_included(getOptionOrArg("-source_latency_included")->is_set_val());
        if (getOptionOrArg("-reference_pin")->is_set_val()) {
          delay.set_reference_pin(getOptionOrArg("-reference_pin")->getStringVal());
        }
        delays.push_back(std::move(delay));
      }
    }
  }
  for (const std::string& port_name : ports) {
    getOrCreatePortConstraint(database, port_name).set_output_delays(delays, getOptionOrArg("-add_delay")->is_set_val());
  }
  return 1;
}

}  // namespace ista::sdc
