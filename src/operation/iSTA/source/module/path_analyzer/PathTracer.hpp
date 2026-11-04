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
#include <string>
#include <string_view>
#include <vector>

#include "graph/TimingStateQuery.hpp"
#include "path/PathEnd.hpp"

namespace ista {
// Materialize a selected path and its timing detail using the same check context.
class PathTracer
{
 public:
  explicit PathTracer(Database& database) : _database(database), _query(database) {}
  TimingPath buildTimingPath(const std::string& end_point, AnalysisType analysis_type, PathSourceType source_type, const PathEnd& path_end);
  void buildPathTrace(const std::string& end_point, AnalysisType analysis_type, PathSourceType source_type, const TimingPathState& end_path_state,
                      std::vector<std::string>& path_pin_name_list, std::vector<TransType>& path_trans_type_list, std::vector<std::string>& path_state_tag_list,
                      std::vector<std::size_t>& path_arc_idx_list);

 private:
  Database& _database;
  TimingStateQuery _query;

  void updatePathDelay(TimingPath& timing_path, Arc* arc, double arc_delay);
  void updateClockInfo(TimingPath& timing_path, const TimingPathState& end_path_state, const PathCheck& check);
  TimingPathPoint makeTimingPathPoint(const std::string& pin_name, Arc* arc, AnalysisType analysis_type, PathSourceType source_type, TransType input_trans_type,
                                      TransType trans_type, const std::string& path_state_tag);
};
}  // namespace ista
