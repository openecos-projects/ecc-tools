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
#include "Logger.hpp"
#include "STAHeader.hpp"
#include "SdcCommands.hpp"

namespace ista::sdc {

TclCreateClock::TclCreateClock(const char* cmd_name, ClientData client_data) : SdcTclCmd(cmd_name, client_data)
{
  addOption(new ecc::TclStringOption("-name", 0));
  addOption(new ecc::TclDoubleOption("-period", 0));
  addOption(new ecc::TclDoubleListOption("-waveform", 0));
  addOption(new ecc::TclStringListOption("objects", 1));
}

unsigned TclCreateClock::exec()
{
  ecc::TclOption* name_option = getOptionOrArg("-name");
  ecc::TclOption* period_option = getOptionOrArg("-period");
  ecc::TclOption* waveform_option = getOptionOrArg("-waveform");
  ecc::TclOption* object_option = getOptionOrArg("objects");
  if (!name_option->is_set_val() || !period_option->is_set_val() || !object_option->is_set_val()) {
    setTclError("create_clock requires -name, -period, and a port collection");
    return 0;
  }
  if (std::string(name_option->getStringVal()).empty()) {
    setTclError("create_clock requires a non-empty -name");
    return 0;
  }

  const double period = period_option->getDoubleVal();
  double rise_edge = 0.0;
  double fall_edge = period / 2.0;
  if (waveform_option->is_set_val()) {
    const std::vector<double> waveform = waveform_option->getDoubleList();
    if (waveform.size() != 2) {
      setTclError("create_clock -waveform must contain exactly two values");
      return 0;
    }
    rise_edge = waveform[0];
    fall_edge = waveform[1];
  }

  if (!std::isfinite(period) || period <= 0.0) {
    setTclError("create_clock -period must be a positive finite value");
    return 0;
  }
  if (!std::isfinite(rise_edge) || !std::isfinite(fall_edge) || rise_edge < 0.0 || fall_edge < 0.0 || rise_edge >= fall_edge || fall_edge >= period) {
    setTclError("create_clock -waveform must satisfy 0 <= rise < fall < period");
    return 0;
  }

  const std::vector<std::string> source_list = object_option->getStringList();
  if (source_list.empty()) {
    setTclError("create_clock requires at least one source port");
    return 0;
  }

  Database& database = STADM.getDatabase();
  auto& clock_map = database.get_timing_constraint().get_clock_map();
  const std::string clock_name = name_option->getStringVal();
  if (clock_map.contains(clock_name)) {
    STALOG.warn(Loc::current(), "clock '", clock_name, "' already exists and will be overwritten");
  }

  std::vector<std::string> unique_source_list;
  std::set<std::string> source_set;
  for (const std::string& source_name : source_list) {
    const auto pin_it = database.get_pin_map().find(source_name);
    if (pin_it == database.get_pin_map().end() || !pin_it->second.get_is_port()) {
      STALOG.warn(Loc::current(), "clock source '", source_name, "' is not a top-level port");
      setTclError("clock source is not a top-level port");
      return 0;
    }
    if (source_set.insert(source_name).second) {
      unique_source_list.push_back(source_name);
    }
  }

  TimingClock timing_clock;
  timing_clock.set_clock_name(clock_name);
  timing_clock.set_period(period);
  timing_clock.set_rise_edge(rise_edge);
  timing_clock.set_fall_edge(fall_edge);
  timing_clock.set_source_list(unique_source_list);
  timing_clock.set_is_propagated(false);
  clock_map[clock_name] = std::move(timing_clock);
  return 1;
}

}  // namespace ista::sdc
