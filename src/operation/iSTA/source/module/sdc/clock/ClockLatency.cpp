// ***************************************************************************************
// Copyright (c) 2023-2025 Peng Cheng Laboratory
// Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences
// Copyright (c) 2023-2025 Beijing Institute of Open Source Chip
//
// iEDA is licensed under Mulan PSL v2.
// ***************************************************************************************
#include <algorithm>
#include <cmath>
#include <exception>
#include <set>
#include <stdexcept>
#include <string>

#include "DataManager.hpp"
#include "clock/ClockCommands.hpp"
#include "query/ObjectQuery.hpp"

namespace ista::sdc {

TclSetClockLatency::TclSetClockLatency(const char* cmd_name, ClientData client_data) : SdcTclCmd(cmd_name, client_data)
{
  addOption(new ecc::TclStringOption("-clock", 0));
  for (const char* option : {"-rise", "-fall", "-min", "-max", "-source", "-early", "-late"}) {
    addOption(new ecc::TclSwitchOption(option));
  }
  addOption(new ecc::TclDoubleOption("delay", 1));
  addOption(new ecc::TclStringOption("objects", 1));
}

unsigned TclSetClockLatency::exec()
{
  ecc::TclOption* delay_option = getOptionOrArg("delay");
  ecc::TclOption* object_option = getOptionOrArg("objects");
  if (!delay_option->is_set_val() || !object_option->is_set_val()) {
    setTclError("set_clock_latency requires a delay and an object collection");
    return 0;
  }
  const double latency = timeValue("delay");
  if (!std::isfinite(latency)) {
    setTclError("set_clock_latency delay must be finite");
    return 0;
  }

  const bool source = getOptionOrArg("-source")->is_set_val();
  const bool early = getOptionOrArg("-early")->is_set_val();
  const bool late = getOptionOrArg("-late")->is_set_val();
  if (!source && (early || late)) {
    setTclError("set_clock_latency -early/-late require -source");
    return 0;
  }
  Database& database = STADM.getDatabase();
  std::vector<std::pair<QueryObjectType, std::string>> targets;
  std::set<std::string> selectors;
  try {
    targets = resolveClockTargets(interpreter(), database, getOptionObjects("objects"));
    if (getOptionOrArg("-clock")->is_set_val()) {
      for (const auto& [kind, name] : resolveClockTargets(interpreter(), database, getOptionObjects("-clock"), true))
        selectors.insert(name);
      for (const auto& [kind, name] : targets)
        if (kind == QueryObjectType::kClock)
          throw std::invalid_argument("-clock requires pin or port targets");
    } else
      selectors.insert("");
    if (source) {
      for (const auto& [kind, name] : targets) {
        if (kind == QueryObjectType::kClock)
          continue;
        bool is_source = false;
        for (const auto& [clock_name, clock] : database.sdc().get_clock_map()) {
          if (!selectors.contains("") && !selectors.contains(clock_name))
            continue;
          const bool matches = std::find(clock.get_source_list().begin(), clock.get_source_list().end(), name) != clock.get_source_list().end();
          if (!matches && !selectors.contains(""))
            throw std::invalid_argument("source latency target is not a source of clock " + clock_name);
          is_source |= matches;
        }
        if (!is_source)
          throw std::invalid_argument("source latency requires a clock source pin or port: " + name);
      }
    }
  } catch (const std::exception& error) {
    setTclError(error.what());
    return 0;
  }

  const bool rise = getOptionOrArg("-rise")->is_set_val();
  const bool fall = getOptionOrArg("-fall")->is_set_val();
  const bool min = getOptionOrArg("-min")->is_set_val();
  const bool max = getOptionOrArg("-max")->is_set_val();
  const bool select_min = min;
  const bool select_max = max;
  for (const auto& [kind, name] : targets) {
    for (AnalysisType analysis : {AnalysisType::kMin, AnalysisType::kMax}) {
      if ((analysis == AnalysisType::kMin && select_max && !select_min) || (analysis == AnalysisType::kMax && select_min && !select_max))
        continue;
      for (TransType edge : {TransType::kRise, TransType::kFall}) {
        if ((edge == TransType::kRise && fall && !rise) || (edge == TransType::kFall && rise && !fall))
          continue;
        if (kind == QueryObjectType::kClock) {
          auto& clock = database.sdc().get_clock_map().at(name);
          if (source) {
            if (early || !late)
              clock.set_source_latency(analysis, ClockVariation::kEarly, edge, latency);
            if (late || !early)
              clock.set_source_latency(analysis, ClockVariation::kLate, edge, latency);
          } else {
            clock.set_network_latency(analysis, edge, latency);
            clock.set_is_propagated(false);
          }
        } else {
          for (const auto& selector : selectors) {
            auto& value = database.sdc().clockPinConstraints()[name][selector];
            if (source) {
              if (early || !late)
                value.source_latency[analysis][ClockVariation::kEarly][edge] = latency;
              if (late || !early)
                value.source_latency[analysis][ClockVariation::kLate][edge] = latency;
            } else {
              value.network_latency[analysis][edge] = latency;
              value.propagated = false;
            }
          }
        }
      }
    }
  }
  return 1;
}

TclRemoveClockLatency::TclRemoveClockLatency(const char* cmd_name, ClientData client_data) : SdcTclCmd(cmd_name, client_data)
{
  addOption(new ecc::TclSwitchOption("-source"));
  addOption(new ecc::TclStringOption("-clock", 0));
  addOption(new ecc::TclStringOption("objects", 1));
}

unsigned TclRemoveClockLatency::exec()
{
  ecc::TclOption* object_option = getOptionOrArg("objects");
  if (!object_option->is_set_val()) {
    setTclError(std::string(get_cmd_name()) + " requires an object collection");
    return 0;
  }
  Database& database = STADM.getDatabase();
  try {
    const auto targets = resolveClockTargets(interpreter(), database, getOptionObjects("objects"));
    std::set<std::string> selectors;
    if (getOptionOrArg("-clock")->is_set_val()) {
      for (const auto& [kind, name] : resolveClockTargets(interpreter(), database, getOptionObjects("-clock"), true))
        selectors.insert(name);
      for (const auto& [kind, name] : targets)
        if (kind == QueryObjectType::kClock)
          throw std::invalid_argument("-clock requires pin or port targets");
    }
    const bool source = getOptionOrArg("-source")->is_set_val();
    for (const auto& [kind, name] : targets) {
      if (kind == QueryObjectType::kClock) {
        auto& clock = database.sdc().get_clock_map().at(name);
        if (source)
          clock.clear_source_latency();
        else
          clock.clear_network_latency();
      } else {
        auto found = database.sdc().clockPinConstraints().find(name);
        if (found == database.sdc().clockPinConstraints().end())
          continue;
        for (auto& [selector, value] : found->second) {
          if (!selectors.empty() && !selectors.contains(selector))
            continue;
          if (source)
            value.source_latency.clear();
          else {
            value.network_latency.clear();
            if (value.propagated == false)
              value.propagated.reset();
          }
        }
      }
    }
  } catch (const std::exception& error) {
    setTclError(error.what());
    return 0;
  }
  return 1;
}

}  // namespace ista::sdc
