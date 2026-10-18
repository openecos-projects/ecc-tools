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
// MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
//
// See the Mulan PSL v2 for more details.
// ***************************************************************************************
#pragma once

#include "DAModel.hpp"

namespace imj {

#define MJDA (imj::DriverAnalyzer::getInst())

class DriverAnalyzer
{
 public:
  static void initInst();
  static DriverAnalyzer& getInst();
  static void destroyInst();
  // function
  void analyze();

 private:
  static DriverAnalyzer* _da_instance;

  DriverAnalyzer() = default;
  DriverAnalyzer(const DriverAnalyzer& other) = delete;
  DriverAnalyzer(DriverAnalyzer&& other) = delete;
  ~DriverAnalyzer() = default;
  DriverAnalyzer& operator=(const DriverAnalyzer& other) = delete;
  DriverAnalyzer& operator=(DriverAnalyzer&& other) = delete;
  // function
  DAModel initDAModel();
  void excludeIOBoundaryNetList(DAModel& da_model);
  bool isIOBoundaryNet(DANet& net);
  void buildGraph(DAModel& da_model);
  void inferPinTypeList(DAModel& da_model);
  void updateNetState(DAModel& da_model, DANet& net);
  void analyzeNetList(DAModel& da_model);
#if 1  // exhibit
  void writeReport(DAModel& da_model);
#endif
};

}  // namespace imj
