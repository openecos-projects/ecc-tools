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
// EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
// MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE.
//
// See the Mulan PSL v2 for more details.
// ***************************************************************************************
#pragma once

#include "Config.hpp"
#include "ConnectType.hpp"
#include "DataManager.hpp"
#include "Database.hpp"
#include "LRModel.hpp"
#include "LVSConnectivitySummaryRow.hpp"
#include "LVSEntitySummaryRow.hpp"
#include "LVSHeader.hpp"
#include "Monitor.hpp"

namespace ilvs {

#define LVSLR (ilvs::LVSReporter::getInst())

class LVSReporter
{
 public:
  static void initInst();
  static LVSReporter& getInst();
  static void destroyInst();
  // function
  void report();

 private:
  // self
  static LVSReporter* _lr_instance;

  LVSReporter() = default;
  LVSReporter(const LVSReporter& other) = delete;
  LVSReporter(LVSReporter&& other) = delete;
  ~LVSReporter() = default;
  LVSReporter& operator=(const LVSReporter& other) = delete;
  LVSReporter& operator=(LVSReporter&& other) = delete;
  // function

  LRModel initLRModel();
  std::vector<fort::char_table> getSummaryTableList();
  std::vector<LVSEntitySummaryRow> getEntitySummaryRowList();
  std::vector<LVSConnectivitySummaryRow> getConnectivitySummaryRowList();
  void addConnectivitySummaryRow(std::vector<LVSConnectivitySummaryRow>& row_list, const std::string& connectivity, int64_t open_num, int64_t short_num,
                                 int64_t connected_num, int64_t total_num);
  std::string getCountPercentageText(int64_t count, int64_t total_count);
  double getPercentage(int64_t count, int64_t total_count);
  std::set<std::string> getPowerOpenTerminalNameSet(ConnectType connect_type);
  std::set<std::string> getPowerShortTerminalNameSet(ConnectType connect_type);
  std::vector<const Violation*> getViolationList();
  void outputRPT(const LRModel& lr_model, const std::vector<fort::char_table>& summary_table_list, const std::vector<const Violation*>& violation_list);
  std::string getJoinedString(const std::vector<int32_t>& value_list);
  std::string getJoinedString(const std::vector<std::string>& value_list);
  void outputJson(const LRModel& lr_model, const std::vector<const Violation*>& violation_list);
  void printSummary(const std::vector<fort::char_table>& summary_table_list);
};

}  // namespace ilvs
