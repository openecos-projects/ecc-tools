// SPDX-License-Identifier: MulanPSL-2.0
#pragma once
#include <functional>
#include <optional>
#include <string>
#include <utility>

#include "graph/TimingPathState.hpp"
#include "liberty/TimingCheckType.hpp"

namespace ista {
// Exact endpoint evaluation for one propagated path and capture choice.
// Values are resolved once by PathChecker and travel with the winning path.
struct PathCheck
{
  double required = 0.0;
  double required_before_round = 0.0;
  TimingCheckType type = TimingCheckType::kNone;
  double check_time = 0.0;
  double uncertainty = 0.0;
  std::string capture_clock;
  TransType capture_transition = TransType::kRise;      // Source clock edge for constraints.
  TransType capture_pin_transition = TransType::kRise;  // Local Liberty/trace edge.
  std::optional<std::size_t> capture_edge_index;
  std::string capture_pin;
  double capture_time = 0.0;
  double source_latency = 0.0;
  double network_delay = 0.0;
  double reconvergence = 0.0;
  std::string common_pin;
  std::optional<double> output_delay;
  bool level_sensitive = false;
  // Preserve the second rounding step of a resolved multicycle adjustment.
  bool has_required_adjustment = false;
  double required_adjustment = 0.0;
  // The selected Liberty check-arc variant at this base point.  A single
  // TimingCheckArc can contain multiple conditional/table variants; retaining
  // only the scalar check value is insufficient for sparse export because
  // the exporter must bind the same LUT identity.
  int32_t selected_check_arc_idx = -1;
};

// Borrows a propagated path only during synchronous PathSearcher.
class PathEnd
{
 public:
  PathEnd(std::reference_wrapper<const TimingPathState> path, PathCheck check, double required, double slack)
      : _path(path), _check(std::move(check)), _required(required), _slack(slack)
  {
  }
  const TimingPathState& path() const { return _path.get(); }
  const PathCheck& check() const { return _check; }
  const std::string& captureClock() const { return _check.capture_clock; }
  double normalRequired() const { return _check.required; }
  double required() const { return _required; }
  double slack() const { return _slack; }

 private:
  std::reference_wrapper<const TimingPathState> _path;
  PathCheck _check;
  double _required;
  double _slack;
};
}  // namespace ista
