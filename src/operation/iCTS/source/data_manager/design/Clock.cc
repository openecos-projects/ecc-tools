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
 * @file Clock.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-08-14
 * @brief Explicit per-clock propagation membership implementation.
 */

#include "data_manager/design/Clock.hh"

#include <algorithm>
#include <string>

#include "data_manager/design/Inst.hh"
#include "data_manager/design/Pin.hh"

namespace icts {
namespace {

auto IsInputDirection(PinType type) -> bool
{
  return type == PinType::kIn || type == PinType::kClock;
}

auto IsOutputDirection(PinType type) -> bool
{
  return type == PinType::kOut;
}

auto Failure(ClockPropagationMutationCode code, std::string message) -> ClockPropagationMutationStatus
{
  return ClockPropagationMutationStatus{.code = code, .message = std::move(message)};
}

}  // namespace

auto Clock::findPropagationArc(const Inst* inst) const -> const ClockPropagationArc*
{
  if (inst == nullptr) {
    return nullptr;
  }
  const auto iter = std::ranges::find_if(_propagation_arcs, [inst](const ClockPropagationArc& arc) -> bool { return arc.inst == inst; });
  return iter == _propagation_arcs.end() ? nullptr : &(*iter);
}

auto Clock::findPropagationArc(const Pin* pin) const -> const ClockPropagationArc*
{
  if (pin == nullptr) {
    return nullptr;
  }
  const auto iter
      = std::ranges::find_if(_propagation_arcs, [pin](const ClockPropagationArc& arc) -> bool { return arc.input_pin == pin || arc.output_pin == pin; });
  return iter == _propagation_arcs.end() ? nullptr : &(*iter);
}

auto Clock::validatePropagationArc(const ClockPropagationArc& arc) const -> ClockPropagationMutationStatus
{
  if (arc.inst == nullptr || arc.input_pin == nullptr || arc.output_pin == nullptr || arc.input_pin == arc.output_pin || arc.path_buffer_weight < 0) {
    return Failure(ClockPropagationMutationCode::kIncomplete, "incomplete_clock_propagation_arc");
  }
  if (arc.input_pin->get_inst() != arc.inst || arc.output_pin->get_inst() != arc.inst) {
    return Failure(ClockPropagationMutationCode::kPinInstMismatch, "clock_propagation_pin_inst_mismatch");
  }
  if (!IsInputDirection(arc.input_pin->get_type()) || !IsOutputDirection(arc.output_pin->get_type())) {
    return Failure(ClockPropagationMutationCode::kInvalidPinDirection, "invalid_clock_propagation_pin_direction");
  }
  const bool physical_kind_matches
      = (arc.kind == ClockPropagationKind::kBuffer && arc.inst->is_buffer()) || (arc.kind == ClockPropagationKind::kInverter && arc.inst->is_inverter());
  if (!physical_kind_matches) {
    return Failure(ClockPropagationMutationCode::kPhysicalKindMismatch, "clock_propagation_physical_kind_mismatch");
  }
  if (findPropagationArc(arc.inst) != nullptr) {
    return Failure(ClockPropagationMutationCode::kDuplicateInst, "duplicate_clock_propagation_inst");
  }
  if (findPropagationArc(arc.input_pin) != nullptr || findPropagationArc(arc.output_pin) != nullptr) {
    return Failure(ClockPropagationMutationCode::kDuplicatePin, "duplicate_clock_propagation_pin");
  }

  return {};
}

auto Clock::addPropagationArc(const ClockPropagationArc& arc) -> ClockPropagationMutationStatus
{
  auto validation = validatePropagationArc(arc);
  if (!validation.ok()) {
    return validation;
  }

  _propagation_arcs.push_back(arc);
  appendUnique(_insts, arc.inst);
  return {};
}

auto Clock::removePropagationArcsFor(const Inst* inst) -> void
{
  if (inst == nullptr) {
    return;
  }
  std::erase_if(_propagation_arcs, [inst](const ClockPropagationArc& arc) -> bool { return arc.inst == inst; });
  std::erase(_insts, inst);
}

auto Clock::removePropagationArcsFor(const Pin* pin) -> void
{
  if (pin == nullptr) {
    return;
  }
  std::vector<Inst*> affected_insts;
  for (const auto& arc : _propagation_arcs) {
    if (arc.input_pin == pin || arc.output_pin == pin) {
      affected_insts.push_back(arc.inst);
    }
  }
  std::erase_if(_propagation_arcs, [pin](const ClockPropagationArc& arc) -> bool { return arc.input_pin == pin || arc.output_pin == pin; });
  for (auto* inst : affected_insts) {
    if (findPropagationArc(inst) == nullptr) {
      std::erase(_insts, inst);
    }
  }
}

}  // namespace icts
