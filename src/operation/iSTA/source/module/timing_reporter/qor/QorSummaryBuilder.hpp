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

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "Database.hpp"
#include "QorSummary.hpp"
#include "ReportOptions.hpp"
#include "ReportPathContext.hpp"

namespace ista {
class ReportPathSelector;
class QorSummaryBuilder
{
 public:
  QorSummary build(ReportPathSelector& selector);
  explicit QorSummaryBuilder(Database& database) : _database(database), _report_path_context(database) {}
  int32_t getFanoutViolationNum();
  std::vector<std::string> getQorSortedGroupList(std::map<std::string, double>& value_map);
  double getQorFrequency(TimingPath& timing_path);
  int32_t getQorCellArea();
  int32_t getQorLeafCellK();

 private:
  Database& _database;
  ReportPathContext _report_path_context;
};
}  // namespace ista
