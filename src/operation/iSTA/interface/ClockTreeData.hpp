// SPDX-License-Identifier: MulanPSL-2.0
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace ista {

// Owned clock-network values exported from a completed STA session. Timing is
// reported in nanoseconds relative to each clock's selected physical root.
struct ClockTreeTimingStateData
{
  std::string analysis;
  std::string source_edge;
  std::string local_edge;
  std::string predecessor;
  std::optional<double> arrival_ns;
  std::optional<double> slew_ns;
};

struct ClockTreeNodeData
{
  std::string name;
  std::string instance_name;
  std::string pin_name;
  std::string master;
  std::string kind;
  std::string cell_role;
  std::int32_t x_dbu = 0;
  std::int32_t y_dbu = 0;
  bool position_valid = false;
  bool sink = false;
  std::vector<ClockTreeTimingStateData> timing_states;
};

struct ClockTreeEdgeData
{
  std::string from;
  std::string to;
  bool cell_arc = false;
  bool negative_unate = false;
  std::string net_name;
  std::string cell_master;
};

struct ClockTreeClockData
{
  std::string name;
  std::string master_clock_name;
  std::string source_edge = "rise";
  bool timing_available = false;
  std::vector<std::string> roots;
  std::vector<ClockTreeNodeData> nodes;
  std::vector<ClockTreeEdgeData> edges;
  std::vector<std::string> diagnostics;
};

struct ClockTreeGraphData
{
  std::vector<ClockTreeClockData> clocks;
  std::vector<std::string> diagnostics;
};

}  // namespace ista
