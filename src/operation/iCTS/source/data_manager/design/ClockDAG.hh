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
 * @file ClockDAG.hh
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-05-06
 * @brief Design-owned read-only clock DAG projection for committed CTS topology
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace icts {

class Clock;
class Net;
class Pin;

enum class ClockGraphIssueCode
{
  kNullClock,
  kMissingClockSource,
  kMissingNet,
  kMissingNetDriver,
  kMissingNetLoad,
  kDuplicateNetMembership,
  kForeignNetMembership,
  kDuplicatePinMembership,
  kForeignPinMembership,
  kIncompletePropagationArc,
  kPropagationPinInstMismatch,
  kInvalidPinDirection,
  kPhysicalKindMismatch,
  kAmbiguousCrossClockOwnership,
  kUnreachableDeclaredSink,
  kGraphCycle,
};

struct ClockGraphIssue
{
  ClockGraphIssueCode code = ClockGraphIssueCode::kNullClock;
  std::string clock_name;
  std::string clock_net_name;
  std::string object_name;
  std::string inst_name;
  std::string cell_master;
  std::string input_pin_name;
  std::string output_pin_name;
  std::string input_net_name;
  std::string output_net_name;
  std::string propagation_kind;
  std::string propagation_origin;
  std::string expected;
  std::string observed;
  std::string invariant;
  std::string message;
};

auto ClockGraphIssueCodeName(ClockGraphIssueCode code) -> const char*;
auto FormatClockGraphIssue(const ClockGraphIssue& issue) -> std::string;

class ClockDAG
{
 public:
  struct PathBufferStats
  {
    bool topology_valid = false;
    bool available = false;
    bool has_ff_sink_terminal = false;
    int32_t min_buffer_count = 0;
    int32_t max_buffer_count = 0;
    std::size_t ff_sink_terminal_count = 0U;
    std::size_t terminal_probe_count = 0U;
    std::string status = "unavailable";
  };

  struct BuildWorkStats
  {
    std::size_t ready_push_count = 0U;
    std::size_t ready_pop_count = 0U;
    std::size_t arc_relaxation_count = 0U;
  };

  ClockDAG() = default;
  ~ClockDAG() = default;

  auto rebuild(const std::vector<Clock*>& clocks) -> bool;
  auto clear() -> void;
  auto invalidate(const std::string& reason) -> void;

  auto is_built() const -> bool { return _built; }
  auto is_valid() const -> bool { return _built && _valid; }
  auto get_status() const -> const std::string& { return _status; }
  auto get_issues() const -> const std::vector<ClockGraphIssue>& { return _issues; }
  auto primaryIssue() const -> const ClockGraphIssue* { return _issues.empty() ? nullptr : &_issues.front(); }

  auto hasCycle(const Clock* clock) const -> bool;
  auto topologicalPins(const Clock* clock) const -> std::vector<Pin*>;
  auto reachablePins(const Clock* clock) const -> std::vector<Pin*>;
  auto reachablePinsFrom(const Clock* clock, Pin* start_pin) const -> std::vector<Pin*>;
  auto reachableNets(const Clock* clock) const -> std::vector<Net*>;
  auto pathBufferStats(const Clock* clock) const -> PathBufferStats;
  auto pathBufferStats() const -> PathBufferStats;

  struct Arc
  {
    Pin* from = nullptr;
    Pin* to = nullptr;
    Net* net = nullptr;
    int32_t path_buffer_weight = 0;
  };

  struct ClockGraph
  {
    const Clock* clock = nullptr;
    bool valid = true;
    bool has_cycle = false;
    std::string status = "valid";
    std::vector<ClockGraphIssue> issues;
    std::vector<Pin*> pins;
    std::vector<Net*> nets;
    std::unordered_set<const Pin*> pin_set;
    std::unordered_set<const Net*> net_set;
    std::unordered_map<const Pin*, std::vector<Arc>> outgoing_arcs;
    std::vector<Pin*> topological_pins;
    BuildWorkStats build_work;
  };

  auto graphForClock(const Clock* clock) const -> const ClockGraph*;

 private:
  auto findGraph(const Clock* clock) const -> const ClockGraph*;

  bool _built = false;
  bool _valid = false;
  std::string _status = "not_built";
  std::vector<ClockGraphIssue> _issues;
  std::vector<const Clock*> _clock_order;
  std::unordered_map<const Clock*, ClockGraph> _graphs_by_clock;
};

}  // namespace icts
