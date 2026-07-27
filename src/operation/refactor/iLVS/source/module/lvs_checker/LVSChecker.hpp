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

#include "Config.hpp"
#include "DataManager.hpp"
#include "Database.hpp"
#include "Logger.hpp"
#include "Monitor.hpp"

namespace ilvs {

#define LVSLC (ilvs::LVSChecker::getInst())

class LVSChecker
{
 public:
  static void initInst();
  static LVSChecker& getInst();
  static void destroyInst();
  // function
#if 1  // check
  CheckResult check(const Netlist& netlist, const Netlist& def);
#endif

 private:
  // self
  static LVSChecker* _lc_instance;

  LVSChecker() = default;
  LVSChecker(const LVSChecker& other) = delete;
  LVSChecker(LVSChecker&& other) = delete;
  ~LVSChecker() = default;
  LVSChecker& operator=(const LVSChecker& other) = delete;
  LVSChecker& operator=(LVSChecker&& other) = delete;
};

}  // namespace ilvs
