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
/**
 * @file WrapperClockDissolve.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-11-19
 * @brief Bypass the synthesis clock buffer tree before CTS builds its own
 */
#include <string>
#include <utility>
#include <vector>

#include "IdbDesign.h"
#include "IdbInstance.h"
#include "IdbNet.h"
#include "IdbPins.h"
#include "Logger.hh"
#include "Wrapper.hh"
#include "adapter/sdc/ClockTrace.hh"

namespace icts {
namespace {

// A clock gate, a mux and an IO pad define where a clock comes from; a buffer or an
// inverter only distributes an existing one.
auto isDissolvableStage(ClockTracePropagationKind kind) -> bool
{
  return kind == ClockTracePropagationKind::kBuffer || kind == ClockTracePropagationKind::kInverter;
}

auto findInstPin(idb::IdbInstance* idb_inst, const std::string& pin_name) -> idb::IdbPin*
{
  if (idb_inst == nullptr || pin_name.empty()) {
    return nullptr;
  }
  if (auto* idb_pin = idb_inst->get_pin_by_term(pin_name); idb_pin != nullptr) {
    return idb_pin;
  }
  auto* pin_list = idb_inst->get_pin_list();
  if (pin_list == nullptr) {
    return nullptr;
  }
  for (auto* candidate_pin : pin_list->get_pin_list()) {
    if (candidate_pin != nullptr && candidate_pin->get_pin_name() == pin_name) {
      return candidate_pin;
    }
  }
  return nullptr;
}

}  // namespace

auto Wrapper::dissolveSynthesisClockBuffers(const std::vector<ClockTraceClockTarget>& clock_targets) -> WrapperClockTreeDissolveResult
{
  WrapperClockTreeDissolveResult result;
  if (_idb_design == nullptr || clock_targets.empty()) {
    return result;
  }
  auto* inst_list = _idb_design->get_instance_list();
  if (inst_list == nullptr) {
    return result;
  }

  result.clocks.reserve(clock_targets.size());
  for (const auto& clock_target : clock_targets) {
    WrapperClockTreeDissolveClock row{.clock_name = clock_target.clock_name};
    for (const auto& step : clock_target.propagation_steps) {
      if (!isDissolvableStage(step.kind)) {
        continue;
      }
      auto* idb_inst = inst_list->find_instance(step.inst_name);
      auto* input_pin = findInstPin(idb_inst, step.input_pin_name);
      auto* output_pin = findInstPin(idb_inst, step.output_pin_name);
      if (idb_inst == nullptr || input_pin == nullptr || output_pin == nullptr || input_pin->get_net() == nullptr || output_pin->get_net() == nullptr) {
        ++row.skipped;
        continue;
      }
      // Placement status answers "may this cell be relocated", not "is this cell the final
      // answer": a PLACED stage is still synthesis fanout scaffolding.  Only the statuses
      // that forbid moving a cell protect it.
      if (idb_inst->is_fixed() || idb_inst->is_cover()) {
        ++row.preserved;
        continue;
      }
      // Nets are read from the pins, not from the trace evidence: an earlier bypass in the
      // same chain already merged this stage's input net.
      const std::string input_net_name = input_pin->get_net()->get_net_name();
      const std::string output_net_name = output_pin->get_net()->get_net_name();
      if (input_net_name.empty() || output_net_name.empty()) {
        ++row.skipped;
        continue;
      }
      // Remove the stage before merging its nets.  The reverse order would leave the
      // removed cell's output pin connected to its own input net, i.e. a combinational
      // loop, whenever the removal itself failed.
      if (!_idb_design->removeInstanceSafe(step.inst_name)) {
        CTSLOG.warn(Loc::current(), "CTS clock dissolve could not remove instance \"", step.inst_name, "\" of clock \"", clock_target.clock_name, "\".");
        ++row.skipped;
        continue;
      }
      // The output net carries the stage's loads and, for a bypassed chain, the next
      // stage's input pin: they all belong on the input net now.  The stage owned no route
      // segment the rebuilt tree may reuse, so its wires are not migrated.
      if (!_idb_design->mergeNetInto(input_net_name, output_net_name, /*move_wires=*/false)) {
        CTSLOG.warn(Loc::current(), "CTS clock dissolve could not merge net \"", output_net_name, "\" into net \"", input_net_name, "\" of clock \"",
                    clock_target.clock_name, "\".");
        ++row.skipped;
        continue;
      }
      ++row.dissolved;
    }
    result.dissolved += row.dissolved;
    result.preserved += row.preserved;
    result.skipped += row.skipped;
    result.clocks.push_back(std::move(row));
  }
  return result;
}

}  // namespace icts
