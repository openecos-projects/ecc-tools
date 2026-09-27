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
#include "clock/ClockCommands.hpp"

namespace ista::sdc {

TclCreateGeneratedClock::TclCreateGeneratedClock(const char* cmd_name, ClientData client_data) : SdcTclCmd(cmd_name, client_data)
{
  for (const char* option : {"-name", "-master_clock", "-comment"}) {
    addOption(new ecc::TclStringOption(option, 0));
  }
  addOption(new ecc::TclStringListOption("-source", 0));
  addOption(new ecc::TclStringListOption("objects", 1));
  for (const char* option : {"-divide_by", "-multiply_by", "-duty_cycle"}) {
    addOption(new ecc::TclDoubleOption(option, 0));
  }
  for (const char* option : {"-edges", "-edge_shift"}) {
    addOption(new ecc::TclDoubleListOption(option, 0));
  }
  for (const char* option : {"-add", "-invert", "-preinvert", "-combinational"}) {
    addOption(new ecc::TclSwitchOption(option));
  }
}

unsigned TclCreateGeneratedClock::exec()
{
  Database& database = STADM.getDatabase();
  auto& clocks = database.get_timing_constraint().get_clock_map();
  if (!getOptionOrArg("-source")->is_set_val() || !getOptionOrArg("objects")->is_set_val()) {
    setTclError("create_generated_clock requires -source and target pins/ports");
    return 0;
  }
  std::vector<std::string> sources;
  std::vector<std::string> targets;
  try {
    sources = findClockSources(database, getOptionOrArg("-source")->getStringList());
    targets = findClockSources(database, getOptionOrArg("objects")->getStringList());
  } catch (const std::exception& error) {
    setTclError(error.what());
    return 0;
  }
  if (sources.size() != 1 || targets.empty()) {
    setTclError("invalid generated clock source or targets");
    return 0;
  }
  if (getOptionOrArg("-add")->is_set_val()) {
    if (!getOptionOrArg("-name")->is_set_val()) {
      setTclError("create_generated_clock -add requires -name");
      return 0;
    }
    if (!getOptionOrArg("-master_clock")->is_set_val()) {
      setTclError("create_generated_clock -add requires -master_clock");
      return 0;
    }
  }
  const std::string name = getOptionOrArg("-name")->is_set_val() ? getOptionOrArg("-name")->getStringVal() : targets.front();
  std::string master_name;
  if (getOptionOrArg("-master_clock")->is_set_val()) {
    master_name = getOptionOrArg("-master_clock")->getStringVal();
  } else {
    for (auto& [candidate, clock] : clocks) {
      if (std::find(clock.get_source_list().begin(), clock.get_source_list().end(), sources.front()) != clock.get_source_list().end()) {
        if (!master_name.empty()) {
          setTclError("ambiguous master; specify -master_clock");
          return 0;
        }
        master_name = candidate;
      }
    }
    if (master_name.empty() && clocks.size() == 1) {
      master_name = clocks.begin()->first;
    }
  }
  if (name.empty() || !clocks.contains(master_name)) {
    setTclError("generated clock master does not exist");
    return 0;
  }
  std::set<std::string> ancestors{name};
  for (std::string ancestor = master_name; !ancestor.empty();) {
    if (!ancestors.insert(ancestor).second) {
      setTclError("generated clock dependency cycle");
      return 0;
    }
    ancestor = clocks.at(ancestor).get_master_clock_name();
  }
  TimingClock& master = clocks.at(master_name);
  double master_period = master.get_period();
  double master_rise = master.get_rise_edge();
  double master_fall = master.get_fall_edge();
  if (getOptionOrArg("-preinvert")->is_set_val()) {
    const double old_rise = master_rise;
    master_rise = master_fall;
    master_fall = old_rise + master_period;
  }
  if (getOptionOrArg("-invert")->is_set_val() && getOptionOrArg("-preinvert")->is_set_val()) {
    setTclError("-invert and -preinvert are mutually exclusive");
    return 0;
  }
  int transformations = 0;
  for (const char* option : {"-divide_by", "-multiply_by", "-edges"}) {
    transformations += getOptionOrArg(option)->is_set_val();
  }
  if (transformations > 1) {
    setTclError("generated clock transformations are mutually exclusive");
    return 0;
  }
  const bool combinational = getOptionOrArg("-combinational")->is_set_val();
  if (transformations == 0 && !combinational) {
    setTclError("create_generated_clock requires -divide_by, -multiply_by, -edges, or -combinational");
    return 0;
  }
  if (combinational && getOptionOrArg("-divide_by")->is_set_val() && getOptionOrArg("-divide_by")->getDoubleVal() != 1.0) {
    setTclError("create_generated_clock -combinational only accepts -divide_by 1");
    return 0;
  }
  if (getOptionOrArg("-invert")->is_set_val() && getOptionOrArg("-edges")->is_set_val()) {
    setTclError("create_generated_clock -invert cannot be used with -edges");
    return 0;
  }
  double period = master_period;
  double rise = master_rise;
  double fall = master_fall;
  for (const char* option : {"-divide_by", "-multiply_by"}) {
    if (!getOptionOrArg(option)->is_set_val()) {
      continue;
    }
    const double factor = getOptionOrArg(option)->getDoubleVal();
    if (!std::isfinite(factor) || factor < 1 || factor != std::floor(factor)) {
      setTclError("generated clock factor must be a positive integer");
      return 0;
    }
    if (std::string_view(option) == "-multiply_by") {
      period /= factor;
      rise /= factor;
      fall /= factor;
    } else {
      period *= factor;
      const bool power_of_two = std::floor(std::log2(factor)) == std::log2(factor);
      if (factor > 1 && power_of_two) {
        fall = rise + period / 2;
      } else {
        rise *= factor;
        fall *= factor;
      }
    }
  }
  if (getOptionOrArg("-edges")->is_set_val()) {
    const std::vector<double> edges = getOptionOrArg("-edges")->getDoubleList();
    const std::vector<double> shifts
        = getOptionOrArg("-edge_shift")->is_set_val() ? getOptionOrArg("-edge_shift")->getDoubleList() : std::vector<double>(3, 0.0);
    if (edges.size() != 3 || shifts.size() != 3) {
      setTclError("-edges and -edge_shift require three values");
      return 0;
    }
    std::vector<double> times;
    for (std::size_t i = 0; i < 3; ++i) {
      if (!std::isfinite(edges[i]) || edges[i] < 1 || edges[i] != std::floor(edges[i]) || (i && edges[i] < edges[i - 1])) {
        setTclError("-edges requires three nondecreasing positive integers");
        return 0;
      }
      const double index = edges[i] - 1;
      times.push_back(std::floor(index / 2) * master_period + (std::fmod(index, 2) == 0 ? master_rise : master_fall) + shifts[i]);
    }
    period = times[2] - times[0];
    rise = times[0];
    fall = times[1];
  } else if (getOptionOrArg("-edge_shift")->is_set_val()) {
    setTclError("-edge_shift requires -edges");
    return 0;
  }
  if (getOptionOrArg("-duty_cycle")->is_set_val()) {
    const double duty = getOptionOrArg("-duty_cycle")->getDoubleVal();
    if (!getOptionOrArg("-multiply_by")->is_set_val() || !std::isfinite(duty) || duty <= 0 || duty >= 100) {
      setTclError("-duty_cycle requires -multiply_by and a percentage between 0 and 100");
      return 0;
    }
    fall = rise + period * duty / 100;
  }
  if (getOptionOrArg("-invert")->is_set_val()) {
    const double old_rise = rise;
    rise = fall;
    fall = old_rise + period;
  }
  if (!std::isfinite(period) || !std::isfinite(rise) || !std::isfinite(fall) || period <= 0 || fall < rise || fall >= rise + period) {
    setTclError("invalid generated clock waveform");
    return 0;
  }
  TimingClock generated;
  generated.set_clock_name(name);
  generated.set_master_clock_name(master_name);
  generated.set_master_source(sources.front());
  generated.set_source_list(targets);
  GeneratedClockDefinition definition;
  definition.master_clock = master_name;
  definition.master_source = sources.front();
  definition.targets = targets;
  if (getOptionOrArg("-divide_by")->is_set_val()) definition.divide_by = getOptionOrArg("-divide_by")->getDoubleVal();
  if (getOptionOrArg("-multiply_by")->is_set_val()) definition.multiply_by = getOptionOrArg("-multiply_by")->getDoubleVal();
  if (getOptionOrArg("-duty_cycle")->is_set_val()) definition.duty_cycle = getOptionOrArg("-duty_cycle")->getDoubleVal();
  if (getOptionOrArg("-edges")->is_set_val()) definition.edges = getOptionOrArg("-edges")->getDoubleList();
  if (getOptionOrArg("-edge_shift")->is_set_val()) definition.edge_shifts = getOptionOrArg("-edge_shift")->getDoubleList();
  definition.invert = getOptionOrArg("-invert")->is_set_val();
  definition.preinvert = getOptionOrArg("-preinvert")->is_set_val();
  definition.combinational = combinational;
  definition.add = getOptionOrArg("-add")->is_set_val();
  generated.set_generated_clock_definition(std::move(definition));
  generated.set_period(period);
  generated.set_rise_edge(rise);
  generated.set_fall_edge(fall);
  generated.set_waveform({rise, fall});
  if (getOptionOrArg("-comment")->is_set_val()) {
    generated.set_comment(getOptionOrArg("-comment")->getStringVal());
  }
  if (!getOptionOrArg("-add")->is_set_val()) {
    for (auto& [other_name, other] : clocks) {
      std::vector<std::string>& roots = other.get_source_list();
      roots.erase(
          std::remove_if(roots.begin(), roots.end(), [&](const std::string& pin) { return std::find(targets.begin(), targets.end(), pin) != targets.end(); }),
          roots.end());
    }
  }
  clocks[name] = std::move(generated);
  return 1;
}

}  // namespace ista::sdc
