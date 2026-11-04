// SPDX-License-Identifier: MulanPSL-2.0
#pragma once
#include <cstddef>
#include <string>

namespace ista {
// Statistics for the paths processed in one PathGroupBuilder pass. Negative-path
// count and summed negative slack preserve the existing path-based accounting.
struct PathStats
{
  std::size_t path_count = 0;
  std::size_t checked_endpoint_count = 0;
  std::size_t unconstrained_endpoint_count = 0;
  std::size_t violating_path_count = 0;
  double worst_slack = 0;
  double total_negative_slack = 0;
  std::string worst_endpoint;
};
}  // namespace ista
