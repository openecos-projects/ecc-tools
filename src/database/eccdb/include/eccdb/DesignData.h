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
#pragma once

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "eccdb/Types.h"

namespace eccdb {

// Public Data types own their contents. Querying one returns a detached
// snapshot; modifying it does not change the Database until update* is called.
struct InstanceData
{
  std::string name;
  CellMasterId master;
  Point origin;
  Orientation orientation = Orientation::kN;
  PlacementStatus placement_status = PlacementStatus::kUnplaced;
  InstanceSource source = InstanceSource::kNone;
};

struct InstancePinData
{
  InstanceId instance;
  MasterTermId master_term;
  NetId net;
  NetId special_net;
};

struct IoPinData
{
  std::string name;
  IoDirection direction = IoDirection::kNone;
  SignalUse use = SignalUse::kNone;
  NetId net;
  NetId special_net;
};

struct NetSpacingRule
{
  RoutingLayerId layer;
  int32_t spacing = 0;
  std::optional<std::pair<int32_t, int32_t>> range;
};

struct NetOptions
{
  std::optional<std::string> original;
  std::optional<NetPattern> pattern;
  std::optional<double> estimated_capacitance;
  std::optional<double> frequency;
  std::optional<int32_t> xtalk;
  std::optional<int32_t> style;
  std::optional<int32_t> voltage;
  std::vector<NetSpacingRule> spacing_rules;
};

struct NetData
{
  std::string name;
  SignalUse use = SignalUse::kNone;
  NetSource source = NetSource::kNone;
  std::optional<int32_t> weight;
  TechRuleId tech_non_default_rule;
  DesignRuleId design_non_default_rule;
};

}  // namespace eccdb
