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
#include "DataManager.hpp"
#include "SdcCommand.hpp"
#include "SdcTclCmd.hpp"
#include "SetUnits.hpp"
#include "WriteSdc.hpp"
#include "clock/ClockCommands.hpp"
#include "environment/EnvironmentCommands.hpp"
#include "exception/PathExceptionCommand.hpp"
#include "query/GetFan.hpp"
#include "query/ObjectCommands.hpp"

namespace ista::sdc {

namespace {

template <typename Command>
void registerCommand(SdcCommand& interpreter, const char* name)
{
  interpreter.createCmd(name, &executeTclCommand<Command>);
}

template <typename Command>
int executeConstraintCommand(ClientData data, Tcl_Interp* interp, int objc, Tcl_Obj* const objv[])
{
  if (DataManager::isInitialized())
    STADM.getDatabase().sdc().markChanged();
  return executeTclCommand<Command>(data, interp, objc, objv);
}

template <typename Command>
void registerConstraintCommand(SdcCommand& interpreter, const char* name)
{
  interpreter.createCmd(name, &executeConstraintCommand<Command>);
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
  registerCommand<TclAllDelayCorners>(interpreter, "all_delay_corners");
  registerCommand<TclGetFullName>(interpreter, "get_full_name");
  registerCommand<TclGetFullName>(interpreter, "get_object_name");
  registerCommand<TclGetName>(interpreter, "get_name");
  registerCommand<TclGetFan>(interpreter, "get_fanin");
  registerCommand<TclGetFan>(interpreter, "get_fanout");
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
  registerConstraintCommand<TclCreateClock>(interpreter, "create_clock");
  registerConstraintCommand<TclCreateGeneratedClock>(interpreter, "create_generated_clock");
  registerConstraintCommand<TclSetClockGroups>(interpreter, "set_clock_groups");
  registerConstraintCommand<TclSetClockLatency>(interpreter, "set_clock_latency");
  registerConstraintCommand<TclRemoveClockLatency>(interpreter, "remove_clock_latency");
  registerConstraintCommand<TclRemoveClockLatency>(interpreter, "unset_clock_latency");
  registerConstraintCommand<TclSetClockTransition>(interpreter, "set_clock_transition");
  registerConstraintCommand<TclSetClockUncertainty>(interpreter, "set_clock_uncertainty");
  registerConstraintCommand<TclRemoveClockUncertainty>(interpreter, "remove_clock_uncertainty");
  registerConstraintCommand<TclRemoveClockUncertainty>(interpreter, "unset_clock_uncertainty");
  registerConstraintCommand<TclSetPropagatedClock>(interpreter, "set_propagated_clock");
  registerConstraintCommand<TclRemovePropagatedClock>(interpreter, "remove_propagated_clock");
  registerConstraintCommand<TclRemovePropagatedClock>(interpreter, "unset_propagated_clock");
}

void registerEnvironmentConstraintCommands(SdcCommand& interpreter)
{
  registerConstraintCommand<TclSetDrivingCell>(interpreter, "set_driving_cell");
  registerConstraintCommand<TclSetInputDelay>(interpreter, "set_input_delay");
  registerConstraintCommand<TclSetOutputDelay>(interpreter, "set_output_delay");
  registerConstraintCommand<TclSetInputTransition>(interpreter, "set_input_transition");
  registerConstraintCommand<TclSetLoad>(interpreter, "set_load");
  registerConstraintCommand<TclSetMaxFanout>(interpreter, "set_max_fanout");
  registerConstraintCommand<TclSetElectricalLimit>(interpreter, "set_max_transition");
  registerConstraintCommand<TclSetElectricalLimit>(interpreter, "set_max_capacitance");
  registerConstraintCommand<TclSetTimingDerate>(interpreter, "set_timing_derate");
  registerConstraintCommand<TclSetCaseAnalysis>(interpreter, "set_case_analysis");
}

void registerPathExceptionCommands(SdcCommand& interpreter)
{
  registerConstraintCommand<TclSetFalsePath>(interpreter, "set_false_path");
  registerConstraintCommand<TclSetMaxDelay>(interpreter, "set_max_delay");
  registerConstraintCommand<TclSetMinDelay>(interpreter, "set_min_delay");
  registerConstraintCommand<TclSetMulticyclePath>(interpreter, "set_multicycle_path");
  registerConstraintCommand<TclResetPath>(interpreter, "reset_path");
  registerConstraintCommand<TclResetPath>(interpreter, "unset_path_exceptions");
}

}  // namespace

void registerSdcCommands(SdcCommand& interpreter)
{
  interpreter.createCmd("write_sdc", &writeSdcCommand);
  registerCommand<TclSetUnits>(interpreter, "set_units");
  registerCollectionCommands(interpreter);
  registerObjectQueryCommands(interpreter);
  registerClockConstraintCommands(interpreter);
  registerEnvironmentConstraintCommands(interpreter);
  registerPathExceptionCommands(interpreter);
}

}  // namespace ista::sdc
