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

#include <string>
#include <vector>

#include "Config.hpp"
#include "DataManager.hpp"
#include "Database.hpp"
#include "Logger.hpp"
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
#if 1  // report
  std::vector<fort::char_table> getSummaryTableList(const CheckResult& check_result, const Netlist& netlist,
                                                     const Netlist& def);

  void report(const CheckResult& check_result, const Netlist& netlist, const Netlist& def,
              const std::string& report_directory_path);
#endif

 private:
  // self
  static LVSReporter* _lr_instance;

  LVSReporter() = default;
  LVSReporter(const LVSReporter& other) = delete;
  LVSReporter(LVSReporter&& other) = delete;
  ~LVSReporter() = default;
  LVSReporter& operator=(const LVSReporter& other) = delete;
  LVSReporter& operator=(LVSReporter&& other) = delete;
};

}  // namespace ilvs
