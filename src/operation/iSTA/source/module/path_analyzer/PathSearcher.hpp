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

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include "PathChecker.hpp"
#include "PathTracer.hpp"
#include "sdc/ExceptionMatcher.hpp"

namespace ista {
// Ordered endpoint enumeration. Diversion writes still require sequential search.
class PathSearcher
{
 public:
  PathSearcher(Database& database, PathChecker& check, const ExceptionThroughIndex& through_index)
      : _database(database), _query(database), _check(check), _trace(database), _through_index(through_index)
  {
  }
  std::vector<TimingPath> findPathEnds(const std::string& end_point);

 private:
  Database& _database;
  TimingStateQuery _query;
  PathChecker& _check;
  PathTracer _trace;
  const ExceptionThroughIndex& _through_index;

  void buildPathDiversionList(const std::string& end_point);
  void buildPathDiversionList(const std::string& end_point, AnalysisType analysis_type, PathSourceType source_type);
  void buildPathDiversionList(const std::string& end_point, AnalysisType analysis_type, PathSourceType source_type,
                              std::vector<std::string>& path_pin_name_list, std::vector<TransType>& path_trans_type_list,
                              std::vector<std::size_t>& path_arc_idx_list);
  void buildPathDiversionState(AnalysisType analysis_type, PathSourceType source_type, std::vector<std::string>& path_pin_name_list,
                               std::vector<TransType>& path_trans_type_list, std::vector<std::size_t>& path_arc_idx_list, std::size_t sink_idx,
                               std::size_t diversion_arc_idx, TransType input_trans_type, const TimingPathState& source_path_state);
  bool isOutputTransType(Arc& arc, AnalysisType analysis_type, TransType input_trans_type, TransType output_trans_type);
  TimingPathState* updateDiversionPathState(const std::string& pin_name, AnalysisType analysis_type, PathSourceType source_type, TransType trans_type,
                                            const TimingPathState& source_path_state, const std::string& predecessor, std::size_t predecessor_arc_idx,
                                            double predecessor_arc_delay, TransType predecessor_trans_type, double arrival);
  std::optional<PathEnd> findWorstPathEnd(const std::string& end_point, AnalysisType analysis_type, PathSourceType source_type);
};
}  // namespace ista
