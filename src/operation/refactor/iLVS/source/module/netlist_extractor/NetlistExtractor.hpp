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

namespace idb {
class IdbDesign;
class IdbPin;
}  // namespace idb

namespace ilvs {

#define LVSNE (ilvs::NetlistExtractor::getInst())

class NetlistExtractor
{
 public:
  static void initInst();
  static NetlistExtractor& getInst();
  static void destroyInst();
  // function
#if 1  // extract
  Netlist extract(idb::IdbDesign* design);
  Netlist extractLogical(idb::IdbDesign* design);
  Netlist extractPhysical(idb::IdbDesign* design);
#endif

 private:
  // self
  static NetlistExtractor* _ne_instance;

  NetlistExtractor() = default;
  NetlistExtractor(const NetlistExtractor& other) = delete;
  NetlistExtractor(NetlistExtractor&& other) = delete;
  ~NetlistExtractor() = default;
  NetlistExtractor& operator=(const NetlistExtractor& other) = delete;
  NetlistExtractor& operator=(NetlistExtractor&& other) = delete;
  // function
#if 1  // extract
  Netlist extractNetlist(idb::IdbDesign* design, bool build_logical_graph, bool build_physical_graph);
  std::string getTerminalName(idb::IdbPin* pin);
#endif
};

}  // namespace ilvs
