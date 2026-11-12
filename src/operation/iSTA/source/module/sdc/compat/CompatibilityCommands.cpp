// SPDX-License-Identifier: MulanPSL-2.0
#include "compat/CompatibilityCommands.hpp"
#include "compat/GetFan.hpp"
#include "CommandRegistration.hpp"
#include "shell/EditingCommands.hpp"
#include "shell/ObjectCommands.hpp"
namespace ista::sdc {
void registerCompatibilityCommands(SdcCommand& interpreter)
{
  registerCommand<TclAllDelayCorners>(interpreter, "all_delay_corners");
  registerCommand<TclGetFullName>(interpreter, "get_full_name");
  registerCommand<TclGetName>(interpreter, "get_name");
  registerCommand<TclGetFan>(interpreter, "get_fanin");
  registerCommand<TclGetFan>(interpreter, "get_fanout");
  registerConstraintCommand<TclRemoveClockLatency>(interpreter, "unset_clock_latency");
  registerConstraintCommand<TclRemoveClockUncertainty>(interpreter, "unset_clock_uncertainty");
  registerConstraintCommand<TclRemovePropagatedClock>(interpreter, "unset_propagated_clock");
  registerConstraintCommand<TclResetPath>(interpreter, "unset_path_exceptions");
}
}
