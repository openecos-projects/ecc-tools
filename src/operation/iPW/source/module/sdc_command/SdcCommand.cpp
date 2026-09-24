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
#include "SdcCommand.hpp"

#ifndef _WIN32
#include <dlfcn.h>
#endif

#include <cctype>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <string_view>

#include "DataManager.hpp"
#include "Logger.hpp"
#include "PWHeader.hpp"
#include "clock/ClockCommands.hpp"
#include "delay/DelayCommands.hpp"
#include "frontend/SdcTclCmd.hpp"
#include "logic/LogicCommands.hpp"
#include "object/ObjectCommands.hpp"
#include "object/ObjectQuery.hpp"

namespace ipw {

namespace sdc {
void registerSdcCommands(SdcCommand& interpreter);
}

namespace {

// The python wheel bundles the matching Tcl script library next to the module
// (ecc_tools_bin/tcl8.6, installed by tcl_engine/CMakeLists.txt) because the
// search paths compiled into libtcl only exist on RHEL-like distros (Debian
// uses /usr/share/tcltk). Returns an empty path for unpackaged builds, where
// the system paths apply.
std::filesystem::path getBundledTclLibrary()
{
#ifndef _WIN32
  namespace fs = std::filesystem;
  Dl_info info;
  if (dladdr(reinterpret_cast<void*>(&SdcCommand::getInst), &info) == 0 || info.dli_fname == nullptr) {
    return {};
  }
  const fs::path bundled = fs::path(info.dli_fname).parent_path() / "tcl8.6";
  std::error_code ec;
  return fs::exists(bundled / "init.tcl", ec) ? bundled : fs::path{};
#else
  // No module self-location on Windows; fall back to the system Tcl paths.
  return {};
#endif
}

}  // namespace

SdcCommand* SdcCommand::_sdc_command_instance = nullptr;

void SdcCommand::initInst()
{
  if (_sdc_command_instance == nullptr) {
    _sdc_command_instance = new SdcCommand();
    sdc::registerSdcCommands(*_sdc_command_instance);
  }
}

SdcCommand& SdcCommand::getInst()
{
  if (_sdc_command_instance == nullptr) {
    PWLOG.error(Loc::current(), "The instance not initialized!");
  }
  return *_sdc_command_instance;
}

void SdcCommand::destroyInst()
{
  if (_sdc_command_instance != nullptr) {
    delete _sdc_command_instance;
    _sdc_command_instance = nullptr;
  }
}

Tcl_Command SdcCommand::createCmd(const char* cmd_name, Tcl_ObjCmdProc* proc)
{
  return Tcl_CreateObjCommand(_interp, cmd_name, proc, nullptr, nullptr);
}

int SdcCommand::evalScriptFile(const std::string& file_name)
{
  clearError();
  std::ifstream script_file(file_name);
  if (!script_file.is_open()) {
    setError(0, "failed to open SDC file '" + file_name + "'");
    return TCL_ERROR;
  }

  const std::string script((std::istreambuf_iterator<char>(script_file)), std::istreambuf_iterator<char>());
  return evalScript(script);
}

SdcCommand::SdcCommand()
{
  _interp = Tcl_CreateInterp();
  // An explicit TCL_LIBRARY always wins; only fall back to the bundled
  // scripts when the user has not chosen a script library themselves.
  if (std::getenv("TCL_LIBRARY") == nullptr) {
    const std::filesystem::path bundled_library = getBundledTclLibrary();
    if (!bundled_library.empty()) {
      Tcl_SetVar(_interp, "tcl_library", bundled_library.string().c_str(), TCL_GLOBAL_ONLY);
    }
  }
  if (Tcl_Init(_interp) != TCL_OK) {
    const std::string message = Tcl_GetStringResult(_interp);
    Tcl_DeleteInterp(_interp);
    _interp = nullptr;
    throw std::runtime_error("failed to initialize SDC Tcl interpreter: " + message);
  }
}

SdcCommand::~SdcCommand()
{
  if (_interp != nullptr) {
    Tcl_DeleteInterp(_interp);
  }
}

int SdcCommand::evalScript(const std::string& script)
{
  clearError();
  int result = TCL_OK;
  std::size_t offset = 0;
  unsigned line_number = 1;

  while (offset < script.size()) {
    Tcl_Parse parse{};
    const char* command_start = script.data() + offset;
    const int parse_result = Tcl_ParseCommand(_interp, command_start, static_cast<int>(script.size() - offset), 0, &parse);
    if (parse_result != TCL_OK) {
      const char* error_start = parse.commandStart != nullptr ? parse.commandStart : command_start;
      const unsigned command_line = line_number + static_cast<unsigned>(std::count(command_start, error_start, '\n'));
      setError(static_cast<int32_t>(command_line), Tcl_GetStringResult(_interp));
      Tcl_ResetResult(_interp);
      Tcl_FreeParse(&parse);
      return TCL_ERROR;
    }

    const char* command_end = parse.commandStart + parse.commandSize;
    std::size_t consumed = static_cast<std::size_t>(command_end - command_start);
    if (consumed == 0) {
      consumed = script.size() - offset;
    }

    const unsigned command_line = line_number + static_cast<unsigned>(std::count(command_start, parse.commandStart, '\n'));

    if (parse.numWords > 0) {
      Tcl_SetErrorLine(_interp, static_cast<int>(command_line));
      const int command_result = Tcl_EvalEx(_interp, parse.commandStart, parse.commandSize, TCL_EVAL_GLOBAL);
      if (command_result != TCL_OK) {
        setError(static_cast<int32_t>(command_line), Tcl_GetStringResult(_interp));
        Tcl_ResetResult(_interp);
        Tcl_FreeParse(&parse);
        return TCL_ERROR;
      }
    }

    line_number += static_cast<unsigned>(std::count(command_start, command_start + consumed, '\n'));
    offset += consumed;
    Tcl_FreeParse(&parse);
  }

  return result;
}

void SdcCommand::clearError()
{
  _error_line_number = 0;
  _error_message.clear();
}

void SdcCommand::setError(int32_t line_number, std::string message)
{
  _error_line_number = line_number;
  _error_message = std::move(message);
}

namespace sdc {

namespace {

template <typename Command>
void registerCommand(SdcCommand& interpreter, const char* name)
{
  interpreter.createCmd(name, &executeTclCommand<Command>);
}

int ignoreTimingConstraint(ClientData, Tcl_Interp*, int, Tcl_Obj* const[])
{
  return TCL_OK;
}

int appendToCollection(ClientData, Tcl_Interp* interp, int objc, Tcl_Obj* const objv[])
{
  Tcl_Obj* result = Tcl_NewListObj(0, nullptr);
  for (int index = 1; index < objc; ++index) {
    int item_num = 0;
    Tcl_Obj** item_list = nullptr;
    if (Tcl_ListObjGetElements(interp, objv[index], &item_num, &item_list) != TCL_OK) {
      return TCL_ERROR;
    }
    for (int item_idx = 0; item_idx < item_num; ++item_idx) {
      Tcl_ListObjAppendElement(interp, result, item_list[item_idx]);
    }
  }
  Tcl_SetObjResult(interp, result);
  return TCL_OK;
}

int getDesigns(ClientData, Tcl_Interp* interp, int objc, Tcl_Obj* const objv[])
{
  if (objc > 2) {
    Tcl_SetObjResult(interp, Tcl_NewStringObj("get_designs accepts at most one pattern", -1));
    return TCL_ERROR;
  }
  const std::string& design_name = PWDM.getDatabase().get_design_name();
  const char* pattern = objc == 2 ? Tcl_GetString(objv[1]) : "*";
  Tcl_Obj* result = Tcl_NewListObj(0, nullptr);
  if (!design_name.empty() && Tcl_StringMatch(design_name.c_str(), pattern) != 0) {
    Tcl_ListObjAppendElement(interp, result, Tcl_NewStringObj(design_name.c_str(), static_cast<int>(design_name.size())));
  }
  Tcl_SetObjResult(interp, result);
  return TCL_OK;
}

int getLibraries(ClientData, Tcl_Interp* interp, int objc, Tcl_Obj* const objv[])
{
  if (objc > 2) {
    Tcl_SetObjResult(interp, Tcl_NewStringObj("get_libs accepts at most one pattern", -1));
    return TCL_ERROR;
  }
  const char* pattern = objc == 2 ? Tcl_GetString(objv[1]) : "*";
  std::set<std::string> library_name_set;
  for (std::pair<const std::string, TimingCell>& cell_pair : PWDM.getDatabase().get_timing_library().get_cell_map()) {
    library_name_set.insert(cell_pair.second.get_library_name());
  }
  Tcl_Obj* result = Tcl_NewListObj(0, nullptr);
  for (const std::string& library_name : library_name_set) {
    if (Tcl_StringMatch(library_name.c_str(), pattern) != 0) {
      Tcl_ListObjAppendElement(interp, result, Tcl_NewStringObj(library_name.c_str(), static_cast<int>(library_name.size())));
    }
  }
  Tcl_SetObjResult(interp, result);
  return TCL_OK;
}

int getEmptyObjectProperty(ClientData, Tcl_Interp* interp, int, Tcl_Obj* const[])
{
  Tcl_ResetResult(interp);
  return TCL_OK;
}

void registerCollectionCommands(SdcCommand& interpreter)
{
  registerCommand<TclRemoveFromCollection>(interpreter, "remove_from_collection");
  interpreter.createCmd("append_to_collection", &appendToCollection);
}

void registerObjectQueryCommands(SdcCommand& interpreter)
{
  registerCommand<TclCurrentDesign>(interpreter, "current_design");
  registerCommand<TclAllInputs>(interpreter, "all_inputs");
  registerCommand<TclAllOutputs>(interpreter, "all_outputs");
  registerCommand<TclAllClocks>(interpreter, "all_clocks");
  registerCommand<TclGetObjectName>(interpreter, "get_object_name");
  registerCommand<TclGetClocks>(interpreter, "get_clock");
  registerCommand<TclGetClocks>(interpreter, "get_clocks");
  registerCommand<TclGetGeneratedClocks>(interpreter, "get_generated_clocks");
  registerCommand<TclGetPorts>(interpreter, "get_port");
  registerCommand<TclGetPorts>(interpreter, "get_ports");
  registerCommand<TclGetCells>(interpreter, "get_cell");
  registerCommand<TclGetCells>(interpreter, "get_cells");
  registerCommand<TclGetNets>(interpreter, "get_net");
  registerCommand<TclGetNets>(interpreter, "get_nets");
  registerCommand<TclGetPins>(interpreter, "get_pins");
  interpreter.createCmd("get_designs", &getDesigns);
  interpreter.createCmd("get_lib", &getLibraries);
  interpreter.createCmd("get_libs", &getLibraries);
  interpreter.createCmd("get_property", &getEmptyObjectProperty);
}

void registerClockConstraintCommands(SdcCommand& interpreter)
{
  registerCommand<TclCreateClock>(interpreter, "create_clock");
  registerCommand<TclCreateGeneratedClock>(interpreter, "create_generated_clock");
  registerCommand<TclSetClockTransition>(interpreter, "set_clock_transition");
  registerCommand<TclSetPropagatedClock>(interpreter, "set_propagated_clock");
}

void registerSignalConstraintCommands(SdcCommand& interpreter)
{
  registerCommand<TclSetDrivingCell>(interpreter, "set_driving_cell");
  registerCommand<TclSetInputTransition>(interpreter, "set_input_transition");
  registerCommand<TclSetLoad>(interpreter, "set_load");
}

void registerIgnoredTimingConstraintCommands(SdcCommand& interpreter)
{
  // Common design SDC files also contain path-analysis constraints. They do
  // not affect power, but accepting them keeps the power-relevant commands in
  // the same file executable.
  for (const char* command_name : {"group_path", "set_all_input_output_delays", "set_clock_gating_check", "set_clock_groups",
                                   "set_clock_latency", "set_clock_uncertainty", "set_false_path", "set_input_delay", "set_max_area",
                                   "set_max_capacitance", "set_max_delay", "set_max_fanout", "set_max_transition", "set_min_delay",
                                   "set_multicycle_path", "set_operating_conditions", "set_output_delay", "set_timing_derate", "set_units",
                                   "set_wire_load_mode", "reset_path", "unset_path_exceptions"}) {
    interpreter.createCmd(command_name, &ignoreTimingConstraint);
  }
}

}  // namespace

void registerSdcCommands(SdcCommand& interpreter)
{
  registerCollectionCommands(interpreter);
  registerObjectQueryCommands(interpreter);
  registerClockConstraintCommands(interpreter);
  registerSignalConstraintCommands(interpreter);
  registerCommand<TclSetCaseAnalysis>(interpreter, "set_case_analysis");
  registerIgnoredTimingConstraintCommands(interpreter);
}

}  // namespace sdc

}  // namespace ipw


#if 1  // 时钟约束



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

TclCreateClock::TclCreateClock(const char* cmd_name) : SdcTclCmd(cmd_name)
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
  timing_clock.set_period(period);
  timing_clock.set_rise_edge(rise_edge);
  timing_clock.set_fall_edge(fall_edge);
  timing_clock.set_source_list(unique_source_list);
  timing_clock.set_is_propagated(false);
  clock_map[clock_name] = std::move(timing_clock);
  return 1;
}

}  // namespace ipw::sdc




namespace ipw::sdc {

TclCreateGeneratedClock::TclCreateGeneratedClock(const char* cmd_name) : SdcTclCmd(cmd_name)
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
  Database& database = PWDM.getDatabase();
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
  if (getOptionOrArg("-add")->is_set_val() && !getOptionOrArg("-name")->is_set_val()) {
    setTclError("create_generated_clock -add requires -name");
    return 0;
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
  if (getOptionOrArg("-combinational")->is_set_val()
      && (!getOptionOrArg("-divide_by")->is_set_val() || getOptionOrArg("-divide_by")->getDoubleVal() != 1.0)) {
    setTclError("-combinational requires -divide_by 1");
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
  generated.set_master_clock_name(master_name);
  generated.set_master_source(sources.front());
  generated.set_source_list(targets);
  generated.set_is_generated_combinational(getOptionOrArg("-combinational")->is_set_val());
  generated.set_period(period);
  generated.set_rise_edge(rise);
  generated.set_fall_edge(fall);
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

}  // namespace ipw::sdc




namespace ipw::sdc {

TclSetClockTransition::TclSetClockTransition(const char* cmd_name) : SdcTclCmd(cmd_name)
{
  addOption(new ecc::TclSwitchOption("-rise"));
  addOption(new ecc::TclSwitchOption("-fall"));
  addOption(new ecc::TclSwitchOption("-min"));
  addOption(new ecc::TclSwitchOption("-max"));
  addOption(new ecc::TclDoubleOption("transition", 1));
  addOption(new ecc::TclStringOption("clocks", 1));
}

unsigned TclSetClockTransition::exec()
{
  ecc::TclOption* transition_option = getOptionOrArg("transition");
  ecc::TclOption* clock_option = getOptionOrArg("clocks");
  if (!transition_option->is_set_val() || !clock_option->is_set_val()) {
    setTclError("set_clock_transition requires a transition and a clock collection");
    return 0;
  }
  const double transition = transition_option->getDoubleVal();
  if (!std::isfinite(transition) || transition < 0.0) {
    setTclError("set_clock_transition must be finite and non-negative");
    return 0;
  }
  Database& database = PWDM.getDatabase();
  const std::set<std::string> clocks = findClocks(database, parseObjectPatterns(clock_option->getStringVal(), false));
  const bool rise = getOptionOrArg("-rise")->is_set_val();
  const bool fall = getOptionOrArg("-fall")->is_set_val();
  const bool min = getOptionOrArg("-min")->is_set_val();
  const bool max = getOptionOrArg("-max")->is_set_val();
  for (const std::string& name : clocks) {
    TimingClock& clock = database.get_timing_constraint().get_clock_map().at(name);
    for (AnalysisType analysis_type : {AnalysisType::kMin, AnalysisType::kMax}) {
      if ((analysis_type == AnalysisType::kMin && max && !min) || (analysis_type == AnalysisType::kMax && min && !max)) {
        continue;
      }
      if (!fall || rise) {
        clock.get_transition_map()[analysis_type][TransType::kRise] = transition;
      }
      if (!rise || fall) {
        clock.get_transition_map()[analysis_type][TransType::kFall] = transition;
      }
    }
  }
  return 1;
}

}  // namespace ipw::sdc




namespace ipw::sdc {

namespace {

std::set<std::string> findPropagatedClocks(Database& database, const std::vector<std::string>& objects)
{
  std::set<std::string> result;
  for (const std::string& object : objects) {
    const std::vector<std::string> direct_clocks = findObjects(database, {object}, QueryObjectType::kClock);
    if (!direct_clocks.empty()) {
      result.insert(direct_clocks.begin(), direct_clocks.end());
      continue;
    }

    const std::vector<std::string> sources = findClockSources(database, {object});
    for (const std::string& source : sources) {
      for (auto& [clock_name, clock] : database.get_timing_constraint().get_clock_map()) {
        if (std::find(clock.get_source_list().begin(), clock.get_source_list().end(), source) != clock.get_source_list().end()) {
          result.insert(clock_name);
        }
      }
    }
  }
  if (result.empty()) {
    throw std::invalid_argument("set_propagated_clock resolved to empty clock collection");
  }
  return result;
}

}  // namespace

TclSetPropagatedClock::TclSetPropagatedClock(const char* cmd_name) : SdcTclCmd(cmd_name)
{
  addOption(new ecc::TclStringListOption("clocks", 1));
}

unsigned TclSetPropagatedClock::exec()
{
  ecc::TclOption* clock_option = getOptionOrArg("clocks");
  if (!clock_option->is_set_val()) {
    setTclError("set_propagated_clock requires a clock collection");
    return 0;
  }
  const std::vector<std::string> clock_name_list = clock_option->getStringList();
  if (clock_name_list.empty()) {
    setTclError("set_propagated_clock requires at least one clock");
    return 0;
  }

  Database& database = PWDM.getDatabase();
  auto& clock_map = database.get_timing_constraint().get_clock_map();
  std::set<std::string> resolved_clocks;
  try {
    resolved_clocks = findPropagatedClocks(database, clock_name_list);
  } catch (const std::exception& error) {
    setTclError(error.what());
    return 0;
  }
  for (const std::string& clock_name : resolved_clocks) {
    if (clock_map.at(clock_name).get_source_list().empty()) {
      setTclError("set_propagated_clock cannot be applied to virtual clock: " + clock_name);
      return 0;
    }
  }
  for (const std::string& clock_name : resolved_clocks) {
    clock_map.at(clock_name).set_is_propagated(true);
  }
  return 1;
}

}  // namespace ipw::sdc


#endif

#if 1  // IO约束



namespace ipw::sdc {

TclSetDrivingCell::TclSetDrivingCell(const char* cmd_name) : SdcTclCmd(cmd_name)
{
  addOption(new ecc::TclStringOption("-lib_cell", 0));
  addOption(new ecc::TclStringOption("-cell", 0));
  addOption(new ecc::TclStringOption("-library", 0));
  addOption(new ecc::TclStringOption("-pin", 0));
  addOption(new ecc::TclStringOption("-from_pin", 0));
  addOption(new ecc::TclDoubleOption("-input_transition_rise", 0));
  addOption(new ecc::TclDoubleOption("-input_transition_fall", 0));
  addOption(new ecc::TclDoubleOption("-multiply_by", 0));
  addOption(new ecc::TclSwitchOption("-rise"));
  addOption(new ecc::TclSwitchOption("-fall"));
  addOption(new ecc::TclSwitchOption("-min"));
  addOption(new ecc::TclSwitchOption("-max"));
  addOption(new ecc::TclSwitchOption("-no_design_rule"));
  addOption(new ecc::TclSwitchOption("-dont_scale"));
  addOption(new ecc::TclStringListOption("objects", 1));
}

unsigned TclSetDrivingCell::exec()
{
  if (getOptionOrArg("-multiply_by")->is_set_val()) warn("set_driving_cell -multiply_by is accepted for compatibility and ignored");
  if (getOptionOrArg("-dont_scale")->is_set_val()) warn("set_driving_cell -dont_scale is accepted for compatibility and ignored");
  if (getOptionOrArg("-no_design_rule")->is_set_val()) warn("set_driving_cell -no_design_rule is accepted for compatibility and ignored");

  ecc::TclOption* cell_option = getOptionOrArg("-lib_cell");
  ecc::TclOption* cell_alias_option = getOptionOrArg("-cell");
  if (!cell_option->is_set_val() && cell_alias_option->is_set_val()) {
    cell_option = cell_alias_option;
  }
  ecc::TclOption* object_option = getOptionOrArg("objects");
  if (!cell_option->is_set_val() || !object_option->is_set_val()) {
    setTclError("set_driving_cell requires -lib_cell or -cell and an input port collection");
    return 0;
  }

  Database& database = PWDM.getDatabase();
  const std::string cell_name = cell_option->getStringVal();
  std::map<std::string, TimingCell>& cell_map = database.get_timing_library().get_cell_map();
  if (cell_name.empty() || !cell_map.contains(cell_name)) {
    setTclError("set_driving_cell library cell does not exist: " + cell_name);
    return 0;
  }
  TimingCell& timing_cell = cell_map.at(cell_name);

  ecc::TclOption* library_option = getOptionOrArg("-library");
  if (library_option->is_set_val() && timing_cell.get_library_name() != library_option->getStringVal()) {
    setTclError("set_driving_cell cell '" + cell_name + "' is not in library '" + library_option->getStringVal() + "'");
    return 0;
  }

  ecc::TclOption* pin_option = getOptionOrArg("-pin");
  std::string to_pin;
  if (pin_option->is_set_val()) {
    to_pin = pin_option->getStringVal();
    if (!timing_cell.get_port_map().contains(to_pin) || !timing_cell.get_port_map().at(to_pin).get_is_output()) {
      setTclError("set_driving_cell output pin does not exist: " + cell_name + "/" + to_pin);
      return 0;
    }
  } else {
    for (auto& [port_name, port] : timing_cell.get_port_map()) {
      if (port.get_is_output()) {
        if (!to_pin.empty()) {
          setTclError("set_driving_cell requires -pin for a cell with multiple outputs: " + cell_name);
          return 0;
        }
        to_pin = port_name;
      }
    }
    if (to_pin.empty()) {
      setTclError("set_driving_cell cell has no output pin: " + cell_name);
      return 0;
    }
  }

  ecc::TclOption* from_pin_option = getOptionOrArg("-from_pin");
  const std::string requested_from_pin = from_pin_option->is_set_val() ? from_pin_option->getStringVal() : std::string{};
  if (!requested_from_pin.empty()
      && (!timing_cell.get_port_map().contains(requested_from_pin) || !timing_cell.get_port_map().at(requested_from_pin).get_is_input())) {
    setTclError("set_driving_cell input pin does not exist: " + cell_name + "/" + requested_from_pin);
    return 0;
  }
  std::string from_pin;
  for (TimingCellArc& timing_cell_arc : timing_cell.get_cell_arc_list()) {
    if (timing_cell_arc.get_sink_port() != to_pin || (!requested_from_pin.empty() && timing_cell_arc.get_source_port() != requested_from_pin)) {
      continue;
    }
    if (!timing_cell.get_port_map().contains(timing_cell_arc.get_source_port())
        || !timing_cell.get_port_map().at(timing_cell_arc.get_source_port()).get_is_input()) {
      continue;
    }
    from_pin = timing_cell_arc.get_source_port();
    break;
  }
  if (from_pin.empty()) {
    setTclError("set_driving_cell has no timing arc to output pin: " + cell_name + "/" + to_pin);
    return 0;
  }

  ecc::TclOption* rise_transition_option = getOptionOrArg("-input_transition_rise");
  ecc::TclOption* fall_transition_option = getOptionOrArg("-input_transition_fall");
  const double rise_transition = rise_transition_option->is_set_val() ? rise_transition_option->getDoubleVal() : 0.0;
  const double fall_transition = fall_transition_option->is_set_val() ? fall_transition_option->getDoubleVal() : 0.0;
  if (!std::isfinite(rise_transition) || rise_transition < 0.0 || !std::isfinite(fall_transition) || fall_transition < 0.0) {
    setTclError("set_driving_cell input transitions must be finite and non-negative");
    return 0;
  }

  const std::vector<std::string>& object_list = object_option->getStringList();
  const std::vector<std::string> port_list = getPortPinNames(database, object_list);
  if (port_list.empty()) {
    setTclError("set_driving_cell requires at least one input port");
    return 0;
  }
  if (port_list.size() != object_list.size()) {
    setTclError("set_driving_cell contains an invalid port object");
    return 0;
  }
  for (const std::string& port_name : port_list) {
    Pin& pin = database.get_pin_map().at(port_name);
    if (!pin.get_is_port() || (pin.get_direction() != PinDirection::kInput && pin.get_direction() != PinDirection::kInout)) {
      setTclError("set_driving_cell requires input ports; '" + port_name + "' is not an input port");
      return 0;
    }
  }

  TimingDrivingCell driving_cell;
  driving_cell.set_library_name(timing_cell.get_library_name());
  driving_cell.set_cell_name(cell_name);
  driving_cell.set_from_pin(from_pin);
  driving_cell.set_to_pin(to_pin);
  driving_cell.set_input_transition_rise(rise_transition);
  driving_cell.set_input_transition_fall(fall_transition);

  const bool rise = getOptionOrArg("-rise")->is_set_val();
  const bool fall = getOptionOrArg("-fall")->is_set_val();
  const bool min = getOptionOrArg("-min")->is_set_val();
  const bool max = getOptionOrArg("-max")->is_set_val();
  for (const std::string& port_name : port_list) {
    TimingPortConstraint& port_constraint = getOrCreatePortConstraint(database, port_name);
    for (AnalysisType analysis_type : {AnalysisType::kMin, AnalysisType::kMax}) {
      if ((analysis_type == AnalysisType::kMin && max && !min) || (analysis_type == AnalysisType::kMax && min && !max)) {
        continue;
      }
      if (!fall || rise) {
        port_constraint.set_driving_cell(analysis_type, TransType::kRise, driving_cell);
      }
      if (!rise || fall) {
        port_constraint.set_driving_cell(analysis_type, TransType::kFall, driving_cell);
      }
    }
  }
  return 1;
}

}  // namespace ipw::sdc




namespace ipw::sdc {

TclSetInputTransition::TclSetInputTransition(const char* cmd_name) : SdcTclCmd(cmd_name)
{
  addOption(new ecc::TclSwitchOption("-rise"));
  addOption(new ecc::TclSwitchOption("-fall"));
  addOption(new ecc::TclSwitchOption("-min"));
  addOption(new ecc::TclSwitchOption("-max"));
  addOption(new ecc::TclStringOption("-clock", 0));
  addOption(new ecc::TclSwitchOption("-clock_fall"));
  addOption(new ecc::TclDoubleOption("transition", 1));
  addOption(new ecc::TclStringListOption("objects", 1));
}

unsigned TclSetInputTransition::exec()
{
  auto& data_manager = DataManager::getInst();

  if (getOptionOrArg("-clock")->is_set_val()) warn("set_input_transition -clock is deprecated and ignored");
  if (getOptionOrArg("-clock_fall")->is_set_val()) warn("set_input_transition -clock_fall is deprecated and ignored");

  ecc::TclOption* transition_option = getOptionOrArg("transition");
  ecc::TclOption* object_option = getOptionOrArg("objects");
  if (!transition_option->is_set_val() || !object_option->is_set_val()) {
    setTclError("set_input_transition requires a transition and an object collection");
    return 0;
  }

  const double transition_value = transition_option->getDoubleVal();
  if (!std::isfinite(transition_value) || transition_value < 0.0) {
    setTclError("set_input_transition must be finite and non-negative");
    return 0;
  }
  const bool rise = getOptionOrArg("-rise")->is_set_val();
  const bool fall = getOptionOrArg("-fall")->is_set_val();
  const bool min = getOptionOrArg("-min")->is_set_val();
  const bool max = getOptionOrArg("-max")->is_set_val();
  for (const std::string& port_name : getPortPinNames(data_manager.getDatabase(), object_option->getStringList())) {
    TimingPortConstraint& port_constraint = getOrCreatePortConstraint(data_manager.getDatabase(), port_name);
    for (AnalysisType analysis_type : {AnalysisType::kMin, AnalysisType::kMax}) {
      if ((analysis_type == AnalysisType::kMin && max && !min) || (analysis_type == AnalysisType::kMax && min && !max)) {
        continue;
      }
      if (!fall || rise) {
        port_constraint.set_input_transition(analysis_type, TransType::kRise, transition_value);
      }
      if (!rise || fall) {
        port_constraint.set_input_transition(analysis_type, TransType::kFall, transition_value);
      }
    }
  }
  return 1;
}

}  // namespace ipw::sdc




namespace ipw::sdc {

namespace {

struct LoadTargets
{
  std::set<std::string> ports;
  std::set<std::string> nets;
};

std::vector<AnalysisType> getAnalysisTypes(SdcTclCmd& command)
{
  const bool min = command.getOptionOrArg("-min")->is_set_val();
  const bool max = command.getOptionOrArg("-max")->is_set_val();
  std::vector<AnalysisType> result;
  if (!max || min) {
    result.push_back(AnalysisType::kMin);
  }
  if (!min || max) {
    result.push_back(AnalysisType::kMax);
  }
  return result;
}

std::vector<TransType> getTransitionTypes(SdcTclCmd& command)
{
  const bool rise = command.getOptionOrArg("-rise")->is_set_val();
  const bool fall = command.getOptionOrArg("-fall")->is_set_val();
  std::vector<TransType> result;
  if (!fall || rise) {
    result.push_back(TransType::kRise);
  }
  if (!rise || fall) {
    result.push_back(TransType::kFall);
  }
  return result;
}

std::vector<TransType> allTransTypes()
{
  return {TransType::kRise, TransType::kFall};
}

LoadTargets findLoadTargets(Database& database, const std::vector<std::string>& objects)
{
  LoadTargets targets;
  for (const std::string& object : objects) {
    const std::vector<std::string> ports = findObjects(database, {object}, QueryObjectType::kPort);
    const std::vector<std::string> nets = findObjects(database, {object}, QueryObjectType::kNet);
    if (ports.empty() && nets.empty()) {
      throw std::invalid_argument("set_load object is not a port or net: " + object);
    }
    targets.ports.insert(ports.begin(), ports.end());
    targets.nets.insert(nets.begin(), nets.end());
  }
  if (targets.ports.empty() && targets.nets.empty()) {
    throw std::invalid_argument("set_load requires a non-empty port or net collection");
  }
  return targets;
}

double getLibraryPinCapacitance(Database& database, const std::string& pin_name, AnalysisType analysis_type, TransType trans_type)
{
  const auto pin_iter = database.get_pin_map().find(pin_name);
  if (pin_iter == database.get_pin_map().end()) {
    return 0.0;
  }
  if (pin_iter->second.get_is_port()) {
    auto& port_constraints = database.get_timing_constraint().get_port_constraint_map();
    const auto constraint = port_constraints.find(pin_name);
    return constraint != port_constraints.end() && constraint->second.get_has_load() ? constraint->second.get_load(analysis_type, trans_type) : 0.0;
  }
  Pin& pin = pin_iter->second;
  const auto instance_iter = database.get_instance_map().find(pin.get_instance_name());
  if (instance_iter == database.get_instance_map().end()) {
    return 0.0;
  }
  const auto cell_iter = database.get_timing_library().get_cell_map().find(instance_iter->second.get_cell_name());
  if (cell_iter == database.get_timing_library().get_cell_map().end()) {
    return 0.0;
  }
  const auto port_iter = cell_iter->second.get_port_map().find(pin.get_pin_name());
  if (port_iter == cell_iter->second.get_port_map().end()) {
    return 0.0;
  }
  TimingCellPort& cell_port = port_iter->second;
  if (cell_port.get_trans_capacitance_map().contains(analysis_type) && cell_port.get_trans_capacitance_map().at(analysis_type).contains(trans_type)) {
    return cell_port.get_trans_capacitance_map().at(analysis_type).at(trans_type);
  }
  return cell_port.get_capacitance();
}

}  // namespace

double subtractNetLoadPinCapacitance(Database& database, const std::string& net_name, double load_value, AnalysisType analysis_type, TransType trans_type)
{
  const auto net_iter = database.get_net_map().find(net_name);
  if (net_iter == database.get_net_map().end()) {
    return load_value;
  }
  double adjusted = load_value;
  for (const std::string& pin_name : net_iter->second.get_load_pin_list()) {
    adjusted -= getLibraryPinCapacitance(database, pin_name, analysis_type, trans_type);
  }
  return std::max(0.0, adjusted);
}

TclSetLoad::TclSetLoad(const char* cmd_name) : SdcTclCmd(cmd_name)
{
  addOption(new ecc::TclSwitchOption("-pin_load"));
  addOption(new ecc::TclSwitchOption("-wire_load"));
  addOption(new ecc::TclSwitchOption("-subtract_pin_load"));
  addOption(new ecc::TclSwitchOption("-rise"));
  addOption(new ecc::TclSwitchOption("-fall"));
  addOption(new ecc::TclSwitchOption("-min"));
  addOption(new ecc::TclSwitchOption("-max"));
  addOption(new ecc::TclDoubleOption("load", 1));
  addOption(new ecc::TclStringListOption("objects", 1));
}

unsigned TclSetLoad::exec()
{
  auto& data_manager = DataManager::getInst();

  ecc::TclOption* load_option = getOptionOrArg("load");
  ecc::TclOption* object_option = getOptionOrArg("objects");
  if (!load_option->is_set_val() || !object_option->is_set_val()) {
    setTclError("set_load requires a load and an object collection");
    return 0;
  }

  const double load_value = load_option->getDoubleVal();
  if (!std::isfinite(load_value) || load_value < 0.0) {
    setTclError("set_load requires a finite non-negative load");
    return 0;
  }
  Database& database = data_manager.getDatabase();
  LoadTargets targets;
  try {
    targets = findLoadTargets(database, object_option->getStringList());
  } catch (const std::exception& error) {
    setTclError(error.what());
    return 0;
  }
  if (!targets.nets.empty()) {
    if (getOptionOrArg("-rise")->is_set_val() || getOptionOrArg("-fall")->is_set_val()) {
      setTclError("set_load -rise/-fall are not supported for net objects");
      return 0;
    }
    if (getOptionOrArg("-pin_load")->is_set_val() || getOptionOrArg("-wire_load")->is_set_val()) {
      setTclError("set_load -pin_load/-wire_load are not supported for net objects");
      return 0;
    }
  }

  // With both load-kind options present, use the default port pin load. Wire
  // load is selected only when it is the sole load-kind option.
  const bool wire_load = getOptionOrArg("-wire_load")->is_set_val() && !getOptionOrArg("-pin_load")->is_set_val();
  const bool subtract_pin_load = getOptionOrArg("-subtract_pin_load")->is_set_val();
  for (AnalysisType analysis_type : getAnalysisTypes(*this)) {
    for (TransType trans_type : getTransitionTypes(*this)) {
      for (const std::string& port_name : targets.ports) {
        TimingPortConstraint& port_constraint = getOrCreatePortConstraint(database, port_name);
        port_constraint.set_load(analysis_type, trans_type, load_value, wire_load);
      }
    }
    for (TransType trans_type : allTransTypes()) {
      for (const std::string& net_name : targets.nets) {
        const double effective_load
            = subtract_pin_load ? subtractNetLoadPinCapacitance(database, net_name, load_value, analysis_type, trans_type) : load_value;
        database.get_timing_constraint().set_net_load(net_name, analysis_type, trans_type, effective_load);
      }
    }
  }
  return 1;
}

}  // namespace ipw::sdc


#endif

#if 1  // Tcl命令




namespace ipw::sdc {

SdcTclCmd::SdcTclCmd(const char* cmd_name) : ecc::TclCmd(cmd_name)
{
}

int SdcTclCmd::execute(Tcl_Interp* interp, int objc, Tcl_Obj* const objv[])
{
  resetExecutionState();

  bool next_is_option_value = false;
  ecc::TclOption* current_option = nullptr;
  int argument_index = 0;
  for (int index = 1; index < objc; ++index) {
    const char* value = Tcl_GetString(objv[index]);
    if (next_is_option_value) {
      setOptionValue(current_option, value);
      next_is_option_value = false;
      continue;
    }

    ecc::TclOption* option = getOptionOrArg(value);
    if (option != nullptr) {
      current_option = option;
      if (option->isSwitchOption()) {
        option->setVal(nullptr);
      } else {
        next_is_option_value = true;
      }
      continue;
    }

    ecc::TclOption* argument = getArg(argument_index++);
    if (argument == nullptr) {
      setTclError(std::string("unexpected argument '") + value + "'");
      setInterpreterError(interp);
      return TCL_ERROR;
    }
    setOptionValue(argument, value);
  }

  if (next_is_option_value) {
    setTclError(std::string("option '") + current_option->get_option_name() + "' requires a value");
    setInterpreterError(interp);
    return TCL_ERROR;
  }
  if (!check()) {
    setTclError(std::string("invalid ") + get_cmd_name() + " command");
    setInterpreterError(interp);
    return TCL_ERROR;
  }
  if (!exec()) {
    if (_error_message.empty()) {
      setTclError(std::string(get_cmd_name()) + " failed");
    }
    setInterpreterError(interp);
    return TCL_ERROR;
  }
  if (!_error_message.empty()) {
    setInterpreterError(interp);
    return TCL_ERROR;
  }

  if (_has_list_result) {
    Tcl_Obj* list = Tcl_NewListObj(0, nullptr);
    for (const std::string& value : _list_result) {
      Tcl_ListObjAppendElement(interp, list, Tcl_NewStringObj(value.c_str(), static_cast<int>(value.size())));
    }
    Tcl_SetObjResult(interp, list);
  }
  return TCL_OK;
}

void SdcTclCmd::setOptionValue(ecc::TclOption* option, const char* value)
{
  if (option->isDoubleOption()) {
    double number = 0.0;
    if (Tcl_GetDouble(nullptr, value, &number) != TCL_OK || !std::isfinite(number)) {
      throw std::invalid_argument(std::string(option->get_option_name()) + " requires a finite number: " + value);
    }
    std::ostringstream number_stream;
    number_stream << std::setprecision(std::numeric_limits<double>::max_digits10) << number;
    option->setVal(number_stream.str().c_str());
    return;
  }
  if (option->isIntOption()) {
    int number = 0;
    if (Tcl_GetInt(nullptr, value, &number) != TCL_OK) {
      throw std::invalid_argument(std::string(option->get_option_name()) + " requires an integer: " + value);
    }
    const std::string canonical = std::to_string(number);
    option->setVal(canonical.c_str());
    return;
  }
  option->setVal(value);
}

void SdcTclCmd::warn(const std::string& message) const
{
  PWLOG.warn(Loc::current(), message);
}

void SdcTclCmd::setResult(std::vector<std::string> result)
{
  _list_result = std::move(result);
  _has_list_result = true;
}

void SdcTclCmd::resetExecutionState()
{
  resetOptionArgValue();
  _error_message.clear();
  _list_result.clear();
  _has_list_result = false;
}

void SdcTclCmd::setInterpreterError(Tcl_Interp* interp) const
{
  Tcl_SetObjResult(interp, Tcl_NewStringObj(_error_message.c_str(), static_cast<int>(_error_message.size())));
}

}  // namespace ipw::sdc


#endif

#if 1  // 逻辑约束



namespace ipw::sdc {

TclSetCaseAnalysis::TclSetCaseAnalysis(const char* cmd_name) : SdcTclCmd(cmd_name)
{
  addOption(new ecc::TclStringOption("value", 1));
  addOption(new ecc::TclStringListOption("objects", 1));
}

namespace {

std::optional<TimingCaseValue> parseCaseValue(std::string value)
{
  std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) { return std::tolower(character); });
  if (value == "0" || value == "zero") {
    return TimingCaseValue::kZero;
  }
  if (value == "1" || value == "one") {
    return TimingCaseValue::kOne;
  }
  if (value == "static") {
    return TimingCaseValue::kStatic;
  }
  if (value == "rise" || value == "rising") {
    return TimingCaseValue::kRise;
  }
  if (value == "fall" || value == "falling") {
    return TimingCaseValue::kFall;
  }
  return std::nullopt;
}

}  // namespace

unsigned TclSetCaseAnalysis::exec()
{
  auto& data_manager = DataManager::getInst();

  ecc::TclOption* value_option = getOptionOrArg("value");
  ecc::TclOption* object_option = getOptionOrArg("objects");
  if (!value_option->is_set_val() || !object_option->is_set_val()) {
    setTclError("set_case_analysis requires a value and an object collection");
    return 0;
  }

  const std::optional<TimingCaseValue> case_value = parseCaseValue(value_option->getStringVal());
  if (!case_value.has_value()) {
    setTclError("set_case_analysis value must be 0, 1, zero, one, static, rise, or fall");
    return 0;
  }

  auto& case_analysis_map = data_manager.getDatabase().get_timing_constraint().get_case_analysis_map();
  const std::vector<std::string> objects = getPortPinNames(data_manager.getDatabase(), object_option->getStringList());
  if (objects.empty()) {
    setTclError("set_case_analysis requires at least one pin or port object");
    return 0;
  }
  for (const std::string& pin_name : objects) {
    case_analysis_map[pin_name] = *case_value;
  }
  return 1;
}

}  // namespace ipw::sdc


#endif

#if 1  // 对象查询



namespace ipw::sdc {

TclGetCells::TclGetCells(const char* cmd_name) : SdcTclCmd(cmd_name)
{
  addOption(new ecc::TclStringOption("cells", 1));
  addObjectQueryOptions(*this, true, true, true);
}

unsigned TclGetCells::exec()
{
  ecc::TclOption* objects = getOptionOrArg("cells");
  const ObjectQueryOptions options = getObjectQueryOptions(*this);
  if (const auto error = getObjectQueryError(options, objects->is_set_val())) return setTclError("get_cells " + *error), 0;
  const std::string patterns = objects->is_set_val() ? objects->getStringVal() : "*";
  std::vector<std::string> result = findObjects(PWDM.getDatabase(), parseObjectPatterns(patterns, options.regexp), QueryObjectType::kCell, options);
  if (result.empty() && !getOptionOrArg("-quiet")->is_set_val()) {
    warn("no cells matched: " + patterns);
  }
  setResult(std::move(result));
  return 1;
}

}  // namespace ipw::sdc




namespace ipw::sdc {

TclGetClocks::TclGetClocks(const char* cmd_name) : SdcTclCmd(cmd_name)
{
  addOption(new ecc::TclStringOption("clocks", 1));
  addObjectQueryOptions(*this, false, false, false);
}

unsigned TclGetClocks::exec()
{
  ecc::TclOption* objects = getOptionOrArg("clocks");
  const ObjectQueryOptions options = getObjectQueryOptions(*this);
  if (const auto error = getObjectQueryError(options, false)) return setTclError("get_clocks " + *error), 0;
  const std::string patterns = objects->is_set_val() ? objects->getStringVal() : "*";
  std::vector<std::string> result = findObjects(PWDM.getDatabase(), parseObjectPatterns(patterns, options.regexp), QueryObjectType::kClock, options);
  if (result.empty() && !getOptionOrArg("-quiet")->is_set_val()) {
    warn("no clocks matched: " + patterns);
  }
  setResult(std::move(result));
  return 1;
}

TclGetGeneratedClocks::TclGetGeneratedClocks(const char* cmd_name) : TclGetClocks(cmd_name) {}

unsigned TclGetGeneratedClocks::exec()
{
  ecc::TclOption* objects = getOptionOrArg("clocks");
  const ObjectQueryOptions options = getObjectQueryOptions(*this);
  if (const auto error = getObjectQueryError(options, false)) return setTclError("get_generated_clocks " + *error), 0;
  const std::string patterns = objects->is_set_val() ? objects->getStringVal() : "*";
  std::vector<std::string> result = findObjects(PWDM.getDatabase(), parseObjectPatterns(patterns, options.regexp), QueryObjectType::kClock, options);
  std::erase_if(result, [](const std::string& clock_name) {
    return !PWDM.getDatabase().get_timing_constraint().get_clock_map().at(clock_name).get_is_generated();
  });
  if (result.empty() && !getOptionOrArg("-quiet")->is_set_val()) {
    warn("no generated clocks matched: " + patterns);
  }
  setResult(std::move(result));
  return 1;
}

}  // namespace ipw::sdc




namespace ipw::sdc {

TclGetNets::TclGetNets(const char* cmd_name) : SdcTclCmd(cmd_name)
{
  addOption(new ecc::TclStringOption("nets", 1));
  addObjectQueryOptions(*this, true, true, true);
  addOption(new ecc::TclSwitchOption("-top_net_of_hierarchical_group"));
  addOption(new ecc::TclSwitchOption("-segments"));
  addOption(new ecc::TclStringOption("-boundary_type", 0));
}

unsigned TclGetNets::exec()
{
  ecc::TclOption* objects = getOptionOrArg("nets");
  const ObjectQueryOptions options = getObjectQueryOptions(*this);
  if (const auto error = getObjectQueryError(options, objects->is_set_val())) return setTclError("get_nets " + *error), 0;
  for (const char* option : {"-top_net_of_hierarchical_group", "-segments", "-boundary_type"}) {
    if (getOptionOrArg(option)->is_set_val()) {
      return setTclError(std::string("get_nets ") + option + " requires hierarchical net-segment support"), 0;
    }
  }
  const std::string patterns = objects->is_set_val() ? objects->getStringVal() : "*";
  std::vector<std::string> result = findObjects(PWDM.getDatabase(), parseObjectPatterns(patterns, options.regexp), QueryObjectType::kNet, options);
  if (result.empty() && !getOptionOrArg("-quiet")->is_set_val()) {
    warn("no nets matched: " + patterns);
  }
  setResult(std::move(result));
  return 1;
}

}  // namespace ipw::sdc




namespace ipw::sdc {

TclAllClocks::TclAllClocks(const char* cmd_name) : SdcTclCmd(cmd_name)
{
}

unsigned TclAllClocks::exec()
{
  // all_clocks uses the same query path as get_clocks so ordering and object
  // matching stay consistent.
  setResult(findObjects(PWDM.getDatabase(), {"*"}, QueryObjectType::kClock));
  return 1;
}

TclGetObjectName::TclGetObjectName(const char* cmd_name) : SdcTclCmd(cmd_name)
{
  addOption(new ecc::TclStringOption("object", 1));
}

unsigned TclGetObjectName::exec()
{
  ecc::TclOption* object = getOptionOrArg("object");
  if (!object->is_set_val()) {
    setTclError(std::string(get_cmd_name()) + " requires an object");
    return 0;
  }

  try {
    setResult(getObjectNames(PWDM.getDatabase(), object->getStringVal()));
  } catch (const std::exception& error) {
    setTclError(error.what());
    return 0;
  }
  return 1;
}

}  // namespace ipw::sdc




namespace ipw::sdc {

TclGetPins::TclGetPins(const char* cmd_name) : SdcTclCmd(cmd_name)
{
  addOption(new ecc::TclStringOption("pins", 1));
  addObjectQueryOptions(*this, true, true, true);
  addOption(new ecc::TclSwitchOption("-leaf"));
}

unsigned TclGetPins::exec()
{
  ecc::TclOption* objects = getOptionOrArg("pins");
  const ObjectQueryOptions options = getObjectQueryOptions(*this);
  if (const auto error = getObjectQueryError(options, objects->is_set_val())) return setTclError("get_pins " + *error), 0;
  const std::string patterns = objects->is_set_val() ? objects->getStringVal() : "*";
  std::vector<std::string> result = findObjects(PWDM.getDatabase(), parseObjectPatterns(patterns, options.regexp), QueryObjectType::kPin, options);
  if (getOptionOrArg("-leaf")->is_set_val()) {
    const auto& pins = PWDM.getDatabase().get_pin_map();
    std::erase_if(result, [&](const std::string& name) { return !pins.contains(name) || pins.at(name).get_is_port(); });
  }
  if (result.empty() && !getOptionOrArg("-quiet")->is_set_val()) {
    warn("no pins matched: " + patterns);
  }
  setResult(std::move(result));
  return 1;
}

}  // namespace ipw::sdc




namespace ipw::sdc {

TclGetPorts::TclGetPorts(const char* cmd_name) : SdcTclCmd(cmd_name)
{
  addOption(new ecc::TclStringOption("ports", 1));
  addObjectQueryOptions(*this, false, true, true);
}

unsigned TclGetPorts::exec()
{
  ecc::TclOption* objects = getOptionOrArg("ports");
  const ObjectQueryOptions options = getObjectQueryOptions(*this);
  if (const auto error = getObjectQueryError(options, objects->is_set_val())) return setTclError("get_ports " + *error), 0;
  const std::string patterns = objects->is_set_val() ? objects->getStringVal() : "*";
  std::vector<std::string> result = findObjects(PWDM.getDatabase(), parseObjectPatterns(patterns, options.regexp), QueryObjectType::kPort, options);
  if (result.empty() && !getOptionOrArg("-quiet")->is_set_val()) {
    warn("no ports matched: " + patterns);
  }
  setResult(std::move(result));
  return 1;
}

TclCurrentDesign::TclCurrentDesign(const char* cmd_name) : SdcTclCmd(cmd_name)
{
  addOption(new ecc::TclStringOption("design", 1));
}

unsigned TclCurrentDesign::exec()
{
  const std::string& design_name = PWDM.getDatabase().get_design_name();
  ecc::TclOption* design = getOptionOrArg("design");
  if (design->is_set_val() && (design_name.empty() || design_name != design->getStringVal())) {
    setTclError("current_design can only select the loaded design '" + design_name + "'");
    return 0;
  }
  setResult(design_name.empty() ? std::vector<std::string>{} : std::vector<std::string>{design_name});
  return 1;
}

TclRemoveFromCollection::TclRemoveFromCollection(const char* cmd_name) : SdcTclCmd(cmd_name)
{
  addOption(new ecc::TclSwitchOption("-intersect"));
  addOption(new ecc::TclStringOption("collection", 1));
  addOption(new ecc::TclStringOption("objects", 1));
}

unsigned TclRemoveFromCollection::exec()
{
  ecc::TclOption* collection_option = getOptionOrArg("collection");
  ecc::TclOption* object_option = getOptionOrArg("objects");
  if (!collection_option->is_set_val() || !object_option->is_set_val()) {
    setTclError("remove_from_collection requires a base collection and an object specification");
    return 0;
  }

  Database& database = PWDM.getDatabase();
  const std::vector<std::string> collection = parseObjectPatterns(collection_option->getStringVal(), false);
  const std::vector<std::string> objects = parseObjectPatterns(object_option->getStringVal(), false);
  std::optional<QueryObjectType> object_type;
  bool is_homogeneous = true;
  for (const std::string& name : collection) {
    QueryObjectType current_type = QueryObjectType::kAny;
    const auto pin = database.get_pin_map().find(name);
    if (pin != database.get_pin_map().end()) {
      current_type = pin->second.get_is_port() ? QueryObjectType::kPort : QueryObjectType::kPin;
    } else if (database.get_timing_constraint().get_clock_map().contains(name)) {
      current_type = QueryObjectType::kClock;
    } else if (name != database.get_design_name() && !database.get_instance_map().contains(name)) {
      setTclError("invalid collection object '" + name + "'");
      return 0;
    }
    is_homogeneous = is_homogeneous && (!object_type || *object_type == current_type);
    object_type = current_type;
  }

  std::set<std::string> selected(objects.begin(), objects.end());
  if (is_homogeneous && object_type && *object_type != QueryObjectType::kAny) {
    const std::vector<std::string> matches = findObjects(database, objects, *object_type);
    selected.insert(matches.begin(), matches.end());
  }
  const bool intersect = getOptionOrArg("-intersect")->is_set_val();
  std::vector<std::string> result;
  for (const std::string& name : collection) {
    if (selected.contains(name) == intersect) {
      result.push_back(name);
    }
  }
  setResult(std::move(result));
  return 1;
}

TclAllInputs::TclAllInputs(const char* cmd_name) : SdcTclCmd(cmd_name)
{
  addOption(new ecc::TclSwitchOption("-no_clocks"));
}

unsigned TclAllInputs::exec()
{
  Database& database = PWDM.getDatabase();
  std::set<std::string> clock_source_set;
  if (getOptionOrArg("-no_clocks")->is_set_val()) {
    for (std::pair<const std::string, TimingClock>& clock_pair : database.get_timing_constraint().get_clock_map()) {
      clock_source_set.insert(clock_pair.second.get_source_list().begin(), clock_pair.second.get_source_list().end());
    }
  }

  std::vector<std::string> port_name_list;
  for (std::pair<const std::string, Pin>& pin_pair : database.get_pin_map()) {
    Pin& pin = pin_pair.second;
    if (pin.get_is_port() && !clock_source_set.contains(pin_pair.first)
        && (pin.get_direction() == PinDirection::kInput || pin.get_direction() == PinDirection::kInout)) {
      port_name_list.push_back(pin_pair.first);
    }
  }
  std::sort(port_name_list.begin(), port_name_list.end());
  setResult(std::move(port_name_list));
  return 1;
}

TclAllOutputs::TclAllOutputs(const char* cmd_name) : SdcTclCmd(cmd_name)
{
}

unsigned TclAllOutputs::exec()
{
  std::vector<std::string> port_name_list;
  for (std::pair<const std::string, Pin>& pin_pair : PWDM.getDatabase().get_pin_map()) {
    Pin& pin = pin_pair.second;
    if (pin.get_is_port() && (pin.get_direction() == PinDirection::kOutput || pin.get_direction() == PinDirection::kInout)) {
      port_name_list.push_back(pin_pair.first);
    }
  }
  std::sort(port_name_list.begin(), port_name_list.end());
  setResult(std::move(port_name_list));
  return 1;
}

}  // namespace ipw::sdc






namespace ipw::sdc {

namespace {

std::string trimFilterText(std::string text)
{
  const std::size_t first = text.find_first_not_of(" \t\r\n");
  if (first == std::string::npos) return {};
  const std::size_t last = text.find_last_not_of(" \t\r\n");
  text = text.substr(first, last - first + 1);
  while (text.size() >= 2 && ((text.front() == '{' && text.back() == '}') || (text.front() == '(' && text.back() == ')'))) {
    int depth = 0;
    bool encloses_all = true;
    for (std::size_t index = 0; index < text.size(); ++index) {
      if (text[index] == text.front()) ++depth;
      if (text[index] == text.back()) --depth;
      if (depth == 0 && index + 1 < text.size()) {
        encloses_all = false;
        break;
      }
    }
    if (!encloses_all) break;
    text = trimFilterText(text.substr(1, text.size() - 2));
  }
  if (text.size() >= 2 && ((text.front() == '"' && text.back() == '"') || (text.front() == '\'' && text.back() == '\''))) {
    text = text.substr(1, text.size() - 2);
  }
  return text;
}

std::size_t findFilterOperator(const std::string& expression, std::string_view operation)
{
  int parentheses = 0;
  int braces = 0;
  char quote = '\0';
  for (std::size_t index = 0; index + operation.size() <= expression.size(); ++index) {
    const char value = expression[index];
    if (quote != '\0') {
      if (value == quote && (index == 0 || expression[index - 1] != '\\')) quote = '\0';
      continue;
    }
    if (value == '"' || value == '\'') {
      quote = value;
      continue;
    }
    if (value == '(') ++parentheses;
    if (value == ')') --parentheses;
    if (value == '{') ++braces;
    if (value == '}') --braces;
    if (parentheses == 0 && braces == 0 && expression.compare(index, operation.size(), operation) == 0) return index;
  }
  return std::string::npos;
}

bool parseFilterNumber(const std::string& text, double& value)
{
  char* end = nullptr;
  value = std::strtod(text.c_str(), &end);
  return end != text.c_str() && *end == '\0' && std::isfinite(value);
}

bool matchesFilter(const std::string& raw_expression, const std::map<std::string, std::string>& attributes, bool regexp, bool nocase)
{
  const std::string expression = trimFilterText(raw_expression);
  if (expression.empty()) return true;
  for (std::string_view operation : {std::string_view("||"), std::string_view("&&")}) {
    const std::size_t position = findFilterOperator(expression, operation);
    if (position != std::string::npos) {
      const bool left = matchesFilter(expression.substr(0, position), attributes, regexp, nocase);
      const bool right = matchesFilter(expression.substr(position + operation.size()), attributes, regexp, nocase);
      return operation == "||" ? left || right : left && right;
    }
  }
  if (expression.front() == '!') return !matchesFilter(expression.substr(1), attributes, regexp, nocase);

  for (std::string_view operation : {std::string_view("!~"), std::string_view("=~"), std::string_view("!="), std::string_view("=="),
                                     std::string_view(">="), std::string_view("<="), std::string_view(">"), std::string_view("<")}) {
    const std::size_t position = findFilterOperator(expression, operation);
    if (position == std::string::npos) continue;
    const std::string attribute = trimFilterText(expression.substr(0, position));
    const std::string expected = trimFilterText(expression.substr(position + operation.size()));
    const auto actual_iter = attributes.find(attribute);
    if (actual_iter == attributes.end()) throw std::invalid_argument("unknown filter attribute: " + attribute);
    const std::string& actual = actual_iter->second;
    if (operation == "=~" || operation == "!~") {
      bool match = false;
      if (regexp) {
        const std::string pattern = std::string(nocase ? "(?i)" : "") + "^(?:" + expected + ")$";
        if (Tcl_RegExpMatch(SdcCommand::getInst().getInterp(), "", pattern.c_str()) < 0) {
          throw std::invalid_argument("invalid filter regular expression: " + expected);
        }
        match = Tcl_RegExpMatch(SdcCommand::getInst().getInterp(), actual.c_str(), pattern.c_str()) == 1;
      } else if (nocase) {
        std::string folded_actual = actual;
        std::string folded_expected = expected;
        std::transform(folded_actual.begin(), folded_actual.end(), folded_actual.begin(), [](unsigned char value) { return std::tolower(value); });
        std::transform(folded_expected.begin(), folded_expected.end(), folded_expected.begin(), [](unsigned char value) { return std::tolower(value); });
        match = Tcl_StringMatch(folded_actual.c_str(), folded_expected.c_str()) != 0;
      } else {
        match = Tcl_StringMatch(actual.c_str(), expected.c_str()) != 0;
      }
      return operation == "=~" ? match : !match;
    }
    if (operation == "==" || operation == "!=") {
      const bool match = actual == expected;
      return operation == "==" ? match : !match;
    }
    double left = 0.0;
    double right = 0.0;
    if (!parseFilterNumber(actual, left) || !parseFilterNumber(expected, right)) {
      throw std::invalid_argument("numeric filter comparison requires numbers: " + expression);
    }
    if (operation == ">=") return left >= right;
    if (operation == "<=") return left <= right;
    if (operation == ">") return left > right;
    return left < right;
  }
  const auto attribute = attributes.find(expression);
  return attribute != attributes.end() && attribute->second != "false" && attribute->second != "0" && !attribute->second.empty();
}

struct ObjectMatch
{
  std::string object_name;
  std::vector<std::string> names;
  std::map<std::string, std::string> attributes;
};

std::string directionName(PinDirection direction)
{
  switch (direction) {
    case PinDirection::kInput:
      return "in";
    case PinDirection::kOutput:
      return "out";
    case PinDirection::kInout:
      return "inout";
    default:
      return "internal";
  }
}

std::string leafName(std::string name)
{
  const std::size_t separator = name.find_last_of("/:");
  return separator == std::string::npos ? name : name.substr(separator + 1);
}

void addObjectMatch(std::vector<ObjectMatch>& matches, std::string object_name, std::string display, std::string object_class,
                           std::map<std::string, std::string> attributes = {})
{
  ObjectMatch match;
  match.object_name = std::move(object_name);
  match.names = {match.object_name};
  if (display != match.object_name) match.names.push_back(display);
  match.names.push_back(leafName(display));
  attributes["name"] = leafName(display);
  attributes["full_name"] = display;
  attributes["object_class"] = std::move(object_class);
  match.attributes = std::move(attributes);
  matches.push_back(std::move(match));
}

bool hasUnescapedBracket(const std::string& pattern)
{
  for (std::size_t index = 0; index < pattern.size(); ++index) {
    if ((pattern[index] == '[' || pattern[index] == ']') && (index == 0 || pattern[index - 1] != '\\')) return true;
  }
  return false;
}

std::string escapeGlobBrackets(const std::string& pattern)
{
  std::string escaped;
  escaped.reserve(pattern.size() + 4);
  for (std::size_t index = 0; index < pattern.size(); ++index) {
    if ((pattern[index] == '[' || pattern[index] == ']') && (index == 0 || pattern[index - 1] != '\\')) escaped.push_back('\\');
    escaped.push_back(pattern[index]);
  }
  return escaped;
}

std::vector<ObjectMatch> collectObjectMatches(Database& database, QueryObjectType type)
{
  std::vector<ObjectMatch> matches;
  if (type == QueryObjectType::kClock || type == QueryObjectType::kAny) {
    for (auto& [name, clock] : database.get_timing_constraint().get_clock_map()) {
      addObjectMatch(matches, name, name, "clock", {{"period", std::to_string(clock.get_period())},
                                                     {"is_generated", clock.get_is_generated() ? "true" : "false"},
                                                     {"is_propagated", clock.get_is_propagated() ? "true" : "false"}});
    }
  }
  if (type == QueryObjectType::kPort || type == QueryObjectType::kPin || type == QueryObjectType::kAny) {
    for (auto& [name, pin] : database.get_pin_map()) {
      if ((type == QueryObjectType::kPort && !pin.get_is_port()) || (type == QueryObjectType::kPin && pin.get_is_port())) continue;
      const std::string display = pin.get_is_port() ? name : pin.get_instance_name() + "/" + pin.get_pin_name();
      std::map<std::string, std::string> attributes{{"direction", directionName(pin.get_direction())},
                                                    {"is_port", pin.get_is_port() ? "true" : "false"}};
      if (!pin.get_is_port()) {
        attributes["pin_name"] = pin.get_pin_name();
        const auto instance = database.get_instance_map().find(pin.get_instance_name());
        if (instance != database.get_instance_map().end()) attributes["ref_name"] = instance->second.get_cell_name();
      }
      addObjectMatch(matches, name, display, pin.get_is_port() ? "port" : "pin", std::move(attributes));
    }
  }
  if (type == QueryObjectType::kCell || type == QueryObjectType::kAny) {
    for (auto& [name, instance] : database.get_instance_map()) {
      addObjectMatch(matches, name, name, "cell", {{"ref_name", instance.get_cell_name()},
                                                   {"is_sequential", instance.get_is_sequential() ? "true" : "false"},
                                                   {"is_clock_gating_cell", instance.get_is_clock_gating() ? "true" : "false"}});
    }
  }
  if (type == QueryObjectType::kNet || type == QueryObjectType::kAny) {
    for (auto& [name, net] : database.get_net_map()) {
      addObjectMatch(matches, name, name, "net", {{"fanout", std::to_string(net.get_load_pin_list().size())}});
    }
  }
  return matches;
}

QueryObjectType getObjectType(Database& database, const std::string& name)
{
  const auto pin = database.get_pin_map().find(name);
  if (pin != database.get_pin_map().end()) return pin->second.get_is_port() ? QueryObjectType::kPort : QueryObjectType::kPin;
  if (database.get_timing_constraint().get_clock_map().contains(name)) return QueryObjectType::kClock;
  if (database.get_instance_map().contains(name)) return QueryObjectType::kCell;
  if (database.get_net_map().contains(name)) return QueryObjectType::kNet;
  return QueryObjectType::kAny;
}

std::set<std::string> collectRelatedObjects(Database& database, const std::vector<std::string>& objects, QueryObjectType target)
{
  std::set<std::string> result;
  for (const std::string& object_pattern : objects) {
    const std::vector<std::string> object_names = findObjects(database, {object_pattern}, QueryObjectType::kAny);
    for (const std::string& object_name : object_names) {
      const QueryObjectType source = getObjectType(database, object_name);
      if (source == target) result.insert(object_name);
      if (source == QueryObjectType::kPin || source == QueryObjectType::kPort) {
        Pin& pin = database.get_pin_map().at(object_name);
        if (target == QueryObjectType::kCell && !pin.get_instance_name().empty()) result.insert(pin.get_instance_name());
        if (target == QueryObjectType::kNet && !pin.get_net_name().empty()) result.insert(pin.get_net_name());
      }
      if (source == QueryObjectType::kCell) {
        for (const std::string& pin_name : database.get_instance_map().at(object_name).get_pin_name_list()) {
          if (target == QueryObjectType::kPin) result.insert(pin_name);
          if (target == QueryObjectType::kNet && database.get_pin_map().contains(pin_name)
              && !database.get_pin_map().at(pin_name).get_net_name().empty()) {
            result.insert(database.get_pin_map().at(pin_name).get_net_name());
          }
        }
      }
      if (source == QueryObjectType::kNet) {
        Net& net = database.get_net_map().at(object_name);
        for (const std::string& pin_name : net.get_pin_name_list()) {
          if (target == QueryObjectType::kPin && database.get_pin_map().contains(pin_name) && !database.get_pin_map().at(pin_name).get_is_port()) {
            result.insert(pin_name);
          }
          if (target == QueryObjectType::kPort && database.get_pin_map().contains(pin_name) && database.get_pin_map().at(pin_name).get_is_port()) {
            result.insert(pin_name);
          }
          if (target == QueryObjectType::kCell && database.get_pin_map().contains(pin_name)
              && !database.get_pin_map().at(pin_name).get_instance_name().empty()) {
            result.insert(database.get_pin_map().at(pin_name).get_instance_name());
          }
        }
      }
      if (target == QueryObjectType::kClock && (source == QueryObjectType::kPin || source == QueryObjectType::kPort)) {
        for (auto& [clock_name, clock] : database.get_timing_constraint().get_clock_map()) {
          if (std::find(clock.get_source_list().begin(), clock.get_source_list().end(), object_name) != clock.get_source_list().end()) result.insert(clock_name);
        }
      }
    }
  }
  return result;
}

bool matchObjectName(std::string actual, std::string pattern, const ObjectQueryOptions& options, bool escape_brackets)
{
  if (options.nocase) {
    std::transform(actual.begin(), actual.end(), actual.begin(), [](unsigned char value) { return std::tolower(value); });
    std::transform(pattern.begin(), pattern.end(), pattern.begin(), [](unsigned char value) { return std::tolower(value); });
  }
  if (options.exact) return actual == pattern;
  if (options.regexp) {
    const std::string expression = "^(?:" + pattern + ")$";
    if (Tcl_RegExpMatch(SdcCommand::getInst().getInterp(), "", expression.c_str()) < 0) throw std::invalid_argument("invalid regular expression: " + pattern);
    return Tcl_RegExpMatch(SdcCommand::getInst().getInterp(), actual.c_str(), expression.c_str()) == 1;
  }
  if (escape_brackets) pattern = escapeGlobBrackets(pattern);
  return Tcl_StringMatch(actual.c_str(), pattern.c_str()) != 0;
}

}  // namespace

std::vector<std::string> parseObjectPatterns(const std::string& text, bool regexp)
{
  // A regular expression may be passed as one raw Tcl word; otherwise parse a Tcl list.
  if (regexp && !text.empty() && text.front() != '{' && text.find('\\') != std::string::npos) {
    return {text};
  }
  int count = 0;
  const char** items = nullptr;
  if (Tcl_SplitList(SdcCommand::getInst().getInterp(), text.c_str(), &count, &items) != TCL_OK) {
    throw std::invalid_argument("invalid object pattern list");
  }
  std::vector<std::string> result(items, items + count);
  Tcl_Free(reinterpret_cast<char*>(items));
  return result;
}

std::vector<std::string> findObjects(Database& database, const std::vector<std::string>& patterns, QueryObjectType type, bool regexp)
{
  ObjectQueryOptions options;
  options.regexp = regexp;
  return findObjects(database, patterns, type, options);
}

std::vector<std::string> findObjects(Database& database, const std::vector<std::string>& patterns, QueryObjectType type, const ObjectQueryOptions& options)
{
  if (options.regexp && options.exact) throw std::invalid_argument("-regexp and -exact are mutually exclusive");
  const std::set<std::string> of_objects = options.of_objects.empty() ? std::set<std::string>{} : collectRelatedObjects(database, options.of_objects, type);
  const std::vector<ObjectMatch> object_matches = collectObjectMatches(database, type);
  std::set<std::string> found;

  auto isEligible = [&](const ObjectMatch& candidate) {
    return (options.of_objects.empty() || of_objects.contains(candidate.object_name))
           && matchesFilter(options.filter, candidate.attributes, options.regexp, options.nocase);
  };
  auto matches = [&](const ObjectMatch& candidate, const std::string& pattern, bool escape_brackets) {
    for (std::size_t index = 0; index < candidate.names.size(); ++index) {
      if (index == 2 && !options.hierarchical) continue;
      if (matchObjectName(candidate.names[index], pattern, options, escape_brackets)) return true;
    }
    return false;
  };

  for (const std::string& pattern : patterns) {
    bool pattern_matched = false;
    for (const ObjectMatch& candidate : object_matches) {
      if (isEligible(candidate) && matches(candidate, pattern, false)) {
        found.insert(candidate.object_name);
        pattern_matched = true;
      }
    }

    // Retry a non-regexp query with literal brackets when the normal glob pass
    // found nothing. This is required for names such as bus[0].
    if (!pattern_matched && !options.regexp && !options.exact && hasUnescapedBracket(pattern)) {
      for (const ObjectMatch& candidate : object_matches) {
        if (isEligible(candidate) && matches(candidate, pattern, true)) {
          found.insert(candidate.object_name);
        }
      }
    }
  }
  return {found.begin(), found.end()};
}

std::vector<std::string> findClockSources(Database& database, const std::vector<std::string>& objects)
{
  std::set<std::string> sources;
  for (const std::string& object : objects) {
    for (const std::string& match : findObjects(database, {object}, QueryObjectType::kAny)) {
      const QueryObjectType type = getObjectType(database, match);
      if (type == QueryObjectType::kPort || type == QueryObjectType::kPin) {
        sources.insert(match);
      } else if (type == QueryObjectType::kNet) {
        Net& net = database.get_net_map().at(match);
        sources.insert(net.get_driver_pin_list().begin(), net.get_driver_pin_list().end());
      } else {
        throw std::invalid_argument("clock sources must be ports, pins, or nets: " + object);
      }
    }
  }
  return {sources.begin(), sources.end()};
}

std::set<std::string> findClocks(Database& database, const std::vector<std::string>& objects)
{
  std::set<std::string> clocks;
  for (const std::string& pattern : objects) {
    const std::vector<std::string> matches = findObjects(database, {pattern}, QueryObjectType::kClock);
    if (matches.empty()) {
      throw std::invalid_argument("clock '" + pattern + "' does not exist");
    }
    clocks.insert(matches.begin(), matches.end());
  }
  if (clocks.empty()) {
    throw std::invalid_argument("a non-empty clock collection is required");
  }
  return clocks;
}

std::vector<std::string> getPortPinNames(Database& database, const std::vector<std::string>& object_list)
{
  std::vector<std::string> pin_names;
  for (const std::string& object_name : object_list) {
    std::string pin_name = object_name;
    if (!pin_name.empty() && pin_name.front() == '\\') {
      pin_name.erase(pin_name.begin());
    }
    if (pin_name.rfind("[get_ports", 0) == 0) {
      pin_name = pin_name.substr(10);
    }
    if (pin_name.rfind("[get_pins", 0) == 0) {
      pin_name = pin_name.substr(9);
      std::replace(pin_name.begin(), pin_name.end(), '/', ':');
    }
    if (database.get_pin_map().count(pin_name) > 0) {
      pin_names.push_back(pin_name);
      continue;
    }
    if (!pin_name.empty() && pin_name.back() == ']') {
      std::string trimmed_pin_name = pin_name;
      trimmed_pin_name.pop_back();
      if (database.get_pin_map().count(trimmed_pin_name) > 0) {
        pin_names.push_back(trimmed_pin_name);
        continue;
      }
    }
    std::replace(pin_name.begin(), pin_name.end(), '/', ':');
    if (database.get_pin_map().count(pin_name) > 0) {
      pin_names.push_back(pin_name);
    }
  }
  return pin_names;
}

std::vector<std::string> getObjectNames(Database& database, const std::string& object_list)
{
  const std::vector<std::string> patterns = parseObjectPatterns(object_list, false);
  if (patterns.empty()) {
    // An empty collection is a valid result of get_ports/get_pins and must
    // remain an empty collection when passed to get_object_name.
    return {};
  }

  const std::vector<ObjectMatch> object_matches = collectObjectMatches(database, QueryObjectType::kAny);
  std::map<std::string, std::string> full_names;
  for (const ObjectMatch& match : object_matches) {
    full_names.emplace(match.object_name, match.attributes.at("full_name"));
  }

  std::vector<std::string> result;
  std::set<std::string> emitted;
  for (const std::string& pattern : patterns) {
    const std::vector<std::string> matches = findObjects(database, {pattern}, QueryObjectType::kAny);
    if (matches.empty()) {
      throw std::invalid_argument("object '" + pattern + "' does not exist");
    }
    for (const std::string& match : matches) {
      const auto full_name = full_names.find(match);
      if (full_name == full_names.end()) {
        throw std::invalid_argument("object '" + match + "' has no full name");
      }
      if (emitted.insert(full_name->second).second) {
        result.push_back(full_name->second);
      }
    }
  }
  return result;
}

TimingPortConstraint& getOrCreatePortConstraint(Database& database, const std::string& port_name)
{
  return database.get_timing_constraint().get_port_constraint_map()[port_name];
}

void addObjectQueryOptions(SdcTclCmd& command, bool hierarchical, bool exact, bool of_objects)
{
  command.addOption(new ecc::TclSwitchOption("-quiet"));
  command.addOption(new ecc::TclSwitchOption("-regexp"));
  command.addOption(new ecc::TclSwitchOption("-nocase"));
  command.addOption(new ecc::TclStringOption("-filter", 0));
  if (hierarchical) command.addOption(new ecc::TclSwitchOption("-hierarchical"));
  if (exact) command.addOption(new ecc::TclSwitchOption("-exact"));
  if (of_objects) command.addOption(new ecc::TclStringListOption("-of_objects", 0));
}

ObjectQueryOptions getObjectQueryOptions(SdcTclCmd& command)
{
  ObjectQueryOptions options;
  for (const auto& [name, target] : std::initializer_list<std::pair<const char*, bool*>>{{"-regexp", &options.regexp},
                                                                                        {"-nocase", &options.nocase},
                                                                                        {"-exact", &options.exact},
                                                                                        {"-hierarchical", &options.hierarchical}}) {
    if (ecc::TclOption* option = command.getOptionOrArg(name); option != nullptr) *target = option->is_set_val();
  }
  if (ecc::TclOption* filter = command.getOptionOrArg("-filter"); filter != nullptr && filter->is_set_val()) options.filter = filter->getStringVal();
  if (ecc::TclOption* objects = command.getOptionOrArg("-of_objects"); objects != nullptr && objects->is_set_val()) {
    options.of_objects = objects->getStringList();
  }
  return options;
}

std::optional<std::string> getObjectQueryError(const ObjectQueryOptions& options, bool has_patterns)
{
  if (options.regexp && options.exact) return "-regexp and -exact are mutually exclusive";
  if (options.nocase && !options.regexp) return "-nocase requires -regexp";
  if (has_patterns && !options.of_objects.empty()) return "patterns and -of_objects are mutually exclusive";
  if (options.hierarchical && !options.of_objects.empty()) return "-hierarchical and -of_objects are mutually exclusive";
  return std::nullopt;
}

}  // namespace ipw::sdc
#endif
