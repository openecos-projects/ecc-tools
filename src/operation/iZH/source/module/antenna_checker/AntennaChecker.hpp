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

#include "ACModel.hpp"
#include "AntennaResult.hpp"
#include "IdbDesign.h"
#include "IdbNet.h"
#include "Logger.hpp"
#include "Monitor.hpp"

namespace izh {

#define ZHAC (izh::AntennaChecker::getInst())

class AntennaChecker
{
 public:
  static void initInst();
  static AntennaChecker& getInst();
  static void destroyInst();

  void check(std::map<std::string, std::any> config_map);
  void checkNets(ACModel& ac_model, const std::vector<idb::IdbNet*>& net_list);
  AntennaResult checkToResult(std::map<std::string, std::any> config_map, bool write_report = false);

 private:
  static AntennaChecker* _ac_instance;

  AntennaChecker() = default;
  AntennaChecker(const AntennaChecker& other) = delete;
  AntennaChecker(AntennaChecker&& other) = delete;
  ~AntennaChecker() = default;

  AntennaChecker& operator=(const AntennaChecker& other) = delete;
  AntennaChecker& operator=(AntennaChecker&& other) = delete;

  ACModel initACModel(std::map<std::string, std::any>& config_map);
  void runACModel(ACModel& ac_model);
  void reportACModel(const ACModel& ac_model);
  void initDatabaseInfo(ACModel& ac_model);
  void writeReport(const ACModel& ac_model) const;
};

}  // namespace izh
