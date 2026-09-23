// ***************************************************************************************
// Copyright (c) 2023-2025 Peng Cheng Laboratory
// Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences
// Copyright (c) 2023-2025 Beijing Institute of Open Source Chip
//
// iEDA is licensed under Mulan PSL v2.
// You can use this software according to the terms and conditions of the Mulan PSL v2.
// You may obtain a copy of the License at:
// http://license.coscl.org.cn/MulanPSL2
//
// THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
// EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
// MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
//
// See the Mulan PSL v2 for more details.
// ***************************************************************************************
#include "SdcCommand.hpp"
#include "clock/ClockCommands.hpp"
#include "delay/DelayCommands.hpp"
#include "design_rule/DesignRuleCommands.hpp"
#include "exception/PathExceptionCommand.hpp"
#include "logic/LogicCommands.hpp"
#include "frontend/SdcTclCmd.hpp"
#include "object/ObjectCommands.hpp"

namespace ipw::sdc {

namespace {

template <typename Command>
void registerCommand(SdcCommand& interpreter, const char* name)
{
  interpreter.createCmd(name, &executeTclCommand<Command>);
}

void registerCollectionCommands(SdcCommand& interpreter)
{
  // Collection operations are tool commands exposed through Tcl; they are not
  // part of the Tcl language itself.
  registerCommand<TclRemoveFromCollection>(interpreter, "remove_from_collection");
}

void registerObjectQueryCommands(SdcCommand& interpreter)
{
  registerCommand<TclCurrentDesign>(interpreter, "current_design");
  registerCommand<TclAllInputs>(interpreter, "all_inputs");
  registerCommand<TclAllOutputs>(interpreter, "all_outputs");
  registerCommand<TclAllClocks>(interpreter, "all_clocks");
  registerCommand<TclGetFullName>(interpreter, "get_full_name");
  registerCommand<TclGetFullName>(interpreter, "get_object_name");
  registerCommand<TclGetClocks>(interpreter, "get_clock");
  registerCommand<TclGetClocks>(interpreter, "get_clocks");
  registerCommand<TclGetGeneratedClocks>(interpreter, "get_generated_clocks");
  registerCommand<TclGetLibs>(interpreter, "get_lib");
  registerCommand<TclGetLibs>(interpreter, "get_libs");
  registerCommand<TclGetLibCells>(interpreter, "get_lib_cell");
  registerCommand<TclGetLibCells>(interpreter, "get_lib_cells");
  registerCommand<TclGetLibPins>(interpreter, "get_lib_pin");
  registerCommand<TclGetLibPins>(interpreter, "get_lib_pins");
  registerCommand<TclGetPorts>(interpreter, "get_port");
  registerCommand<TclGetPorts>(interpreter, "get_ports");
  registerCommand<TclGetCells>(interpreter, "get_cell");
  registerCommand<TclGetCells>(interpreter, "get_cells");
  registerCommand<TclGetNets>(interpreter, "get_net");
  registerCommand<TclGetNets>(interpreter, "get_nets");
  registerCommand<TclGetPins>(interpreter, "get_pins");
}

void registerClockConstraintCommands(SdcCommand& interpreter)
{
  registerCommand<TclCreateClock>(interpreter, "create_clock");
  registerCommand<TclCreateGeneratedClock>(interpreter, "create_generated_clock");
  registerCommand<TclSetClockGroups>(interpreter, "set_clock_groups");
  registerCommand<TclSetClockTransition>(interpreter, "set_clock_transition");
  registerCommand<TclSetClockUncertainty>(interpreter, "set_clock_uncertainty");
  registerCommand<TclSetPropagatedClock>(interpreter, "set_propagated_clock");
}

void registerDelayConstraintCommands(SdcCommand& interpreter)
{
  registerCommand<TclSetDrivingCell>(interpreter, "set_driving_cell");
  registerCommand<TclSetInputDelay>(interpreter, "set_input_delay");
  registerCommand<TclSetOutputDelay>(interpreter, "set_output_delay");
  registerCommand<TclSetInputTransition>(interpreter, "set_input_transition");
  registerCommand<TclSetLoad>(interpreter, "set_load");
}

void registerDesignRuleConstraintCommands(SdcCommand& interpreter)
{
  registerCommand<TclSetMaxFanout>(interpreter, "set_max_fanout");
}

void registerLogicConstraintCommands(SdcCommand& interpreter)
{
  registerCommand<TclSetCaseAnalysis>(interpreter, "set_case_analysis");
}

void registerPathExceptionCommands(SdcCommand& interpreter)
{
  registerCommand<TclSetFalsePath>(interpreter, "set_false_path");
  registerCommand<TclSetMaxDelay>(interpreter, "set_max_delay");
  registerCommand<TclSetMinDelay>(interpreter, "set_min_delay");
  registerCommand<TclSetMulticyclePath>(interpreter, "set_multicycle_path");
  registerCommand<TclResetPath>(interpreter, "reset_path");
  registerCommand<TclResetPath>(interpreter, "unset_path_exceptions");
}

}  // namespace

void registerSdcCommands(SdcCommand& interpreter)
{
  registerCollectionCommands(interpreter);
  registerObjectQueryCommands(interpreter);
  registerClockConstraintCommands(interpreter);
  registerDelayConstraintCommands(interpreter);
  registerDesignRuleConstraintCommands(interpreter);
  registerLogicConstraintCommands(interpreter);
  registerPathExceptionCommands(interpreter);
}

}  // namespace ipw::sdc
