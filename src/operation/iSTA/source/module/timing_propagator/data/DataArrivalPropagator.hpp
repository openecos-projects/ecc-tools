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

#include "Database.hpp"
#include "graph/TimingStateQuery.hpp"
#include "sdc/ExceptionMatcher.hpp"

namespace ista {
class DataArrivalPropagator
{
 public:
  explicit DataArrivalPropagator(Database& database) : _database(database), _path_query(database) {}
  void propagate();

 private:
  void propagateArrivalArc(std::size_t arc_idx);
  void propagatePathStateArc(std::size_t arc_idx, AnalysisType analysis_type, PathSourceType source_type);
  void propagatePathStateArc(std::size_t arc_idx, AnalysisType analysis_type, PathSourceType source_type, TransType input_trans_type);
  void propagatePathStateArc(std::size_t arc_idx, AnalysisType analysis_type, PathSourceType source_type, TransType input_trans_type,
                             TransType output_trans_type);
  Database& _database;
  TimingStateQuery _path_query;
  ExceptionThroughIndex _through_index;
};
}  // namespace ista
