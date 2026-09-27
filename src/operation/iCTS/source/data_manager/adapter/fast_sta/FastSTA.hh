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
 * @file FastSTA.hh
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-05-18
 * @brief CTS-facing facade for fast timing and power calculation.
 */

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace icts {

class Clock;
class ClockLayout;
class Design;
class Net;
class Wrapper;
struct WrapperTimingGraph;
struct SdcClockData;
struct FastStaContext;
struct FastStaTimingSummary;
template <typename T>
class ClockSteinerTree;

}  // namespace icts

namespace icts {

using FastStaContextId = std::size_t;
using FastStaCharContextId = std::size_t;
using FastStaNodeId = std::size_t;
using FastStaNetId = std::size_t;
using FastStaRcNodeId = std::size_t;

constexpr auto kInvalidFastStaContextId = std::numeric_limits<FastStaContextId>::max();
constexpr auto kInvalidFastStaCharContextId = std::numeric_limits<FastStaCharContextId>::max();
constexpr auto kInvalidFastStaNodeId = std::numeric_limits<FastStaNodeId>::max();
constexpr auto kInvalidFastStaNetId = std::numeric_limits<FastStaNetId>::max();
constexpr auto kInvalidFastStaRcNodeId = std::numeric_limits<FastStaRcNodeId>::max();

struct FastStaPoint
{
  int x_dbu = 0;
  int y_dbu = 0;
};

struct FastStaRcSegment
{
  FastStaPoint begin{};
  FastStaPoint end{};
  double resistance_ohm = 0.0;
  double capacitance_pf = 0.0;
  bool electrical_values_valid = false;
};

enum class FastStaTransition
{
  kRise,
  kFall
};

enum class FastStaTimingCheckKind
{
  kSetup,
  kHold
};

struct FastStaClockNetRouteGeometry
{
  std::string net_name = "";
  std::vector<FastStaRcSegment> routed_segments;
};

struct FastStaClockRouteGeometry
{
  int32_t design_dbu_per_um = 0;
  std::vector<FastStaClockNetRouteGeometry> clock_nets;
};

struct FastStaClockNetRcTreeCounts
{
  std::size_t rc_node_count = 0U;
  std::size_t rc_edge_count = 0U;
};

struct FastStaGraphProfile
{
  std::size_t node_count = 0U;
  std::size_t owned_clock_node_count = 0U;
  std::size_t net_count = 0U;
  std::size_t sink_count = 0U;
  std::size_t buffer_input_count = 0U;
  std::size_t buffer_output_count = 0U;
  std::size_t logic_node_count = 0U;
  std::size_t logic_net_count = 0U;
  std::size_t timing_arc_count = 0U;
  std::size_t timing_check_count = 0U;
};

struct FastStaAnalysisStatus
{
  bool timing_valid = false;
  bool power_valid = false;
  bool clock_timing_valid = false;
};

struct FastStaClockElectricalScope
{
  std::vector<FastStaNodeId> node_ids;
  std::vector<FastStaNetId> net_ids;
};

struct FastStaClockSizingBuffer
{
  FastStaNodeId node_id = kInvalidFastStaNodeId;
  std::string inst_name = "";
  std::string cell_master = "";
};

struct FastStaClockSinkArrival
{
  FastStaNodeId node_id = kInvalidFastStaNodeId;
  std::string sink_name = "";
  double arrival_ns = 0.0;
  double slew_ns = 0.0;
};

enum class FastStaClockPointKind
{
  kSource,
  kBufferInput,
  kBufferOutput,
  kSink
};

struct FastStaClockPointFact
{
  std::string pin_name = "";
  FastStaClockPointKind kind = FastStaClockPointKind::kSink;
  double arrival_ns = 0.0;
  double slew_ns = 0.0;
};

struct FastStaClockTreeTopology
{
  FastStaNodeId source_node_id = kInvalidFastStaNodeId;
  std::vector<FastStaNodeId> parent_by_node;
  std::vector<std::vector<FastStaNodeId>> children_by_node;
};

enum class FastStaSlewRole
{
  kUnknown,
  kBufferInput,
  kSink
};

struct FastStaCapStatus
{
  FastStaNetId net_id = kInvalidFastStaNetId;
  std::string net_name = "";
  double load_cap_pf = 0.0;
  double max_cap_pf = 0.0;
  bool violated = false;
  bool constraint_available = false;
};

struct FastStaSlewStatus
{
  FastStaNodeId node_id = kInvalidFastStaNodeId;
  std::string node_name = "";
  FastStaSlewRole role = FastStaSlewRole::kUnknown;
  double slew_ns = 0.0;
  double max_slew_ns = 0.0;
  bool violated = false;
  bool constraint_available = false;
};

struct FastStaSkewSummary
{
  bool valid = false;
  FastStaNodeId min_sink_node_id = kInvalidFastStaNodeId;
  FastStaNodeId max_sink_node_id = kInvalidFastStaNodeId;
  std::string min_sink_name = "";
  std::string max_sink_name = "";
  double min_arrival_ns = 0.0;
  double max_arrival_ns = 0.0;
  double skew_ns = 0.0;
};

struct FastStaPowerSummary
{
  // Clock-domain power only. Unknown gate controls use a toggling-clock proxy;
  // no data-net activity is fabricated from the clock frequency.
  double switching_power_w = 0.0;
  double internal_power_w = 0.0;
  double leakage_power_w = 0.0;
  double total_power_w = 0.0;
  double area_um2 = 0.0;
  std::size_t unknown_gate_activity_count = 0U;
};

struct FastStaBufferMasterChange
{
  FastStaNodeId node_id = kInvalidFastStaNodeId;
  std::string cell_master = "";
};

struct FastStaInstanceMasterChange
{
  std::string inst_name = "";
  std::string cell_master = "";
};

struct FastStaTimingUncertainty
{
  double model_ns = 0.0;
  double rc_ns = 0.0;
  double numerical_ns = 0.0;

  [[nodiscard]] auto total_ns() const -> double { return model_ns + rc_ns + numerical_ns; }
};

struct FastStaTimingRelationFact
{
  std::string relation_id = "";
  std::string launch_pin_name = "";
  std::string capture_pin_name = "";
  FastStaNodeId launch_node_id = kInvalidFastStaNodeId;
  FastStaNodeId capture_node_id = kInvalidFastStaNodeId;
  FastStaNodeId launch_clock_node_id = kInvalidFastStaNodeId;
  FastStaNodeId capture_clock_node_id = kInvalidFastStaNodeId;
  FastStaTimingCheckKind check = FastStaTimingCheckKind::kSetup;
  FastStaTransition data_transition = FastStaTransition::kRise;
  FastStaTransition launch_clock_transition = FastStaTransition::kRise;
  FastStaTransition capture_clock_transition = FastStaTransition::kRise;
  double slack_ns = 0.0;
  FastStaTimingUncertainty uncertainty{};
  std::string launch_clock_name = "";
  std::string capture_clock_name = "";
  double arrival_ns = 0.0;
  double required_ns = 0.0;
  double requirement_ns = 0.0;
  bool clock_gating = false;
  bool output_delay = false;
};

enum class FastStaAnalysisKind
{
  kEarly,
  kLate
};

struct FastStaTimingPointFact
{
  std::string pin_name = "";
  std::string launch_pin_name = "";
  FastStaTransition transition = FastStaTransition::kRise;
  FastStaAnalysisKind analysis = FastStaAnalysisKind::kEarly;
  double arrival_ns = 0.0;
  double slew_ns = 0.0;
};

enum class FastStaTimingStage
{
  kLaunch,
  kCellOutput,
  kNetLoad
};

struct FastStaTimingStageFact
{
  std::string pin_name = "";
  std::string input_pin_name = "";
  std::string launch_pin_name = "";
  std::string cell_master = "";
  std::string from_port = "";
  std::string to_port = "";
  std::string net_name = "";
  std::string driver_library_name = "";
  std::string load_library_name = "";
  FastStaTransition transition = FastStaTransition::kRise;
  FastStaTransition input_transition = FastStaTransition::kRise;
  FastStaAnalysisKind analysis = FastStaAnalysisKind::kEarly;
  FastStaTimingStage stage = FastStaTimingStage::kNetLoad;
  std::size_t selected_variant_index = std::numeric_limits<std::size_t>::max();
  double input_arrival_ns = 0.0;
  double input_slew_ns = 0.0;
  double arrival_ns = 0.0;
  double slew_ns = 0.0;
  double stage_delay_ns = 0.0;
};

struct FastStaParasiticNodeFact
{
  std::string node_name = "";
  std::vector<std::string> terminal_pin_names;
  double capacitance_pf = 0.0;
  double wire_capacitance_pf = 0.0;
  double pin_capacitance_pf = 0.0;
};

struct FastStaParasiticEdgeFact
{
  std::size_t source_node_index = 0U;
  std::size_t target_node_index = 0U;
  double resistance_ohm = 0.0;
};

struct FastStaParasiticNetFact
{
  std::string net_name = "";
  std::string driver_pin_name = "";
  std::vector<std::string> load_pin_names;
  std::vector<FastStaParasiticNodeFact> nodes;
  std::vector<FastStaParasiticEdgeFact> edges;
};

struct FastStaPinCapacitanceFact
{
  std::string pin_name = "";
  std::array<std::array<double, 2U>, 2U> capacitance_pf{};
};

struct FastStaPiElmoreFact
{
  std::string net_name = "";
  std::string driver_pin_name = "";
  std::string load_pin_name = "";
  FastStaAnalysisKind analysis = FastStaAnalysisKind::kEarly;
  FastStaTransition transition = FastStaTransition::kRise;
  double near_cap_pf = 0.0;
  double far_cap_pf = 0.0;
  double resistance_ohm = 0.0;
  double elmore_ns = 0.0;
};

struct FastStaTimingSeedSet
{
  std::vector<std::size_t> relation_indexes;
};

struct FastStaClockArrivalDelta
{
  // Local insertion change at this physical clock point. It adds to every
  // reachable downstream clock point; nested insertions add, duplicate IDs fail.
  FastStaNodeId clock_node_id = kInvalidFastStaNodeId;
  double delta_ns = 0.0;
};

struct FastStaSeparationQuery
{
  std::vector<FastStaClockArrivalDelta> clock_deltas;
  double setup_floor_ns = 0.0;
  double hold_floor_ns = 0.0;
  double near_active_ns = 0.0;
  std::size_t maximum_near_active = 0U;
};

struct FastStaSeparationResult
{
  bool complete = false;
  std::vector<FastStaTimingRelationFact> violated;
  std::vector<FastStaTimingRelationFact> near_active;
  std::size_t evaluated_check_count = 0U;
  double runtime_s = 0.0;
  std::string diagnostic = "";
};

struct FastStaCharTopologySpec
{
  Wrapper* wrapper = nullptr;
  std::string source_cell_master = "";
  std::string sink_cell_master = "";
  std::vector<std::string> buffer_cell_masters;
  std::vector<double> wire_segments_um;
  std::optional<int32_t> dbu_per_um = std::nullopt;
  int routing_layer = 0;
  std::optional<double> wire_width_um = std::nullopt;
  double clock_period_ns = 0.0;
  double root_input_slew_ns = 0.0;
};

struct FastStaCharSampleResult
{
  bool valid = false;
  double delay_ns = 0.0;
  double output_slew_ns = 0.0;
  double power_w = 0.0;
  double source_boundary_net_switch_power_w = 0.0;
};

struct FastStaCharBuildResult
{
  std::optional<FastStaCharContextId> context_id = std::nullopt;
  std::string failure_reason = "";

  auto ok() const -> bool { return context_id.has_value(); }
};

struct FastStaEnvironment
{
  Wrapper* wrapper = nullptr;
  int32_t dbu_per_um = 0;
  int routing_layer = 0;
  std::optional<double> wire_width_um = std::nullopt;
  double root_input_slew_ns = 0.0;
  std::optional<double> max_cap_pf = std::nullopt;
  double max_sink_tran_ns = 0.0;
  std::size_t worker_count = 4U;
};

struct FastStaBuildInput
{
  const Clock* clock = nullptr;
  const FastStaClockRouteGeometry* route_geometry = nullptr;
  const WrapperTimingGraph* timing_graph = nullptr;
  const SdcClockData* constraints = nullptr;
  bool defer_timing_update = false;
  // Timing-only consumers do not fabricate missing Liberty power tables.
  bool require_power = true;
  // Borrowed only during construction; every context overlays all committed
  // clock topologies so the immutable input data graph cannot retain old CTS RC.
  const Design* committed_design = nullptr;
  const ClockLayout* committed_layout = nullptr;
  // Explicit CTS physical-clock view; graph-only callers default to SDC's
  // ideal/propagated declarations. This does not mutate the owned SDC snapshot.
  bool propagate_all_clocks = false;
};

struct FastStaBuildResult
{
  std::optional<FastStaContextId> context_id = std::nullopt;
  std::string failure_reason = "";

  auto ok() const -> bool { return context_id.has_value(); }
};

struct FastStaBranchState
{
  bool valid = false;
  double arrival_ns = 0.0;
  double slew_ns = 0.0;
  FastStaTransition source_transition = FastStaTransition::kRise;
};

using FastStaBranchStates = std::array<std::array<FastStaBranchState, 2U>, 2U>;

struct FastStaBranchLoad
{
  // Canonical committed pin identity; geometry and candidate labels are not lookup keys.
  std::string pin_name = "";
  FastStaPoint location{};
};

struct FastStaBranchRequest
{
  std::string component_id = "";
  std::string candidate_label = "";
  std::string representative_pin_name = "";
  std::string driver_cell_master = "";
  std::string driver_input_port = "";
  std::string driver_output_port = "";
  FastStaPoint driver_location{};
  FastStaBranchStates input_states{};
  std::vector<FastStaBranchLoad> loads;
  std::vector<FastStaRcSegment> rc_segments;
  // Required for conditional/gated drivers: canonical instance whose controls
  // supply case facts. Candidate labels and geometry never identify controls.
  std::string driver_inst_name = "";
};

struct FastStaBranchLoadResult
{
  std::string pin_name = "";
  FastStaBranchStates states{};
  bool clock_inactive = false;
};

struct FastStaBranchResult
{
  bool complete = false;
  std::string diagnostic = "";
  std::string component_id = "";
  std::string candidate_label = "";
  std::array<std::array<double, 2U>, 2U> upstream_input_cap_pf{};
  std::vector<FastStaBranchLoadResult> loads;
  std::size_t rc_node_count = 0U;
  std::size_t rc_edge_count = 0U;
  double runtime_s = 0.0;
};

enum class FastStaTimingStatus
{
  kNotRun,
  kComplete,
  kInvalidInput,
  kUnsupported
};

struct FastStaTimingSummary
{
  FastStaTimingStatus status = FastStaTimingStatus::kNotRun;
  std::size_t node_count = 0U;
  std::size_t net_count = 0U;
  std::size_t relation_count = 0U;
  std::size_t endpoint_count = 0U;
  std::size_t launch_count = 0U;
  std::size_t setup_relation_count = 0U;
  std::size_t hold_relation_count = 0U;
  std::size_t logic_rc_node_count = 0U;
  std::size_t logic_rc_edge_count = 0U;
  std::size_t logic_pin_cap_count = 0U;
  std::size_t unsupported_count = 0U;
  std::size_t conditional_fallback_arc_count = 0U;
  std::size_t conditional_extrema_bundle_count = 0U;
  std::size_t setup_violation_count = 0U;
  std::size_t hold_violation_count = 0U;
  std::size_t slew_violation_count = 0U;
  std::size_t cap_violation_count = 0U;
  std::size_t fanout_violation_count = 0U;
  std::optional<double> setup_min_slack_ns = std::nullopt;
  std::optional<double> hold_min_slack_ns = std::nullopt;
  double setup_wns_ns = 0.0;
  double setup_tns_ns = 0.0;
  double hold_wns_ns = 0.0;
  double hold_tns_ns = 0.0;
  double max_slew_ns = 0.0;
  double max_cap_pf = 0.0;
  double max_fanout = 0.0;
  double max_skew_ns = 0.0;
  double window_width_min_ns = 0.0;
  double window_width_median_ns = 0.0;
  double window_width_p95_ns = 0.0;
  double window_width_max_ns = 0.0;
  std::size_t window_conflict_count = 0U;
  double clock_propagation_runtime_s = 0.0;
  double logic_propagation_runtime_s = 0.0;
  double relation_extraction_runtime_s = 0.0;
  double runtime_s = 0.0;
  bool used_full_rebuild = false;
  std::size_t updated_clock_node_count = 0U;
  std::size_t updated_clock_net_count = 0U;
  std::size_t updated_logic_node_count = 0U;
  std::size_t visited_logic_node_count = 0U;
  std::string fallback_reason = "";
};

class FastSTA
{
 public:
  FastSTA();
  ~FastSTA();

  FastSTA(const FastSTA& rhs) = delete;
  FastSTA(FastSTA&& rhs) = delete;
  auto operator=(const FastSTA& rhs) -> FastSTA& = delete;
  auto operator=(FastSTA&& rhs) -> FastSTA& = delete;

  auto bindEnvironment(const FastStaEnvironment& environment) -> void;
  auto buildContext(const FastStaBuildInput& input) -> FastStaBuildResult;
  auto buildClockSizingContext(FastStaContextId source_context_id, const FastStaBuildInput& input) -> FastStaBuildResult;
  // A successful replacement returns a NEW monotonically allocated graph ID
  // and invalidates the old ID (including all node IDs cached under it).
  // Validation happens on an owned candidate; failure preserves the old context.
  auto replaceContext(FastStaContextId context_id, const FastStaBuildInput& input) -> FastStaBuildResult;
  auto synchronizeClockContext(FastStaContextId pending_id, const FastStaBuildInput& input) -> FastStaBuildResult;
  auto matchesClockInput(FastStaContextId context_id, const FastStaBuildInput& input) const -> bool;
  auto changeInstanceMasters(FastStaContextId context_id, const std::vector<FastStaInstanceMasterChange>& changes) -> bool;
  auto beginContextTransaction(FastStaContextId context_id) -> FastStaBuildResult;
  auto commitContextTransaction(FastStaContextId pending_id) -> bool;
  auto discardContextTransaction(FastStaContextId pending_id) -> bool;
  static auto collectClockRouteGeometry(const ClockLayout& layout, std::size_t clock_index) -> FastStaClockRouteGeometry;
  auto eraseContext(FastStaContextId context_id) -> bool;
  auto reset() -> void;

  auto buildCharContext(const FastStaCharTopologySpec& spec) -> FastStaCharBuildResult;
  auto eraseCharContext(FastStaCharContextId char_context_id) -> bool;
  auto setCharLoad(FastStaCharContextId char_context_id, double effective_load_pf) -> bool;
  auto runCharSample(FastStaCharContextId char_context_id, double input_slew_ns) -> FastStaCharSampleResult;

  auto changeBufferMasters(FastStaContextId context_id, const std::vector<FastStaBufferMasterChange>& changes) -> bool;
  auto changeBufferMastersTimingOnly(FastStaContextId context_id, const std::vector<FastStaBufferMasterChange>& changes) -> bool;
  auto beginBufferMastersClockTrial(FastStaContextId context_id, const std::vector<FastStaBufferMasterChange>& changes) -> bool;
  auto restoreBufferMastersClockTrial(FastStaContextId context_id) -> bool;
  auto commitBufferMastersClockTrial(FastStaContextId context_id) -> bool;
  auto updateTiming(FastStaContextId context_id) -> bool;
  auto updatePower(FastStaContextId context_id) -> bool;
  auto injectNetRouteTree(FastStaContextId context_id, const Net& net, const ClockSteinerTree<int>& route_tree, FastStaClockNetRcTreeCounts& rc_tree_counts)
      -> bool;

  auto queryGraphProfile(FastStaContextId context_id) const -> std::optional<FastStaGraphProfile>;
  auto queryAnalysisStatus(FastStaContextId context_id) const -> std::optional<FastStaAnalysisStatus>;
  auto queryClockElectricalScope(FastStaContextId context_id) const -> std::optional<FastStaClockElectricalScope>;
  auto queryTimingSummary(FastStaContextId context_id) const -> std::optional<FastStaTimingSummary>;
  auto queryClockTreeTopology(FastStaContextId context_id) const -> std::optional<FastStaClockTreeTopology>;
  auto collectClockSizingBuffers(FastStaContextId context_id) const -> std::vector<FastStaClockSizingBuffer>;
  auto collectClockSinkArrivals(FastStaContextId context_id) const -> std::vector<FastStaClockSinkArrival>;
  // Full-graph clock electrical diagnostics, including propagated gate outputs
  // beyond the physical owner boundary. Sizing buffers, sink arrivals and skew
  // are separately restricted to the context's physical Clock owner.
  auto collectClockPointFacts(FastStaContextId context_id) const -> std::vector<FastStaClockPointFact>;
  auto queryClockNodeArrival(FastStaContextId context_id, FastStaNodeId node_id) const -> std::optional<double>;
  auto querySkew(FastStaContextId context_id) const -> FastStaSkewSummary;
  auto queryCapStatus(FastStaContextId context_id, FastStaNetId net_id) const -> std::optional<FastStaCapStatus>;
  auto querySlewStatus(FastStaContextId context_id, FastStaNodeId node_id) const -> std::optional<FastStaSlewStatus>;
  auto queryPower(FastStaContextId context_id) const -> std::optional<FastStaPowerSummary>;
  auto collectTimingRelations(FastStaContextId context_id) const -> std::vector<FastStaTimingRelationFact>;
  auto collectTimingPointFacts(FastStaContextId context_id) const -> std::vector<FastStaTimingPointFact>;
  auto collectTimingStageFacts(FastStaContextId context_id, std::optional<FastStaNodeId> node_filter = std::nullopt) const
      -> std::vector<FastStaTimingStageFact>;
  auto collectParasiticNetFacts(FastStaContextId context_id) const -> std::vector<FastStaParasiticNetFact>;
  auto collectPinCapacitanceFacts(FastStaContextId context_id) const -> std::vector<FastStaPinCapacitanceFact>;
  auto collectPiElmoreFacts(FastStaContextId context_id) const -> std::vector<FastStaPiElmoreFact>;
  auto evaluateBranch(FastStaContextId context_id, const FastStaBranchRequest& request) const -> FastStaBranchResult;
  auto collectTimingRelationSeeds(FastStaContextId context_id) const -> FastStaTimingSeedSet;
  auto separateTimingRelations(FastStaContextId context_id, const FastStaSeparationQuery& query) const -> FastStaSeparationResult;

 private:
  struct ContextStore;

  auto queryContext(FastStaContextId context_id) const -> const FastStaContext*;
  auto mutableContext(FastStaContextId context_id) -> FastStaContext*;

  std::unique_ptr<ContextStore> _contexts;
  std::optional<FastStaEnvironment> _environment = std::nullopt;
};

}  // namespace icts
