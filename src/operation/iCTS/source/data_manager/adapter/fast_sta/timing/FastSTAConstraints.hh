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
 * @file FastSTAConstraints.hh
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-09-05
 * @brief Resolve normalized constraints against resident timing identities.
 */

#pragma once

#include <optional>
#include <string>

#include "FastSTA.hh"
#include "liberty/FastSTACondition.hh"
#include "timing/TimingConstraints.hh"

namespace icts {

struct FastStaContext;
struct FastStaClockGateModel;

class FastStaConstraints
{
 public:
  static auto prepare(FastStaContext& context) -> std::optional<std::string>;
  static auto caseLookup(const FastStaContext& context, const std::string& inst_name) -> FastStaCondition::PinValueLookup;
  static auto gateActive(const FastStaContext& context, const std::string& inst_name, const FastStaClockGateModel& gate) -> FastStaLogicValue;
  static auto matches(const FastStaContext& context, const SdcObjectRef& object, FastStaNodeId node_id, const std::string& clock_name = {}) -> bool;
  static auto transitionMatches(SdcTransition selection, FastStaTransition transition) -> bool;
  static auto clock(const FastStaContext& context, const std::string& name) -> const SdcClockDecl*;
  static auto period(const FastStaContext& context, const std::string& name) -> double;
  static auto phase(const FastStaContext& context, const std::string& name, FastStaTransition transition) -> double;
  static auto isPropagated(const FastStaContext& context, const std::string& name) -> bool;
};

}  // namespace icts
