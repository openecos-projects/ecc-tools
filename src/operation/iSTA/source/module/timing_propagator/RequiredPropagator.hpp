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
#pragma once

#include <string>
#include <unordered_map>

#include "PathChecker.hpp"
#include "sdc/ExceptionMatcher.hpp"

namespace ista {
// Propagate required times for matching path identities, then publish one
// consistent worst-slack arrival/required pair at each graph point.
class RequiredPropagator
{
 public:
  RequiredPropagator(Database& database, PathChecker& check) : _database(database), _query(database), _check(check) {}
  void propagate();

 private:
  Database& _database;
  TimingStateQuery _query;
  PathChecker& _check;
  ExceptionThroughIndex _through_index;
  std::unordered_map<const TimingPathState*, double> _required;
  std::unordered_map<const TimingPathState*, double> _setup_checks;

  void propagateCaptureRequired(double required_time);
  double resolveRequiredTime();
  void seedEndPointRequired(double required_time);
  void propagateRequiredArc(Arc& arc);
  void updateSlack();
};
}  // namespace ista
