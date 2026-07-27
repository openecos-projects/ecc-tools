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
#include <cassert>
#include <string>
#include <unordered_map>

#include "IdbDesign.h"
#include "NetlistExtractor.hpp"

int main()
{
  ilvs::NetlistExtractor::initInst();

  idb::IdbDesign design;
  idb::IdbSpecialNet* vdd = design.createOrFindSpecialNet("VDD", idb::IdbConnectType::kPower);
  idb::IdbSpecialNet* vss = design.createOrFindSpecialNet("VSS", idb::IdbConnectType::kGround);
  assert(vdd != nullptr);
  assert(vss != nullptr);
  vdd->add_wildcard_instance_pin("VDD");
  vss->add_wildcard_instance_pin("VSS");

  idb::IdbInstance* instance = design.get_instance_list()->add_instance("u0");
  assert(instance != nullptr);
  idb::IdbPin* vdd_pin = instance->addPin("VDD");
  idb::IdbPin* vss_pin = instance->addPin("VSS");
  idb::IdbPin* signal_pin = instance->addPin("A");
  assert(vdd_pin != nullptr);
  assert(vss_pin != nullptr);
  assert(signal_pin != nullptr);
  vdd_pin->set_term()->set_name("VDD");
  vss_pin->set_term()->set_name("VSS");
  signal_pin->set_term()->set_name("A");

  const ilvs::Netlist snapshot = LVSNE.extractPhysical(&design);
  assert((snapshot.physical_graph.power_instance_pin_net_map == std::unordered_map<std::string, std::string>{{"u0/VDD", "VDD"}}));
  assert((snapshot.physical_graph.ground_instance_pin_net_map == std::unordered_map<std::string, std::string>{{"u0/VSS", "VSS"}}));
  assert(vdd->get_instance_pin_list()->get_pin_list().empty());
  assert(vss->get_instance_pin_list()->get_pin_list().empty());
  assert(vdd_pin->get_special_net() == nullptr);
  assert(vss_pin->get_special_net() == nullptr);
  assert(signal_pin->get_special_net() == nullptr);
  ilvs::NetlistExtractor::destroyInst();
  return 0;
}
