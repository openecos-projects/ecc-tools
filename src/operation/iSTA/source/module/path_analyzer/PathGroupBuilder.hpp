// SPDX-License-Identifier: MulanPSL-2.0
#pragma once

#include <map>
#include <string>
#include <vector>

#include "Database.hpp"

namespace ista {
// Owns completed endpoint paths and their summary until publication. Candidate
// order is supplied by PathSearcher; ranking and report quotas belong to the reporter.
class PathGroupBuilder
{
 public:
  explicit PathGroupBuilder(Database& database);
  void addPathEnd(std::vector<TimingPath>& paths);
  void publish();

 private:
  std::string groupName(TimingPath& path);
  Database& _database;
  std::map<std::string, PathGroup> _groups;
  PathStats _summary;
};
}  // namespace ista
