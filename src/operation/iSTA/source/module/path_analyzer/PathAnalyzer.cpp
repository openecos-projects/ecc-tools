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
// WHETHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
// MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
//
// See the Mulan PSL v2 for more details.
// ***************************************************************************************
#include "PathAnalyzer.hpp"

#include <string>

#include "PathChecker.hpp"
#include "PathGroupBuilder.hpp"
#include "PathSearcher.hpp"
namespace ista {
void PathAnalyzer::analyze()
{
  const ExceptionThroughIndex through_index(_database.sdc(), _database.network());
  PathSearcher search(_database, _check, through_index);
  PathGroupBuilder groups(_database);
  for (std::string& end_point : _database.graph().endpoints()) {
    auto paths = search.findPathEnds(end_point);
    groups.addPathEnd(paths);
  }
  groups.publish();
}
}  // namespace ista
