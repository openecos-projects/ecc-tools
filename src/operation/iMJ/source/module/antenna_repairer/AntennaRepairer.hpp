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

#include <any>
#include <map>
#include <string>

#include "ARModel.hpp"

namespace imj {

#define MJAR (imj::AntennaRepairer::getInst())

class AntennaRepairer
{
 public:
  static void initInst();
  static AntennaRepairer& getInst();
  static void destroyInst();

  ARSummary repair(std::map<std::string, std::any> config_map);

 private:
  static AntennaRepairer* _ar_instance;

  AntennaRepairer() = default;
  AntennaRepairer(const AntennaRepairer& other) = delete;
  AntennaRepairer(AntennaRepairer&& other) = delete;
  ~AntennaRepairer() = default;

  AntennaRepairer& operator=(const AntennaRepairer& other) = delete;
  AntennaRepairer& operator=(AntennaRepairer&& other) = delete;
};

}  // namespace imj
