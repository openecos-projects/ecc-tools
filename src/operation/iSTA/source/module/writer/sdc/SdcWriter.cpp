// SPDX-License-Identifier: MulanPSL-2.0
#include "SdcWriter.hpp"

#include <unistd.h>

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <ostream>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "Database.hpp"
#include "SdcObjectWriter.hpp"

namespace ista::sdc {
namespace {
std::string timeNumber(Database& database, double value)
{
  return sdcNumber(value / database.liberty().timeUnitInNs());
}
std::string capNumber(Database& database, double value)
{
  return sdcNumber(value / database.liberty().capUnitInPf());
}
std::string timeNumbers(Database& database, std::vector<double> values)
{
  for (auto& value : values)
    value /= database.liberty().timeUnitInNs();
  return sdcNumbers(values);
}
std::string minMax(AnalysisType type)
{
  return type == AnalysisType::kMin ? " -min" : " -max";
}
std::string riseFall(TransType type)
{
  return type == TransType::kRise ? " -rise" : " -fall";
}
std::string selector(const char* suffix, TransType type)
{
  return std::string(" -") + (type == TransType::kRise ? "rise_" : type == TransType::kFall ? "fall_" : "") + suffix + " ";
}
std::string clocks(const std::set<std::string>& names)
{
  return sdcQuery("get_clocks", {names.begin(), names.end()});
}

void writeClock(Database& database, const std::string& name, std::ostream& out)
{
  auto& clock = database.sdc().get_clock_map().at(name);
  const auto& sources = clock.get_source_objects();
  if (clock.get_is_generated()) {
    if (!clock.get_generated_clock_definition())
      throw std::runtime_error("missing generated clock definition: " + name);
    const auto& definition = *clock.get_generated_clock_definition();
    out << "create_generated_clock -name " << sdcWord(name) << " -master_clock " << sdcWord(clock.get_master_clock_name()) << " -source "
        << sdcObjects(database, {definition.master_source});
    if (definition.preinvert)
      throw std::runtime_error("write_sdc cannot preserve -preinvert in standard SDC: " + name);
    if (definition.divide_by)
      out << " -divide_by " << sdcNumber(*definition.divide_by);
    if (definition.multiply_by)
      out << " -multiply_by " << sdcNumber(*definition.multiply_by);
    if (definition.duty_cycle)
      out << " -duty_cycle " << sdcNumber(*definition.duty_cycle);
    if (definition.edges)
      out << " -edges " << sdcNumbers(*definition.edges);
    if (definition.edge_shifts)
      out << " -edge_shift " << timeNumbers(database, *definition.edge_shifts);
    if (definition.invert)
      out << " -invert";
    if (definition.combinational)
      out << " -combinational";
    // Always add when serializing a snapshot: creation order must not remove
    // sources from another clock sharing the same object.
    out << " -add";
    if (sources.empty())
      throw std::runtime_error("generated clock has no remaining targets: " + name);
  } else {
    out << "create_clock -name " << sdcWord(name) << " -period " << timeNumber(database, clock.get_period());
    const auto& waveform = clock.get_waveform();
    out << " -waveform " << timeNumbers(database, waveform.empty() ? std::vector<double>{clock.get_rise_edge(), clock.get_fall_edge()} : waveform);
    if (!sources.empty())
      out << " -add";
  }
  if (!clock.get_comment().empty())
    out << " -comment " << sdcWord(clock.get_comment());
  if (!sources.empty())
    out << " " << sdcObjects(database, sources);
  out << '\n';
}

void writeClocks(Database& database, std::ostream& out)
{
  auto& map = database.sdc().get_clock_map();
  // Iterative topological traversal avoids recursion limits on deep chains.
  std::set<std::string> pending;
  for (const auto& [name, clock] : map)
    pending.insert(name);
  while (!pending.empty()) {
    bool progress = false;
    for (auto iter = pending.begin(); iter != pending.end();) {
      auto& clock = map.at(*iter);
      if (clock.get_is_generated()) {
        const auto& master = clock.get_master_clock_name();
        if (!map.contains(master))
          throw std::runtime_error("missing generated clock master: " + master);
        if (pending.contains(master)) {
          ++iter;
          continue;
        }
      }
      writeClock(database, *iter, out);
      iter = pending.erase(iter);
      progress = true;
    }
    if (!progress)
      throw std::runtime_error("cyclic generated clock dependencies");
  }
  for (auto& [name, clock] : map) {
    const std::string target = sdcQuery("get_clocks", {name});
    for (const auto& [analysis, transitions] : clock.get_transition_map()) {
      for (const auto& [transition, value] : transitions)
        out << "set_clock_transition" << minMax(analysis) << riseFall(transition) << " " << timeNumber(database, value) << " " << target << '\n';
    }
    for (const auto& [analysis, variations] : clock.get_source_latency_map())
      for (const auto& [variation, transitions] : variations)
        for (const auto& [transition, value] : transitions)
          out << "set_clock_latency -source" << minMax(analysis) << (variation == ClockVariation::kEarly ? " -early" : " -late") << riseFall(transition) << " "
              << timeNumber(database, value) << " " << target << '\n';
    for (const auto& [analysis, transitions] : clock.get_network_latency_map())
      for (const auto& [transition, value] : transitions)
        out << "set_clock_latency" << minMax(analysis) << riseFall(transition) << " " << timeNumber(database, value) << " " << target << '\n';
    // Network-latency setters restore ideal mode. Reapply propagated mode last.
    if (clock.get_is_propagated())
      out << "set_propagated_clock " << target << '\n';
    // The model uses zero as its unset/default uncertainty. Emitting both
    // checks explicitly preserves those effective defaults as well.
    out << "set_clock_uncertainty -setup " << timeNumber(database, clock.get_setup_uncertainty()) << " " << target << '\n';
    out << "set_clock_uncertainty -hold " << timeNumber(database, clock.get_hold_uncertainty()) << " " << target << '\n';
  }
  for (const auto& [pin, selectors] : database.sdc().clockPinConstraints()) {
    const auto target = sdcObjects(database, {pin});
    // A propagated pin is not clock-specific. Setters discard its old ideal latency.
    const auto generic = selectors.find("");
    if (generic != selectors.end() && generic->second.propagated == true)
      out << "set_propagated_clock " << target << '\n';
    for (const auto& [clock, value] : selectors) {
      const auto select = clock.empty() ? std::string{} : " -clock " + sdcQuery("get_clocks", {clock});
      for (const auto& [analysis, variations] : value.source_latency)
        for (const auto& [variation, transitions] : variations)
          for (const auto& [edge, latency] : transitions)
            out << "set_clock_latency -source" << select << minMax(analysis) << (variation == ClockVariation::kEarly ? " -early" : " -late") << riseFall(edge)
                << " " << timeNumber(database, latency) << " " << target << '\n';
      for (const auto& [analysis, transitions] : value.network_latency)
        for (const auto& [edge, latency] : transitions)
          out << "set_clock_latency" << select << minMax(analysis) << riseFall(edge) << " " << timeNumber(database, latency) << " " << target << '\n';
      if (value.setup_uncertainty)
        out << "set_clock_uncertainty -setup " << timeNumber(database, *value.setup_uncertainty) << " " << target << '\n';
      if (value.hold_uncertainty)
        out << "set_clock_uncertainty -hold " << timeNumber(database, *value.hold_uncertainty) << " " << target << '\n';
    }
  }
}

void writeIoDelay(Database& database, std::ostream& out, const std::string& port, const TimingIoDelay& delay, bool input)
{
  if (!delay.get_group_path().empty())
    throw std::runtime_error("cannot export an I/O delay with an internal path group");
  out << (input ? "set_input_delay" : "set_output_delay") << minMax(delay.get_analysis_type()) << riseFall(delay.get_trans_type()) << " -add_delay";
  if (!delay.get_clock_name().empty())
    out << " -clock " << sdcQuery("get_clocks", {delay.get_clock_name()});
  if (delay.get_clock_trans_type() == TransType::kFall)
    out << " -clock_fall";
  if (!delay.get_reference_pin().empty())
    out << " -reference_pin " << sdcObjects(database, {delay.get_reference_pin()});
  if (delay.get_level_sensitive())
    out << " -level_sensitive";
  if (delay.get_network_latency_included())
    out << " -network_latency_included";
  if (delay.get_source_latency_included())
    out << " -source_latency_included";
  out << " " << timeNumber(database, delay.get_delay()) << " " << sdcQuery("get_ports", {port}) << '\n';
}

void writePorts(Database& database, std::ostream& out)
{
  for (auto& [name, port] : database.sdc().get_port_constraint_map()) {
    const std::string target = sdcQuery("get_ports", {name});
    for (const auto& delay : port.get_input_delay_list())
      writeIoDelay(database, out, name, delay, true);
    for (const auto& delay : port.get_output_delay_list())
      writeIoDelay(database, out, name, delay, false);
    for (bool write_drivers : {false, true})
      for (AnalysisType analysis : {AnalysisType::kMin, AnalysisType::kMax}) {
        for (TransType transition : {TransType::kRise, TransType::kFall}) {
          if (!write_drivers && port.get_has_input_transition(analysis, transition)) {
            out << "set_input_transition" << minMax(analysis) << riseFall(transition) << " "
                << timeNumber(database, port.get_input_transition(analysis, transition)) << " " << target << '\n';
          } else if (auto* drive = port.get_driving_cell(analysis, transition); write_drivers && drive) {
            out << "set_driving_cell" << minMax(analysis) << riseFall(transition) << " -lib_cell " << sdcWord(drive->get_cell_name());
            if (drive->get_no_design_rule())
              out << " -no_design_rule";
            if (!drive->get_library_name().empty())
              out << " -library " << sdcWord(drive->get_library_name());
            if (!drive->get_from_pin().empty())
              out << " -from_pin " << sdcWord(drive->get_from_pin());
            out << " -pin " << sdcWord(drive->get_to_pin()) << " -input_transition_rise " << timeNumber(database, drive->get_input_transition_rise())
                << " -input_transition_fall " << timeNumber(database, drive->get_input_transition_fall()) << " " << target << '\n';
          }
        }
      }
    for (const auto& load : port.get_loads()) {
      out << "set_load" << (load.wire ? " -wire_load" : " -pin_load") << minMax(load.analysis) << riseFall(load.transition) << " "
          << capNumber(database, load.value) << " " << target << '\n';
    }
  }
}

std::string exceptionSelectors(Database& database, const TimingException& exception)
{
  std::string text;
  if (!exception.get_from_objects().empty())
    text += selector("from", exception.get_from_trans_type()) + sdcExceptionObjects(database, exception.get_from_objects());
  for (const auto& through : exception.get_through_list())
    text += selector("through", through.get_trans_type()) + sdcExceptionObjects(database, through.get_objects());
  if (!exception.get_to_objects().empty())
    text += selector("to", exception.get_to_trans_type()) + sdcExceptionObjects(database, exception.get_to_objects());
  if (exception.get_rise() != exception.get_fall())
    text += exception.get_rise() ? " -rise" : " -fall";
  if (!exception.get_comment().empty())
    text += " -comment " + sdcWord(exception.get_comment());
  return text;
}

void writeExceptions(Database& database, std::ostream& out)
{
  for (const auto& exception : database.sdc().get_path_exception_list()) {
    if (!exception.get_setup() && !exception.get_hold())
      continue;
    const auto selectors = exceptionSelectors(database, exception);
    switch (exception.get_type()) {
      case TimingExceptionType::kFalsePath:
        out << "set_false_path";
        if (exception.get_setup() != exception.get_hold())
          out << (exception.get_setup() ? " -setup" : " -hold");
        out << selectors << '\n';
        break;
      case TimingExceptionType::kMaxDelay:
      case TimingExceptionType::kMinDelay:
        if (exception.get_probe())
          throw std::runtime_error("set_max_delay/set_min_delay -probe cannot be represented in standard SDC");
        out << (exception.get_type() == TimingExceptionType::kMaxDelay ? "set_max_delay " : "set_min_delay ") << timeNumber(database, exception.get_delay());
        if (exception.get_ignore_clock_latency())
          out << " -ignore_clock_latency";
        out << selectors << '\n';
        break;
      case TimingExceptionType::kMulticycle:
        if (exception.get_setup_multiplier())
          out << "set_multicycle_path -setup" << (exception.get_setup_use_end_clock() ? " -end " : " -start ") << *exception.get_setup_multiplier() << selectors
              << '\n';
        if (exception.get_hold_multiplier())
          out << "set_multicycle_path -hold" << (exception.get_hold_use_end_clock() ? " -end " : " -start ") << *exception.get_hold_multiplier() << selectors
              << '\n';
        break;
    }
  }
}
}  // namespace

void writeSdc(Database& database, std::ostream& output)
{
  // Buffer first so invalid model state cannot leave a partial script behind.
  std::ostringstream out;
  const auto time_unit = database.liberty().get_time_unit();
  const auto cap_unit = database.liberty().get_cap_unit();
  out << "# iSTA effective constraints; SDC 2.1\nset_units -time "
      << (time_unit == TimingTimeUnit::kPS   ? "ps"
          : time_unit == TimingTimeUnit::kFS ? "fs"
                                             : "ns")
      << " -capacitance "
      << (cap_unit == TimingCapacitiveUnit::kFF  ? "fF"
          : cap_unit == TimingCapacitiveUnit::kF ? "F"
                                                 : "pF")
      << '\n';
  writeClocks(database, out);
  auto& constraints = database.sdc();
  for (const auto& uncertainty : constraints.get_clock_uncertainty_list()) {
    out << "set_clock_uncertainty";
    if (uncertainty.get_setup() != uncertainty.get_hold())
      out << (uncertainty.get_setup() ? " -setup" : " -hold");
    out << selector("from", uncertainty.get_from_trans_type()) << clocks(uncertainty.get_from_clocks()) << selector("to", uncertainty.get_to_trans_type())
        << clocks(uncertainty.get_to_clocks()) << " " << timeNumber(database, uncertainty.get_value()) << '\n';
  }
  writePorts(database, out);
  for (const auto& limit : constraints.electricalLimits().entries()) {
    out << (limit.rule == ElectricalRule::kTransition ? "set_max_transition " : "set_max_capacitance ")
        << (limit.rule == ElectricalRule::kTransition ? timeNumber(database, limit.value) : capNumber(database, limit.value));
    if (limit.force)
      out << " -force";
    if (limit.object == LimitObject::kClock)
      out << (limit.clock_path ? " -clock_path" : " -data_path") << riseFall(limit.edge);
    switch (limit.object) {
      case LimitObject::kDesign:
        out << " [current_design]";
        break;
      case LimitObject::kPort:
        out << " " << sdcQuery("get_ports", {limit.name});
        break;
      case LimitObject::kPin:
        out << " " << sdcQuery("get_pins", {limit.name});
        break;
      case LimitObject::kLibPin:
        out << " " << sdcQuery("get_lib_pins", {limit.name});
        break;
      case LimitObject::kClock:
        out << " " << sdcQuery("get_clocks", {limit.name});
        break;
    }
    out << '\n';
  }
  for (const auto& [key, factor] : constraints.timingDerates().entries()) {
    const auto& [analysis, clock, edge, kind, object, name, increment] = key;
    out << "set_timing_derate";
    if (analysis != AnalysisType::kNone)
      out << (analysis == AnalysisType::kMin ? " -early" : " -late");
    if (kind == DerateDelay::kCell || kind == DerateDelay::kNet)
      out << (clock ? " -clock" : " -data");
    out << riseFall(edge)
        << (kind == DerateDelay::kCell     ? " -cell_delay"
            : kind == DerateDelay::kNet    ? " -net_delay"
            : kind == DerateDelay::kPeriod ? " -min_period"
            : kind == DerateDelay::kWidth  ? " -min_pulse_width"
                                           : " -cell_check")
        << (increment ? " -increment " : " ") << sdcNumber(factor);
    if (object != DerateObject::kDesign)
      out << " "
          << sdcQuery(object == DerateObject::kNet       ? "get_nets"
                      : object == DerateObject::kLibCell ? "get_lib_cells"
                                                         : "get_cells",
                      {object == DerateObject::kNet ? database.network().netQueryName(name) : name});
    out << '\n';
  }
  for (const auto& [name, load] : constraints.get_net_load_map()) {
    for (AnalysisType analysis : {AnalysisType::kMin, AnalysisType::kMax}) {
      // Net loads have no rise/fall qualifier in SDC.
      const bool rise = load.has(analysis, TransType::kRise), fall = load.has(analysis, TransType::kFall);
      if (!rise && !fall)
        continue;
      if (!rise || !fall || load.get(analysis, TransType::kRise) != load.get(analysis, TransType::kFall))
        throw std::runtime_error("transition-dependent net load cannot be represented in SDC: " + name);
      out << "set_load" << minMax(analysis) << " " << capNumber(database, load.get(analysis, TransType::kRise)) << " "
          << sdcQuery("get_nets", {database.network().netQueryName(name)}) << '\n';
    }
  }
  for (const auto& [name, value] : constraints.get_case_analysis_map()) {
    const char* literal;
    switch (value) {
      case TimingCaseValue::kZero:
        literal = "0";
        break;
      case TimingCaseValue::kOne:
        literal = "1";
        break;
      case TimingCaseValue::kRise:
        literal = "rising";
        break;
      case TimingCaseValue::kFall:
        literal = "falling";
        break;
      default:
        throw std::runtime_error("case analysis value cannot be represented in SDC: " + name);
    }
    out << "set_case_analysis " << literal << " " << sdcObjects(database, {name}) << '\n';
  }
  if (constraints.get_max_fanout())
    out << "set_max_fanout " << sdcNumber(*constraints.get_max_fanout()) << " [current_design]\n";
  for (const auto& [name, value] : constraints.get_port_max_fanout_map())
    out << "set_max_fanout " << sdcNumber(value) << " " << sdcQuery("get_ports", {name}) << '\n';
  for (const auto& group : constraints.get_clock_group_list()) {
    out << "set_clock_groups";
    switch (group.get_type()) {
      case TimingClockGroupType::kAsynchronous:
        out << " -asynchronous";
        break;
      case TimingClockGroupType::kLogicallyExclusive:
        out << " -logically_exclusive";
        break;
      case TimingClockGroupType::kPhysicallyExclusive:
        out << " -physically_exclusive";
        break;
      default:
        throw std::runtime_error("clock group relationship cannot be represented in SDC");
    }
    if (group.get_allow_paths())
      out << " -allow_paths";
    if (!group.get_name().empty())
      out << " -name " << sdcWord(group.get_name());
    if (!group.get_comment().empty())
      out << " -comment " << sdcWord(group.get_comment());
    for (const auto& clocks_in_group : group.get_groups())
      out << " -group " << clocks(clocks_in_group);
    out << '\n';
  }
  writeExceptions(database, out);
  output << out.str();
  if (!output)
    throw std::runtime_error("failed to write SDC stream");
}

void writeSdcFile(Database& database, const std::string& path)
{
  if (path.empty())
    throw std::invalid_argument("write_sdc requires a non-empty file_name");
  std::ostringstream content;
  writeSdc(database, content);
  std::string temporary = path + ".tmp.XXXXXX";
  std::vector<char> buffer(temporary.begin(), temporary.end());
  buffer.push_back('\0');
  const int fd = mkstemp(buffer.data());
  if (fd < 0)
    throw std::runtime_error("cannot create SDC output '" + path + "': " + std::strerror(errno));
  temporary = buffer.data();
  FILE* file = fdopen(fd, "wb");
  if (file == nullptr) {
    close(fd);
    std::remove(temporary.c_str());
    throw std::runtime_error("cannot open temporary SDC output");
  }
  const auto text = content.str();
  const bool written = fwrite(text.data(), 1, text.size(), file) == text.size();
  const bool closed = fclose(file) == 0;
  if (!written || !closed || std::rename(temporary.c_str(), path.c_str()) != 0) {
    const std::string error = std::strerror(errno);
    std::remove(temporary.c_str());
    throw std::runtime_error("failed to write SDC output '" + path + "': " + error);
  }
}
}  // namespace ista::sdc
