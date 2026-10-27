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

namespace spef {
struct ConnEntry;
struct Net;
struct ResCap;
}  // namespace spef

namespace ista {
class ParasiticNet;
class ParasiticNode;

class Parasitics;
class ReadParasitics
{
 public:
  explicit ReadParasitics(Parasitics& parasitics) : _parasitics(parasitics) {}
  void read();

 private:
  Parasitics& _parasitics;
  void readNet(spef::Net& spef_net);
  void readConnection(ParasiticNet& parasitic_net, spef::ConnEntry& spef_conn);
  void readCapacitance(ParasiticNet& parasitic_net, spef::ResCap& spef_cap);
  void readResistance(ParasiticNet& parasitic_net, spef::ResCap& spef_res);
  double readCapacitance(double spef_capacitance);
  double readResistance(double spef_resistance);
  double unitScale(std::string& spef_unit, std::string& target_unit);
  ParasiticNode& ensureNode(ParasiticNet& parasitic_net, const std::string& node_name);
};
}  // namespace ista
