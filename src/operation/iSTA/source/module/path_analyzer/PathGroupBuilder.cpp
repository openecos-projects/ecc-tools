// SPDX-License-Identifier: MulanPSL-2.0
#include "PathGroupBuilder.hpp"

#include <cmath>
#include <limits>
#include <string>
#include <vector>

namespace ista {
PathGroupBuilder::PathGroupBuilder(Database& database) : _database(database)
{
  _summary.worst_slack = std::numeric_limits<double>::infinity();
}

void PathGroupBuilder::addPathEnd(std::vector<TimingPath>& paths)
{
  if (paths.empty()) {
    ++_summary.unconstrained_endpoint_count;
    return;
  }
  ++_summary.checked_endpoint_count;
  for (TimingPath& path : paths) {
    const auto name = groupName(path);
    auto& group = _groups[name];
    group.setName(name);
    auto& end = group.endpointPaths()[path.get_end_point()];
    end.push_back(path);
    // A strict comparison retains the first path on ties. TNS and the legacy
    // violating_path_count count negative paths, not distinct endpoints.
    if (path.get_slack() < _summary.worst_slack) {
      _summary.worst_slack = path.get_slack();
      _summary.worst_endpoint = path.get_end_point();
    }
    if (path.get_slack() < 0.0) {
      ++_summary.violating_path_count;
      _summary.total_negative_slack += path.get_slack();
    }
  }
}

std::string PathGroupBuilder::groupName(TimingPath& path)
{
  if (path.get_check_type() == TimingCheckType::kRecovery || path.get_check_type() == TimingCheckType::kRemoval) {
    return "**async_default**";
  }
  if (!path.get_capture_clock_name().empty()) {
    return path.get_capture_clock_name();
  }
  return "default";
}

void PathGroupBuilder::publish()
{
  if (!std::isfinite(_summary.worst_slack)) {
    _summary.worst_slack = 0.0;
  }
  // Publish in map order. TimingAnalyzer clears the previous analysis results
  // before building the graph; this builder only owns the current path groups.
  for (auto& [name, group] : _groups) {
    _database.pathGroups().push_back(group);
  }
  _summary.path_count = 0;
  for (auto& [name, group] : _groups) {
    for (auto& [end_name, end] : group.endpointPaths()) {
      _summary.path_count += end.size();
    }
  }
  _database.pathStats() = _summary;
}
}  // namespace ista
