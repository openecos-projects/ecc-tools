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
 * @file FastSTAClockState.hh
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-05-19
 * @brief CTS fast STA resident graph state.
 */

#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "FastSTA.hh"
#include "clock_net_parasitic/FastSTAClockNetParasitic.hh"
#include "liberty/FastSTALibertyModel.hh"
#include "timing/FastSTAClockTiming.hh"
#include "timing/TimingConstraints.hh"

namespace icts {

enum class FastStaNodeKind
{
  kSource,
  kBufferInput,
  kBufferOutput,
  kSink
};

enum class FastStaNodeDomain
{
  kClock,
  kLogic
};

enum class FastStaNetDomain
{
  kClock,
  kLogic
};

struct FastStaNode
{
  FastStaNodeKind kind = FastStaNodeKind::kSink;
  std::string name = "";
  std::string inst_name = "";
  std::string pin_name = "";
  std::string cell_master = "";
  FastStaPoint location{};
  double input_cap_pf = 0.0;
  std::array<std::array<double, 2U>, 2U> input_cap_pf_by_timing{};
  double max_slew_ns = 0.0;
  FastStaNetId incoming_net_id = kInvalidFastStaNetId;
  std::vector<FastStaNetId> output_net_ids;
  FastStaTimingPoint timing{};
  double internal_power_w = 0.0;
  double leakage_power_w = 0.0;
  double area_um2 = 0.0;
  std::array<FastStaTimingPoint, 2U> early_timing{};
  std::array<FastStaTimingPoint, 2U> late_timing{};
  std::array<std::vector<FastStaTimingPoint>, 2U> tagged_early_timing{};
  std::array<std::vector<FastStaTimingPoint>, 2U> tagged_late_timing{};
  double arrival_seed_early_ns = 0.0;
  double arrival_seed_late_ns = 0.0;
  double slew_seed_early_ns = 0.0;
  double slew_seed_late_ns = 0.0;
  double clock_arrival_early_ns = 0.0;
  double clock_arrival_late_ns = 0.0;
  bool top_level = false;
  FastStaNodeDomain domain = FastStaNodeDomain::kClock;
  std::string port_name = "";
  std::string logic_function = "";
  std::string clock_name = "";
  std::optional<bool> case_value = std::nullopt;
  bool input = false;
  bool output = false;
  bool clock_pin = false;
  bool clock_inactive = false;
  std::string clock_from_port = "";
  std::string clock_to_port = "";
  bool clock_edge_triggered = false;
  FastStaTransition clock_trigger_transition = FastStaTransition::kRise;
  bool input_cap_profile_available = false;
  bool slew_limit_from_master = false;
};

struct FastStaNet
{
  std::string name = "";
  FastStaNodeId driver_node_id = kInvalidFastStaNodeId;
  std::vector<FastStaNodeId> load_node_ids;
  std::vector<FastStaRcNodeId> load_rc_node_ids;
  double wire_resistance_ohm = 0.0;
  double wire_cap_pf = 0.0;
  int64_t total_wirelength_dbu = 0;
  double load_cap_pf = 0.0;
  double max_cap_pf = 0.0;
  FastStaNetParasitic parasitic{};
  // Buffer-output propagation retains early/late rise/fall drivers. Nets
  // without a Liberty buffer driver use local input-port propagation and do
  // not allocate this state.
  std::vector<std::array<FastStaDmpDriverResult, 2U>> driver_timing_by_state;
  double switching_power_w = 0.0;
  double wire_delay_ns = 0.0;
  double max_fanout = 0.0;
  FastStaNetDomain domain = FastStaNetDomain::kClock;
  bool wire_delay_precomputed = false;
  bool cap_limit_from_master = false;
};

struct FastStaTimingArc
{
  FastStaNodeId from_node_id = kInvalidFastStaNodeId;
  FastStaNodeId to_node_id = kInvalidFastStaNodeId;
  std::string cell_master = "";
  std::string from_port = "";
  std::string to_port = "";
  bool positive_unate = false;
  bool negative_unate = false;
  bool clock_gate_boundary = false;
};

struct FastStaTimingLaunch
{
  FastStaNodeId output_node_id = kInvalidFastStaNodeId;
  FastStaNodeId clock_node_id = kInvalidFastStaNodeId;
  std::string cell_master = "";
  std::string clock_port = "";
  std::string output_port = "";
  FastStaTransition clock_transition = FastStaTransition::kRise;
};

struct FastStaTimingCheck
{
  FastStaNodeId data_node_id = kInvalidFastStaNodeId;
  FastStaNodeId clock_node_id = kInvalidFastStaNodeId;
  std::string cell_master = "";
  std::string clock_port = "";
  std::string data_port = "";
  FastStaTimingCheckKind kind = FastStaTimingCheckKind::kSetup;
  FastStaTransition clock_transition = FastStaTransition::kRise;
  std::optional<double> requirement_override_ns = std::nullopt;
  bool clock_gating = false;
};

struct FastStaLogicEdge
{
  FastStaNodeId to_node_id = kInvalidFastStaNodeId;
  std::size_t model_index = 0U;
  std::size_t load_index = 0U;
  bool net = false;
};

struct FastStaLogicTraversal
{
  std::vector<std::vector<std::pair<FastStaNodeId, FastStaLogicEdge>>> incoming;
  std::vector<std::vector<FastStaNodeId>> nodes_by_level;
};

struct FastStaLogicPreparation
{
  std::vector<std::vector<FastStaLogicEdge>> outgoing;
  std::vector<std::size_t> indegree;
  FastStaLogicTraversal traversal;
  std::string diagnostic;
  // Number of back edges disabled to make the logic graph orderable. Non-zero
  // means the netlist contains a combinational loop; propagation continues on
  // the acyclic remainder. Reported by the layer that owns the logger.
  std::size_t disabled_loop_edge_count = 0U;
};

struct FastStaContext
{
  Wrapper* wrapper = nullptr;
  std::string clock_name = "";
  std::string clock_net_name = "";
  double clock_period_ns = 0.0;
  double root_input_slew_ns = 0.0;
  std::size_t worker_count = 4U;
  int32_t dbu_per_um = 0;
  int routing_layer = 0;
  std::optional<double> wire_width_um = std::nullopt;
  FastStaNodeId source_node_id = kInvalidFastStaNodeId;
  std::vector<FastStaNode> nodes;
  std::vector<FastStaNet> nets;
  std::unordered_map<std::string, FastStaNodeId> node_id_by_name;
  std::unordered_map<std::string, FastStaNodeId> buffer_input_node_id_by_inst;
  std::unordered_map<std::string, FastStaNodeId> buffer_output_node_id_by_inst;
  std::unordered_map<std::pair<int, int>, FastStaNodeId, FastStaPointKeyHash> node_id_by_location;
  std::unordered_map<std::string, FastStaNetId> net_id_by_name;
  std::unordered_map<std::string, FastStaLibertyCell> liberty_cell_by_master;
  FastStaSkewSummary skew{};
  FastStaPowerSummary power{};
  bool timing_valid = false;
  bool logic_tags_valid = false;
  bool power_valid = false;
  bool clock_trial_active = false;
  // Clock-only candidate propagation is usable for skew/electrical screening;
  // a complete timing publication still requires FastStaTiming::update.
  bool clock_timing_valid = false;
  std::vector<FastStaTimingArc> timing_arcs;
  std::vector<FastStaTimingLaunch> timing_launches;
  std::vector<FastStaTimingCheck> timing_checks;
  std::vector<FastStaTimingRelationFact> timing_relations;
  FastStaTimingSeedSet timing_relation_seeds{};
  FastStaTimingSummary timing_summary{};
  SdcClockData constraints{};
  // Preserve declarations before generated-clock inference and resolution.
  SdcClockData input_constraints{};
  std::unordered_map<std::string, bool> case_values;
  std::vector<FastStaNodeId> clock_source_node_ids;
  std::vector<FastStaNodeKind> initial_node_kinds;
  std::vector<FastStaNodeDomain> initial_node_domains;
  std::vector<FastStaNetDomain> initial_net_domains;
  std::unordered_map<std::string, FastStaNodeId> initial_buffer_inputs;
  std::unordered_map<std::string, FastStaNodeId> initial_buffer_outputs;
  std::uint64_t liberty_revision = 0U;
  std::unordered_map<FastStaNodeId, std::array<std::array<double, 2U>, 2U>> unconstrained_io_caps;
  std::vector<FastStaNodeId> owned_clock_node_ids;
  std::vector<FastStaNodeId> owned_clock_sink_node_ids;
  bool owner_clock_scope_available = false;
  bool propagate_all_clocks = false;
  // Original input objects retain their IDs; CTS-only objects occupy the suffix.
  std::size_t input_node_count = 0U;
  std::size_t input_net_count = 0U;
  std::vector<FastStaNodeId> clock_overlay_node_ids;
  std::vector<FastStaNetId> clock_overlay_net_ids;
  std::vector<FastStaClockRouteGeometry> clock_routes;
  std::shared_ptr<const FastStaLogicPreparation> logic_preparation;
};

}  // namespace icts
