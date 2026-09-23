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
#include "PWHeader.hpp"
#include "object/ObjectQuery.hpp"
#include "clock/ClockCommands.hpp"

namespace ipw::sdc {

namespace {

std::string deriveClockName(const std::vector<std::string>& source_list)
{
  if (source_list.size() != 1) {
    return {};
  }
  return source_list.front();
}

}  // namespace

void removeClockSourcesFromOtherClocks(std::map<std::string, TimingClock>& clock_map, const std::string& clock_name,
                                       const std::vector<std::string>& source_list)
{
  if (source_list.empty()) {
    return;
  }
  for (auto& [other_name, other_clock] : clock_map) {
    if (other_name == clock_name) {
      continue;
    }
    std::vector<std::string>& other_sources = other_clock.get_source_list();
    other_sources.erase(std::remove_if(other_sources.begin(), other_sources.end(),
                                       [&](const std::string& source) {
                                         return std::find(source_list.begin(), source_list.end(), source) != source_list.end();
                                       }),
                        other_sources.end());
  }
}

TclCreateClock::TclCreateClock(const char* cmd_name, ClientData client_data) : SdcTclCmd(cmd_name, client_data)
{
  addOption(new ecc::TclStringOption("-name", 0));
  addOption(new ecc::TclStringOption("-comment", 0));
  addOption(new ecc::TclDoubleOption("-period", 0));
  addOption(new ecc::TclDoubleListOption("-waveform", 0));
  addOption(new ecc::TclSwitchOption("-add"));
  addOption(new ecc::TclStringListOption("objects", 1));
}

unsigned TclCreateClock::exec()
{
  ecc::TclOption* name_option = getOptionOrArg("-name");
  ecc::TclOption* comment_option = getOptionOrArg("-comment");
  ecc::TclOption* period_option = getOptionOrArg("-period");
  ecc::TclOption* waveform_option = getOptionOrArg("-waveform");
  ecc::TclOption* object_option = getOptionOrArg("objects");
  if (!period_option->is_set_val()) {
    setTclError("create_clock requires -period");
    return 0;
  }

  const double period = period_option->getDoubleVal();
  if (!std::isfinite(period) || period <= 0.0) {
    setTclError("create_clock -period must be a positive finite value");
    return 0;
  }

  std::vector<double> waveform{0.0, period / 2.0};
  if (waveform_option->is_set_val()) {
    waveform = waveform_option->getDoubleList();
    if (waveform.empty() || waveform.size() % 2 != 0) {
      setTclError("create_clock -waveform edge list must contain an even number of values");
      return 0;
    }
    double previous_edge = 0.0;
    bool first_edge = true;
    for (double edge : waveform) {
      if (!std::isfinite(edge) || edge < 0.0) {
        setTclError("create_clock -waveform edge values must be finite and non-negative");
        return 0;
      }
      if (!first_edge && edge < previous_edge) {
        setTclError("create_clock -waveform edge values must be non-decreasing");
        return 0;
      }
      if (edge > period * 2.0) {
        setTclError("create_clock -waveform edge values must not exceed two periods");
        return 0;
      }
      previous_edge = edge;
      first_edge = false;
    }
  }
  const double rise_edge = waveform[0];
  const double fall_edge = waveform[1];

  Database& database = PWDM.getDatabase();
  auto& clock_map = database.get_timing_constraint().get_clock_map();
  std::vector<std::string> source_list;
  if (object_option->is_set_val()) {
    try {
      source_list = findClockSources(database, object_option->getStringList());
    } catch (const std::exception& error) {
      setTclError(error.what());
      return 0;
    }
    if (source_list.empty()) {
      setTclError("create_clock source collection resolved to empty");
      return 0;
    }
  }

  const std::string clock_name = name_option->is_set_val() ? std::string(name_option->getStringVal()) : deriveClockName(source_list);
  if (clock_name.empty()) {
    setTclError("create_clock requires -name for virtual clocks or multi-source clocks");
    return 0;
  }
  if (clock_map.contains(clock_name)) {
    PWLOG.warn(Loc::current(), "clock '", clock_name, "' already exists and will be overwritten");
  }

  std::vector<std::string> unique_source_list;
  std::set<std::string> source_set;
  for (const std::string& source_name : source_list) {
    if (source_set.insert(source_name).second) {
      unique_source_list.push_back(source_name);
    }
  }

  if (!getOptionOrArg("-add")->is_set_val()) {
    removeClockSourcesFromOtherClocks(clock_map, clock_name, unique_source_list);
  }
  TimingClock timing_clock;
  timing_clock.set_clock_name(clock_name);
  timing_clock.set_period(period);
  timing_clock.set_rise_edge(rise_edge);
  timing_clock.set_fall_edge(fall_edge);
  timing_clock.set_waveform(waveform);
  if (comment_option->is_set_val()) {
    timing_clock.set_comment(comment_option->getStringVal());
  }
  timing_clock.set_source_list(unique_source_list);
  timing_clock.set_is_propagated(false);
  clock_map[clock_name] = std::move(timing_clock);
  return 1;
}

}  // namespace ipw::sdc
