// SPDX-License-Identifier: MulanPSL-2.0
// Copyright (c) 2026 Beijing Institute of Open Source Chip
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace imj {

// Lightweight result data owned by the EccDB native antenna adaptor. It has
// no geometry or database dependency so the graph kernel can return its
// counters without depending on the legacy iDB checker.
struct AntennaRunSummary
{
  bool checked = false;
  bool has_rules = false;
  bool coverage_complete = false;
  uint64_t signal_net_count = 0;
  uint64_t connected_net_count = 0;
  uint64_t wire_count = 0;
  uint64_t violation_count = 0;
  uint64_t rule_layer_count = 0;
  uint64_t covered_rule_layer_count = 0;
  uint64_t missing_rule_layer_count = 0;
  uint64_t pins_missing_antenna_info = 0;
  uint64_t pins_with_gate_area = 0;
  uint64_t components_without_gate = 0;
  uint64_t skipped_segments = 0;
  uint64_t conductors_out_of_range = 0;
  uint64_t partial_areas_dropped = 0;
  uint64_t via_geometry_count = 0;
  std::vector<std::string> violations;
  std::vector<std::string> coverage_diagnostics;
};

}  // namespace imj
