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

#if 1  // 前向声明

namespace idb {
class IdbDesign;
}  // namespace idb

#endif

namespace ilvs {

#define LVSI (ilvs::LVSInterface::getInst())

class LVSInterface
{
 public:
  static LVSInterface& getInst();
  static void destroyInst();

#if 1  // 外部调用LVS的API

#if 1  // iLVS
  void initLVS(std::map<std::string, std::any> config_map);
  void runLVS();
  void destroyLVS();
  void writeNetlist(const std::string& file_path);
  void writeDef(const std::string& file_path);
  void readSnapshots(const std::string& netlist_file_path, const std::string& def_file_path);
#endif

#endif

#if 1  // LVS调用外部的API

#if 1  // TopData

#if 1  // input
  void input(std::map<std::string, std::any>& config_map);
  void wrapConfig(std::map<std::string, std::any>& config_map);
  void wrapDatabase();
#endif

#if 1  // output
  void output();
#endif

#endif

#endif

 private:
  static LVSInterface* _lvs_interface_instance;

  LVSInterface() = default;
  LVSInterface(const LVSInterface& other) = delete;
  LVSInterface(LVSInterface&& other) = delete;
  ~LVSInterface() = default;
  LVSInterface& operator=(const LVSInterface& other) = delete;
  LVSInterface& operator=(LVSInterface&& other) = delete;
};

}  // namespace ilvs
