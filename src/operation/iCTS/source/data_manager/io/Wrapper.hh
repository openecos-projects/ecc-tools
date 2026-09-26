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
 * @file Wrapper.hh
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-01-11
 * @brief DB wrapper for iCTS
 */

#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "spatial/Point.hh"
#include "timing/TimingConstraints.hh"

namespace idb {
class IdbBuilder;
class IdbDesign;
class IdbInstance;
class IdbLayout;
class IdbLayerRouting;
class IdbNet;
class IdbPin;
template <typename T>
class IdbCoordinate;
class LibCell;
class LibLibrary;
}  // namespace idb

namespace idm {
class RawLibertyGeneration;
}  // namespace idm

namespace icts {

class Clock;
class Config;
class Design;
class Inst;
class Net;
class Pin;
struct SdcClockTraceInput;
struct ClockTraceBuild;
struct ClockTraceClockTarget;
struct ClockRouteSegmentRc;
struct WrapperCellGeometry
{
  std::string name;
  std::string cell_master;
  Point<int> origin = Point<int>(-1, -1);
  int32_t width_dbu = 0;
  int32_t height_dbu = 0;
};

struct WrapperWriteSummary
{
  bool success = false;
  std::string failed_clock;
  std::string failed_net;
  bool idb_clock_tree_restored = false;
  std::size_t inserted_inst_count = 0U;
  std::size_t inserted_net_count = 0U;
  std::string reason;
};

enum class WrapperRoutingDirection
{
  kHorizontal,
  kVertical
};

struct WrapperSignalRoutingLayer
{
  std::string name;
  int32_t id = -1;
  uint32_t order = 0U;
  WrapperRoutingDirection direction = WrapperRoutingDirection::kHorizontal;
  int32_t width_dbu = 0;
  double width_um = 0.0;
  double sheet_resistance_ohm_per_square = 0.0;
  double area_capacitance_pf_per_um2 = 0.0;
  double edge_capacitance_pf_per_um = 0.0;
  double resistance_ohm_per_um = 0.0;
  double capacitance_pf_per_um = 0.0;
};

enum class WrapperSignalRoutingStatus
{
  kComplete,
  kLayoutUnavailable,
  kAnchorUnavailable,
  kAnchorNotRouting,
  kInvalidDbu,
  kDirectionalLayerUnavailable
};

struct WrapperSignalRoutingAuthority
{
  WrapperSignalRoutingStatus status = WrapperSignalRoutingStatus::kLayoutUnavailable;
  std::string policy_version = "idb-signal-routing-rc-v1";
  std::string anchor_name;
  int32_t anchor_id = -1;
  uint32_t anchor_order = 0U;
  std::optional<WrapperSignalRoutingLayer> horizontal;
  std::optional<WrapperSignalRoutingLayer> vertical;
  std::string diagnostic;

  [[nodiscard]] auto complete() const -> bool { return status == WrapperSignalRoutingStatus::kComplete && horizontal.has_value() && vertical.has_value(); }
};

struct WrapperLogicTerminal
{
  std::string pin_name;
  int32_t x_dbu = 0;
  int32_t y_dbu = 0;
  bool driver = false;
};

struct WrapperLogicNetGeometry
{
  std::string net_name;
  bool clock_net = false;
  std::vector<WrapperLogicTerminal> terminals;
};

enum class WrapperLogicGeometryStatus
{
  kComplete,
  kDesignUnavailable,
  kInvalidTerminal,
  kDriverUnavailable,
  kMultipleDrivers
};

struct WrapperLogicGeometryResult
{
  WrapperLogicGeometryStatus status = WrapperLogicGeometryStatus::kDesignUnavailable;
  std::vector<WrapperLogicNetGeometry> nets;
  std::size_t excluded_clock_net_count = 0U;
  std::size_t excluded_nonlogic_net_count = 0U;
  std::size_t excluded_singleton_net_count = 0U;
  std::string normalization_algorithm_version = "raw-idb-connectivity-v1";
  std::size_t normalization_repair_count = 0U;
  std::string normalization_receipt_fingerprint;
  std::string normalized_membership_fingerprint;
  std::string diagnostic;

  [[nodiscard]] auto complete() const -> bool { return status == WrapperLogicGeometryStatus::kComplete; }
};

struct WrapperTimingNode
{
  std::string pin_name = "";
  std::string inst_name = "";
  std::string cell_master = "";
  int32_t x_dbu = 0;
  int32_t y_dbu = 0;
  double input_cap_pf = 0.0;
  std::array<std::array<double, 2U>, 2U> input_cap_pf_by_timing{};
  double max_slew_ns = 0.0;
  bool input = false;
  bool output = false;
  bool top_level = false;
  std::string port_name = "";
  std::string logic_function = "";
  bool clock_pin = false;
  bool input_cap_profile_available = false;
  bool slew_limit_from_master = false;
};

struct WrapperTimingRcSegment
{
  int32_t begin_x_dbu = 0;
  int32_t begin_y_dbu = 0;
  int32_t end_x_dbu = 0;
  int32_t end_y_dbu = 0;
  double resistance_ohm = 0.0;
  double capacitance_pf = 0.0;
};

struct WrapperTimingNet
{
  std::string net_name = "";
  std::string driver_pin = "";
  std::vector<std::string> load_pins;
  double wire_resistance_ohm = 0.0;
  double wire_cap_pf = 0.0;
  int64_t total_wirelength_dbu = 0;
  std::vector<WrapperTimingRcSegment> rc_segments;
  bool physically_zero_length = false;
};

struct WrapperTimingArc
{
  std::string inst_name = "";
  std::string cell_master = "";
  std::string input_port = "";
  std::string output_port = "";
  std::string input_pin = "";
  std::string output_pin = "";
  bool positive_unate = false;
  bool negative_unate = false;
  bool clock_gate_boundary = false;
};

enum class WrapperTimingTransition
{
  kRise,
  kFall
};

struct WrapperTimingLaunch
{
  std::string inst_name = "";
  std::string cell_master = "";
  std::string clock_port = "";
  std::string output_port = "";
  std::string clock_pin = "";
  std::string output_pin = "";
  WrapperTimingTransition clock_transition = WrapperTimingTransition::kRise;
};

enum class WrapperTimingCheckKind
{
  kSetup,
  kHold
};

struct WrapperTimingCheck
{
  std::string inst_name = "";
  std::string cell_master = "";
  std::string clock_port = "";
  std::string data_port = "";
  std::string clock_pin = "";
  std::string data_pin = "";
  WrapperTimingCheckKind kind = WrapperTimingCheckKind::kSetup;
  WrapperTimingTransition clock_transition = WrapperTimingTransition::kRise;
  bool clock_gating = false;
};

enum class WrapperTimingGraphStatus
{
  kComplete,
  kDesignUnavailable,
  kLibraryUnavailable,
  kConnectivityInvalid,
  kUnsupported
};

struct WrapperTimingGraph
{
  WrapperTimingGraphStatus status = WrapperTimingGraphStatus::kDesignUnavailable;
  std::vector<WrapperTimingNode> nodes;
  std::vector<WrapperTimingNet> nets;
  std::vector<WrapperTimingArc> arcs;
  std::vector<WrapperTimingLaunch> launches;
  std::vector<WrapperTimingCheck> checks;
  WrapperSignalRoutingAuthority signal_routing_authority{};
  std::size_t unsupported_arc_count = 0U;
  double graph_construction_runtime_s = 0.0;
  double logic_rc_construction_runtime_s = 0.0;
  std::string diagnostic = "";

  [[nodiscard]] auto complete() const -> bool { return status == WrapperTimingGraphStatus::kComplete; }
};

class Wrapper
{
 public:
  Wrapper();
  ~Wrapper();

  // Delete copy and move constructors
  Wrapper(const Wrapper& rhs) = delete;
  Wrapper(Wrapper&& rhs) = delete;
  auto operator=(const Wrapper& rhs) -> Wrapper& = delete;
  auto operator=(Wrapper&& rhs) -> Wrapper& = delete;

  // Initialize with idb builder
  auto init(idb::IdbBuilder* idb) -> void;

  // Reset wrapper
  auto reset() -> void;
  auto clearCtsBindings() -> void;

  struct RootDriverCost
  {
    bool valid = false;
    bool power_available = false;
    std::string method;
    std::string cell_master;
    double input_slew_ns = 0.0;
    double output_load_pf = 0.0;
    double cell_delay_ns = 0.0;
    double output_slew_ns = 0.0;
    double internal_power_w = 0.0;
    double leakage_power_w = 0.0;
    double cell_power_w = 0.0;
  };

  struct BufferPorts
  {
    std::string input;
    std::string output;
  };

  struct ClockSourceDriveCapLimitInput
  {
    const Pin* clock_source = nullptr;
    std::optional<double> configured_max_cap_pf = std::nullopt;
  };

  struct PinSlewLimitInput
  {
    const Pin* pin = nullptr;
    double configured_max_sink_tran_ns = 0.0;
  };

  struct WireCapacitanceProfile
  {
    double area_cap_pf = 0.0;
    double edge_cap_pf = 0.0;
    double ground_cap_pf = 0.0;
    double coupling_cap_pf = 0.0;
    double timing_coupling_factor = 0.0;
    double total_cap_pf = 0.0;
    double timing_effective_cap_pf = 0.0;
  };

  auto queryDbUnit() const -> std::optional<int32_t>;
  auto querySdcUnits() const -> std::optional<SdcUnits>;
  auto querySupplyVoltage() const -> std::optional<double>;
  auto queryLibertyRevision() const -> std::uint64_t;
  static auto queryParallelWorkerCount() -> std::size_t;
  auto is_design_ready() const -> bool { return _idb_design != nullptr; }
  auto is_layout_ready() const -> bool { return _idb != nullptr && _idb_layout != nullptr; }
  auto queryWireResistance(int routing_layer, double length_um, std::optional<double> wire_width_um = std::nullopt) const -> std::optional<double>;
  auto queryWireCapacitance(int routing_layer, double length_um, std::optional<double> wire_width_um = std::nullopt) const -> std::optional<double>;
  auto queryRequiredWireResistance(int routing_layer, double length_um, std::optional<double> wire_width_um = std::nullopt) const -> double;
  auto queryRequiredWireCapacitance(int routing_layer, double length_um, std::optional<double> wire_width_um = std::nullopt) const -> double;
  auto queryRequiredWireCapacitanceProfile(int routing_layer, double length_um, std::optional<double> wire_width_um = std::nullopt) const
      -> WireCapacitanceProfile;
  auto queryRequiredClockTimingWireCapacitanceProfile(int routing_layer, double length_um, std::optional<double> wire_width_um = std::nullopt) const
      -> WireCapacitanceProfile;
  auto queryConfiguredClockRouteSegmentRc(const Config& config) const -> ClockRouteSegmentRc;
  auto queryConfiguredSignalRoutingAuthority() const -> WrapperSignalRoutingAuthority;
  auto querySignalRoutingAuthority(std::string_view anchor_name) const -> WrapperSignalRoutingAuthority;
  static auto deriveSignalRoutingLayer(idb::IdbLayerRouting& layer, int32_t dbu_per_um) -> std::optional<WrapperSignalRoutingLayer>;
  auto queryCellOutPinCapLimit(const std::string& cell_master) const -> std::optional<double>;
  auto queryCellOutPinCapTableAxisMax(const std::string& cell_master) const -> std::optional<double>;
  auto queryClockSourceDriveCapLimit(const ClockSourceDriveCapLimitInput& input) const -> std::optional<double>;
  auto queryClockSourceDriveCapLimit(const Config& config, const Pin* clock_source) const -> std::optional<double>;
  auto queryCellInPinSlewLimit(const std::string& cell_master) const -> std::optional<double>;
  auto queryCellInPinSlewTableAxisMax(const std::string& cell_master) const -> std::optional<double>;
  auto queryCellHeightUm(const std::string& cell_master) const -> std::optional<double>;
  auto queryCellAreaUm2(const std::string& cell_master) const -> std::optional<double>;
  auto queryCharInputPinCap(const std::string& cell_master) const -> std::optional<double>;
  auto queryPinCapacitance(const Pin* pin) const -> std::optional<double>;
  auto queryPinCapacitance(const Pin* pin, bool early, WrapperTimingTransition transition) const -> std::optional<double>;
  auto queryPinSlewLimit(const PinSlewLimitInput& input) const -> std::optional<double>;
  auto queryPinSlewLimit(const Config& config, const Pin* pin) const -> std::optional<double>;
  auto queryRootDriverCostDirect(const std::string& cell_master, double input_slew_ns, double output_load_pf, double clock_period_ns) const -> RootDriverCost;
  auto queryBufferPorts(const std::string& cell_master) const -> std::optional<BufferPorts>;
  auto findLibertyCell(const std::string& cell_master) const -> idb::LibCell*;

  // Setter
  auto set_idb_design(idb::IdbDesign* design) -> void { _idb_design = design; }
  auto set_idb_layout(idb::IdbLayout* layout) -> void { _idb_layout = layout; }

  // Interface
  auto read(Design& design) -> void;
  auto readClocks(Design& design, const std::vector<std::pair<std::string, std::string>>& clock_net_pairs) -> bool;
  auto readTraceClockTargets(Design& design, const std::vector<ClockTraceClockTarget>& clock_targets) -> bool;
  auto traceSdcClocks(const SdcClockTraceInput& input) const -> ClockTraceBuild;
  auto writeClock(Design& design, Clock& clock) -> bool;
  auto writeClocksDetailed(Design& design, const std::vector<Clock*>& clocks) -> WrapperWriteSummary;
  auto writeClocks(Design& design, const std::vector<Clock*>& clocks) -> bool;
  auto collectLogicCellGeometries() const -> std::vector<WrapperCellGeometry>;
  auto queryInstGeometry(const std::string& inst_name) const -> std::optional<WrapperCellGeometry>;
  auto collectTimingGraph() const -> WrapperTimingGraph;
  auto withinCore(int32_t point_x, int32_t point_y) const -> std::optional<bool>;

 private:
  class CtsClockReader;
  class CtsClockIdbWriter;
  friend class CtsClockReader;
  friend class CtsClockIdbWriter;

  // DB to CTS
  static auto idbToCts(idb::IdbCoordinate<int32_t>& coord) -> Point<int>;

  // CTS to DB
  static auto ctsToIdb(const Point<int>& loc) -> idb::IdbCoordinate<int32_t>;

  // cross reference
  auto crossRef(idb::IdbPin* idb_pin, Pin* cts_pin) -> void
  {
    _idb2cts_pin_map[idb_pin] = cts_pin;
    _cts2idb_pin_map[cts_pin] = idb_pin;
  }
  auto crossRef(idb::IdbInstance* idb_inst, Inst* cts_inst) -> void
  {
    _idb2cts_inst_map[idb_inst] = cts_inst;
    _cts2idb_inst_map[cts_inst] = idb_inst;
  }
  auto crossRef(idb::IdbNet* idb_net, Net* cts_net) -> void
  {
    _idb2cts_net_map[idb_net] = cts_net;
    _cts2idb_net_map[cts_net] = idb_net;
  }

  auto loadLibertyIfNeeded() const -> void;

  idb::IdbBuilder* _idb = nullptr;
  idb::IdbDesign* _idb_design = nullptr;
  idb::IdbLayout* _idb_layout = nullptr;
  mutable bool _liberty_loaded = false;
  mutable std::uint64_t _liberty_revision = 0U;
  mutable std::shared_ptr<const idm::RawLibertyGeneration> _liberty_generation;
  mutable std::unordered_map<std::string, idb::LibCell*> _lib_cell_by_master;

  std::unordered_map<Inst*, idb::IdbInstance*> _cts2idb_inst_map;
  std::unordered_map<idb::IdbInstance*, Inst*> _idb2cts_inst_map;

  std::unordered_map<Net*, idb::IdbNet*> _cts2idb_net_map;
  std::unordered_map<idb::IdbNet*, Net*> _idb2cts_net_map;

  std::unordered_map<Pin*, idb::IdbPin*> _cts2idb_pin_map;
  std::unordered_map<idb::IdbPin*, Pin*> _idb2cts_pin_map;
};

}  // namespace icts
