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
 * @file FastSTAContextStore.hh
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-09-13
 * @brief Private context ownership and reversible timing edit records.
 */

#pragma once

#include <array>
#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "FastSTA.hh"
#include "clock_sizing/FastSTAClockSizingEdit.hh"
#include "clock_state/FastSTAClockState.hh"
#include "segment_char/FastSTAChar.hh"

namespace icts {

struct FastSTA::ContextStore
{
  struct LogicTimingSnapshot
  {
    FastStaNodeId id;
    FastStaTimingPoint scalar;
    std::array<FastStaTimingPoint, 2U> early;
    std::array<FastStaTimingPoint, 2U> late;
    std::array<std::vector<FastStaTimingPoint>, 2U> tagged_early;
    std::array<std::vector<FastStaTimingPoint>, 2U> tagged_late;

    LogicTimingSnapshot(FastStaNodeId node_id, const FastStaNode& node);

    void swap(FastStaContext& context);
  };

  struct NodePreparationSnapshot
  {
    FastStaNodeId id;
    FastStaNodeKind kind;
    FastStaNodeDomain domain;
    std::string clock_name;
    std::string clock_from_port;
    std::string clock_to_port;
    std::optional<bool> case_value;
    std::array<std::array<double, 2U>, 2U> caps;
    double input_cap;
    bool clock_inactive;
    bool clock_edge_triggered;
    FastStaTransition clock_trigger_transition;
    bool cap_available;

    NodePreparationSnapshot(FastStaNodeId node_id, const FastStaNode& node);

    void swap(FastStaContext& context);
  };

  struct ClockTopologySnapshot
  {
    std::vector<FastStaNode> node_suffix;
    std::vector<FastStaNet> net_suffix;
    std::vector<NodePreparationSnapshot> preparation;
    std::vector<FastStaNodeKind> initial_kinds;
    std::vector<FastStaNodeDomain> initial_node_domains;
    std::vector<FastStaNetDomain> initial_net_domains;
    std::unordered_map<std::string, FastStaNodeId> initial_inputs;
    std::unordered_map<std::string, FastStaNodeId> initial_outputs;
    std::unordered_map<std::string, FastStaNodeId> inputs;
    std::unordered_map<std::string, FastStaNodeId> outputs;
    std::unordered_map<std::string, bool> case_values;
    std::unordered_map<FastStaNodeId, std::array<std::array<double, 2U>, 2U>> io_caps;
    SdcClockData constraints;
    std::vector<FastStaNodeId> clock_sources;
    std::vector<FastStaNodeId> owned_nodes;
    std::vector<FastStaNodeId> owned_sinks;
    std::vector<FastStaNodeId> overlay_nodes;
    std::vector<FastStaNetId> overlay_nets;
    std::vector<FastStaClockRouteGeometry> routes;
    std::shared_ptr<const FastStaLogicPreparation> logic_preparation;
    FastStaNodeId source;
    std::string clock_name;
    double period;
    bool owner_scope;

    ClockTopologySnapshot(const FastStaContext& context, const std::unordered_set<FastStaNodeId>& complete_nodes);

    void swap(FastStaContext& context);
  };

  struct ClockTrialNodeSnapshot
  {
    FastStaNodeId id;
    FastStaTimingPoint timing;
    std::array<FastStaTimingPoint, 2U> early;
    std::array<FastStaTimingPoint, 2U> late;
    std::array<double, 3U> power;
    bool inactive;

    ClockTrialNodeSnapshot(FastStaNodeId node_id, const FastStaNode& node);

    void swap(FastStaContext& context);
  };

  struct ClockTrialNetSnapshot
  {
    FastStaNetId id;
    std::vector<std::array<FastStaDmpDriverResult, 2U>> drivers;
    double switching_power_w;
    double max_cap_pf;

    ClockTrialNetSnapshot(FastStaNetId net_id, FastStaNet& net);

    void swap(FastStaContext& context);
  };

  struct TimingEditSnapshot
  {
    std::vector<std::pair<FastStaNodeId, FastStaNode>> nodes;
    std::vector<std::pair<FastStaNetId, FastStaNet>> nets;
    std::vector<ClockTrialNodeSnapshot> clock_nodes;
    std::vector<ClockTrialNetSnapshot> clock_nets;
    std::vector<std::pair<std::size_t, FastStaTimingArc>> arcs;
    std::vector<LogicTimingSnapshot> logic;
    std::vector<std::pair<FastStaNodeId, std::array<double, 3U>>> additional_node_power;
    std::vector<std::pair<FastStaNetId, double>> additional_net_power;
    std::vector<FastStaTimingRelationFact> relations;
    FastStaTimingSeedSet seeds;
    FastStaSkewSummary skew;
    FastStaTimingSummary summary;
    FastStaPowerSummary power;
    std::unique_ptr<FastStaContext> cold_state;
    std::unique_ptr<ClockTopologySnapshot> topology;
    bool complete = false;
    bool timing_valid = false;
    bool power_valid = false;
    bool logic_tags_valid = false;
    bool clock_timing_valid = false;
    bool clock_trial_active = false;

    TimingEditSnapshot(FastStaContext& context, const std::vector<FastStaBufferMasterChange>& changes, const FastStaDirtyRegion* region = nullptr,
                       const std::vector<FastStaNodeId>* affected_logic = nullptr);

    // Swapping is an exact inverse, including published tags/relations and
    // electrical reductions. No propagation or library access occurs here.
    void swap(FastStaContext& context);
  };

  struct ContextTransaction
  {
    FastStaContextId committed_id;
    FastStaContextId pending_id;
    bool pending_active = true;
    std::vector<TimingEditSnapshot> edits;

    void activate(FastStaContext& context, bool pending);
  };

  static auto applyBufferMasters(FastStaContext& context, const std::vector<FastStaBufferMasterChange>& changes, bool update_power,
                                 ContextTransaction* transaction = nullptr) -> bool;
  std::vector<std::unique_ptr<FastStaContext>> contexts;
  std::vector<bool> context_valid;
  std::vector<std::unique_ptr<FastStaChar::Context>> char_contexts;
  std::vector<bool> char_context_valid;
  std::unordered_map<FastStaContextId, TimingEditSnapshot> clock_trials;
  std::unordered_map<FastStaContextId, ContextTransaction> transactions;
  std::unordered_map<FastStaContextId, FastStaContextId> pending_by_committed;
};

}  // namespace icts
