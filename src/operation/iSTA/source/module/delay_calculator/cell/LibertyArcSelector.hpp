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
#include <vector>

#include "Database.hpp"

namespace ista {
class LibertyArcSelector
{
 public:
  LibertyArcSelector(Database& database) : _database(database) {}
  LibertyArcSelector(const LibertyArcSelector&) = delete;
  LibertyArcSelector& operator=(const LibertyArcSelector&) = delete;
  bool isClockArcTriggerTrans(TimingCellArc& timing_cell_arc, TransType input_trans_type);
  TimingCellArc* cellArc(Arc& arc);
  TransType getOutputTransType(TimingCellArc& timing_cell_arc, TransType input_trans_type);
  std::vector<TransType> getOutputTransTypeList(TimingCellArc& timing_cell_arc, TransType input_trans_type);
  std::vector<TimingArc*> cellCandidates(TimingCellArc& timing_cell_arc, TransType input_trans_type, TransType output_trans_type,
                                         const std::string& output_pin = "");
  std::vector<TimingArc*> checkCandidates(TimingCheckArc& timing_check_arc, TransType clock_trans_type, TransType data_trans_type);
  bool isMatchTimingType(TimingArc& timing_arc, TransType trans_type);
  bool isMatchTimingType(TimingCellArc& timing_cell_arc, TransType trans_type);

 private:
  bool isPositiveArc(TimingArc& timing_arc);
  bool isNegativeArc(TimingArc& timing_arc);
  bool isUnateArc(TimingCellArc& timing_cell_arc);
  bool isNegativeArc(TimingCellArc& timing_cell_arc);
  bool isTwoTypeSenseArcSet(TimingCellArc& timing_cell_arc);
  Database& _database;
};
}  // namespace ista
