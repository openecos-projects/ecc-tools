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
#include "DataManager.hpp"
#include "Database.hpp"
#include "ECModel.hpp"
#include "LVSHeader.hpp"
#include "Monitor.hpp"

namespace ilvs {

#define LVSEC (ilvs::EntityChecker::getInst())

class EntityChecker
{
 public:
  static void initInst();
  static EntityChecker& getInst();
  static void destroyInst();
  // function
  void check();

 private:
  // self
  static EntityChecker* _ec_instance;

  EntityChecker() = default;
  EntityChecker(const EntityChecker& other) = delete;
  EntityChecker(EntityChecker&& other) = delete;
  ~EntityChecker() = default;
  EntityChecker& operator=(const EntityChecker& other) = delete;
  EntityChecker& operator=(EntityChecker&& other) = delete;

  ECModel initECModel();
  std::vector<std::string> getComparedIONameList(const DesignData& design_data);
  bool isPowerGroundIO(const DesignData& design_data, const std::string& io_terminal_name);
  void checkIO(ECModel& ec_model);
  std::vector<std::string> getDifference(const std::vector<std::string>& first_name_list,
                                         const std::vector<std::string>& second_name_list);
  void checkInstance(ECModel& ec_model);
  void checkNet(ECModel& ec_model);
};

}  // namespace ilvs
