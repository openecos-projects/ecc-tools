// SPDX-License-Identifier: MulanPSL-2.0
#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace ista {

// IDs refer to this view until PyPlaceDB remaps its pin/state ordering.
struct SparseTimingState
{
  std::string pin;
  int32_t domain = -1;
  int32_t tag = -1;
  int32_t launch_tag = -1;
  // 0 is an electrical-only state with no arrival transitions or data seed.
  int32_t source_type = 0;
  // 0 is the load/receiver role; 1 is the separate driver role of a
  // bidirectional pin.  The Python schema keeps the physical pin identity;
  // this field is used while compiling role-specific state and RC edges.
  int32_t role = 0;
  int32_t level = 0;
  int32_t slew_group = -1;
  std::array<bool, 2> valid{false, false};
  std::array<bool, 2> seed{false, false};
  std::array<double, 4> reference{-9e7, -9e7, 0, 0};
};

struct SparseTimingEdge
{
  int32_t base_arc = -1;
  int32_t source = -1;
  int32_t sink = -1;
  // Bit (2 * input_transition + output_transition), rise=0/fall=1.
  // Arrival legality is distinct from the shared physical electrical slew.
  uint8_t transition_mask = 0;
};

enum class SparseTimingRuleKind : int32_t
{
  kNormal,
  kMaxDelay,
  kMulticycle,
  kFalsePath
};

// Required = intercept_ps + check_coefficient * LUT(clock_slew_ps, data_slew).
// The compiler uses iSTA's edge/uncertainty/exception API; Python does not
// reinterpret the SDC or derive clock phases from these rows.
struct SparseTimingCheck
{
  int32_t state = -1;
  int32_t capture_domain = -1;
  int32_t endpoint_arc = -1;  // timing_check_arcs index, -1 for an I/O check
  int32_t transition = 0;     // 0 rise, 1 fall
  int32_t constraint = -1;    // chosen path exception, -1 for normal setup
  SparseTimingRuleKind kind = SparseTimingRuleKind::kNormal;
  bool valid = false;
  double intercept_ps = 0;
  double check_coefficient = 0;
  double clock_slew_ps = 0;
  double reference_required_ps = 0;
};

struct SparseTimingSnapshot
{
  bool enabled = false;
  bool early = false;
  std::vector<std::string> domains;
  std::vector<std::string> tags;
  std::vector<std::string> launch_tags;
  std::vector<std::string> unsupported;
  std::vector<SparseTimingState> states;
  std::vector<SparseTimingEdge> cell_edges;
  std::vector<SparseTimingEdge> slew_only_cell_edges;
  std::vector<SparseTimingEdge> net_edges;
  std::vector<std::array<double, 4>> net_threshold_adjustments;
  std::vector<SparseTimingCheck> checks;
  int64_t sequential_instances = 0;
  std::vector<int64_t> clock_reached_pins;
  std::vector<int64_t> clock_reached_sequential_pins;
  std::vector<std::string> clock_pin_sample;
  std::vector<std::array<double, 2>> reference_pin_cap;
};

struct TimingData;
class SparseTimingExportAdapter
{
 public:
  static void compile(TimingData& snapshot);
  static void compileModel(TimingData& snapshot);
};

}  // namespace ista
