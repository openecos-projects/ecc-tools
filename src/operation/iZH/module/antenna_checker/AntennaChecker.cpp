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
#include "AntennaChecker.hpp"

namespace izh {

// public

void AntennaChecker::initInst()
{
  if (_ac_instance == nullptr) {
    _ac_instance = new AntennaChecker();
  }
}

AntennaChecker& AntennaChecker::getInst()
{
  if (_ac_instance == nullptr) {
    ZHLOG.error(Loc::current(), "The instance not initialized!");
  }
  return *_ac_instance;
}

void AntennaChecker::destroyInst()
{
  if (_ac_instance != nullptr) {
    delete _ac_instance;
    _ac_instance = nullptr;
  }
}

// function

void AntennaChecker::check(std::map<std::string, std::any> config_map)
{
  Monitor monitor;
  ZHLOG.info(Loc::current(), "Starting...");

  ACModel ac_model = initACModel(config_map);

  ZHLOG.info(Loc::current(), "ZH checkAntenna");
  ZHLOG.info(Loc::current(), "Found ", ac_model.get_violation_num(), " antenna violations");

  ZHLOG.info(Loc::current(), "Completed", monitor.getStatsInfo());
}

// private

ACModel AntennaChecker::initACModel(std::map<std::string, std::any>& config_map)
{
  ACModel ac_model;
  if (!config_map.empty()) {
    ZHLOG.warn(Loc::current(), "The checkAntenna config has not been consumed yet!");
  }
  return ac_model;
}

AntennaChecker* AntennaChecker::_ac_instance = nullptr;

}  // namespace izh
