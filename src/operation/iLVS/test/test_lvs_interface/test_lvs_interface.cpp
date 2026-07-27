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
#include "DefData.hpp"
#include "DataManager.hpp"
#include "IdbDesign.h"
#include "LVSHeader.hpp"
#include "LVSInterface.hpp"
#include "NetlistData.hpp"
#include "Utility.hpp"

int main()
{
  assert(ilvs::Utility::getIOName("IN") == "PIN/IN");
  assert(ilvs::Utility::getInstancePinName("u0", "A") == "u0/A");
  assert(ilvs::Utility::isIOName("PIN/IN"));
  assert(ilvs::Utility::getIOPinName("PIN/IN") == "IN");
  assert(ilvs::Utility::getIOPinName("IN") == "IN");

  idb::IdbDesign design;
  design.set_design_name("top");
  idb::IdbPin* io_pin = design.createOrFindIoPin("IN");
  assert(io_pin != nullptr);
  io_pin->set_term()->set_name("IN");

  idb::IdbInstance* instance = design.get_instance_list()->add_instance("u0");
  assert(instance != nullptr);
  idb::IdbPin* signal_pin = instance->addPin("A");
  idb::IdbPin* vdd_pin = instance->addPin("VDD");
  idb::IdbPin* vss_pin = instance->addPin("VSS");
  assert(signal_pin != nullptr);
  assert(vdd_pin != nullptr);
  assert(vss_pin != nullptr);
  signal_pin->set_term()->set_name("A");
  vdd_pin->set_term()->set_name("VDD");
  vss_pin->set_term()->set_name("VSS");

  idb::IdbNet* signal_net = design.createOrFindNet("n1");
  assert(signal_net != nullptr);
  assert(design.connectPinToNet(io_pin, signal_net));
  assert(design.connectPinToNet(signal_pin, signal_net));

  idb::IdbSpecialNet* vdd_net = design.createOrFindSpecialNet("VDD", idb::IdbConnectType::kPower);
  idb::IdbSpecialNet* vss_net = design.createOrFindSpecialNet("VSS", idb::IdbConnectType::kGround);
  assert(vdd_net != nullptr);
  assert(vss_net != nullptr);
  vdd_net->add_wildcard_instance_pin("VDD");
  vss_net->add_wildcard_instance_pin("VSS");

  ilvs::NetlistData netlist_data = LVSI.wrapNetlistData(&design);
  assert(netlist_data.get_design_name() == "top");
  assert(netlist_data.get_io_terminal_name_list() == std::vector<std::string>({"PIN/IN"}));
  assert(LVSUTIL.exist(netlist_data.get_instance_name_set(), std::string("u0")));
  assert(LVSUTIL.exist(netlist_data.get_net_map(), std::string("n1")));
  assert(netlist_data.get_net_map().at("n1").get_terminal_name_list() == std::vector<std::string>({"PIN/IN", "u0/A"}));
  assert(netlist_data.get_terminal_connect_type_map().at("u0/VDD") == ilvs::ConnectType::kPower);
  assert(netlist_data.get_terminal_connect_type_map().at("u0/VSS") == ilvs::ConnectType::kGround);

  ilvs::DefData def_data = LVSI.wrapDefData(&design);
  assert(def_data.get_design_name() == "top");
  assert(LVSUTIL.exist(def_data.get_net_map(), std::string("n1")));
  assert(def_data.get_physical_graph().get_net_routing_graph_map().empty());
  assert(LVSUTIL.exist(def_data.get_def_routing_data().get_net_routing_data_map(), std::string("n1")));
  assert(def_data.get_def_routing_data().get_power_instance_pin_net_map().at("u0/VDD") == "VDD");
  assert(def_data.get_def_routing_data().get_ground_instance_pin_net_map().at("u0/VSS") == "VSS");
  assert(vdd_net->get_instance_pin_list()->get_pin_list().empty());
  assert(vss_net->get_instance_pin_list()->get_pin_list().empty());
  assert(vdd_pin->get_special_net() == nullptr);
  assert(vss_pin->get_special_net() == nullptr);

  ilvs::LVSInterface::destroyInst();
  return 0;
}
