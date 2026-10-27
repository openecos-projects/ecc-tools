// SPDX-License-Identifier: MulanPSL-2.0
#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

#include "Database.hpp"
#include "PathGroupBuilder.hpp"

void require(bool value, const char* message)
{
  if (!value)
    throw std::runtime_error(message);
}
ista::TimingPath path(const std::string& end, double slack, const std::string& clock = "")
{
  ista::TimingPath result;
  result.set_end_point(end);
  result.set_slack(slack);
  result.set_capture_clock_name(clock);
  return result;
}
int main()
{
  ista::Database database;
  ista::PathGroupBuilder groups(database);
  std::vector<ista::TimingPath> empty;
  groups.addPathEnd(empty);
  std::vector<ista::TimingPath> first{path("z", -2.0, "clk"), path("z", -1.0, "clk")};
  groups.addPathEnd(first);
  std::vector<ista::TimingPath> second{path("a", -2.0)};
  groups.addPathEnd(second);
  auto recovery = path("reset", 1.0, "clk");
  recovery.set_check_type(ista::TimingCheckType::kRecovery);
  std::vector<ista::TimingPath> asynchronous{recovery};
  groups.addPathEnd(asynchronous);
  require(database.pathGroups().empty(), "grouping published too early");
  groups.publish();
  auto& summary = database.pathStats();
  require(summary.path_count == 4 && summary.checked_endpoint_count == 3 && summary.unconstrained_endpoint_count == 1, "endpoint/path accounting changed");
  require(summary.violating_path_count == 3 && summary.total_negative_slack == -5.0 && summary.worst_slack == -2.0 && summary.worst_endpoint == "z",
          "negative-path accounting or first-winner tie changed");
  auto& list = database.pathGroups();
  require(list.size() == 3 && list[0].name() == "**async_default**" && list[1].name() == "clk" && list[2].name() == "default",
          "group publication order changed");
  auto& paths = list[1].endpointPaths().at("z");
  require(paths.size() == 2 && paths[0].get_slack() == -2.0 && paths[1].get_slack() == -1.0, "candidate order changed");
  ista::PathGroupBuilder next(database);
  next.addPathEnd(empty);
  next.publish();
  require(list.size() == 3 && summary.path_count == 0 && summary.worst_slack == 0.0, "append behavior or empty summary fallback changed");
  ista::PathGroupBuilder nonfinite(database);
  std::vector<ista::TimingPath> nan{path("nan", std::numeric_limits<double>::quiet_NaN())};
  nonfinite.addPathEnd(nan);
  nonfinite.publish();
  require(summary.worst_slack == 0.0 && summary.violating_path_count == 0 && list.size() == 4, "nonfinite fallback or append behavior changed");
  ista::Database clock_database;
  clock_database.sdc().get_clock_map()["fallback"] = ista::TimingClock();
  ista::PathGroupBuilder clock_groups(clock_database);
  clock_groups.addPathEnd(second);
  clock_groups.publish();
  require(clock_database.pathGroups().front().name() == "default", "clockless path assigned to unrelated clock group");
  ista::PathGroupBuilder explicit_clock(clock_database);
  std::vector<ista::TimingPath> constrained{path("clocked", -1.0, "fallback")};
  explicit_clock.addPathEnd(constrained);
  explicit_clock.publish();
  require(clock_database.pathGroups().back().name() == "fallback", "explicit capture clock group lost");
}
