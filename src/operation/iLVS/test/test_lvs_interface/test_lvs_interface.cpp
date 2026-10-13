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
#include <cassert>
#include <stdexcept>
#include <string>
#include <vector>

#include "DataManager.hpp"
#include "EntityChecker.hpp"
#include "DefData.hpp"
#include "IdbDesign.h"
#include "IdbLayout.h"
#include "LVSHeader.hpp"
#include "LVSInterface.hpp"
#include "NetlistData.hpp"
#include "Utility.hpp"
#include "context/DatabaseContext.h"
#include "design/DesignStore.h"
#include "design/netlist/model/NetlistComponents.h"
#include "library/LibraryStore.h"
#include "library/cell_master/model/CellMasterComponents.h"
#include "library/master_term/model/MasterTermComponents.h"
#include "tech/TechStore.h"

namespace {

void testNativeInputIoDriverSelection()
{
  eccdb::TechStore technology;
  eccdb::LibraryStore library(technology.techRegistry());
  eccdb::DesignStore design(technology.techRegistry(), library.libraryRegistry());
  design.globalStorage().setInfo({.name = "native_lvs_io_root", .database_units_per_micron = 1000});
  eccdb::DatabaseContext context(technology, library, design);

  const auto sink_master = library.cellMasterStorage().createCellMaster(
      {.name = "SINK", .type = eccdb::LibraryCellMasterType::kCore, .width = 10, .height = 10});
  static_cast<void>(library.masterTermStorage().createMasterTerm(
      sink_master, {.name = "A", .direction = eccdb::LibraryMasterTermDirection::kInput}));
  const auto driver_master = library.cellMasterStorage().createCellMaster(
      {.name = "DRIVER", .type = eccdb::LibraryCellMasterType::kCore, .width = 10, .height = 10});
  static_cast<void>(library.masterTermStorage().createMasterTerm(
      driver_master, {.name = "Y", .direction = eccdb::LibraryMasterTermDirection::kOutput}));

  auto& netlist = design.netlistStorage();
  const auto input_sink = netlist.createInstance({.name = "input_sink", .master = sink_master});
  const auto driven_sink = netlist.createInstance({.name = "driven_sink", .master = sink_master});
  const auto output_sink = netlist.createInstance({.name = "output_sink", .master = sink_master});
  const auto driver = netlist.createInstance({.name = "driver", .master = driver_master});
  const auto input_net = netlist.createNet({.name = "input_net"});
  const auto driven_net = netlist.createNet({.name = "driven_net"});
  const auto output_net = netlist.createNet({.name = "output_net"});
  const auto input_port = netlist.createBlockTerm({.name = "top_input", .direction = eccdb::DesignIoPinDirection::kInput});
  const auto driven_port = netlist.createBlockTerm({.name = "top_driven", .direction = eccdb::DesignIoPinDirection::kInput});
  const auto output_port = netlist.createBlockTerm({.name = "top_output", .direction = eccdb::DesignIoPinDirection::kOutput});

  const auto input_sink_term = netlist.findInstanceTerm(input_sink, "A");
  const auto driven_sink_term = netlist.findInstanceTerm(driven_sink, "A");
  const auto output_sink_term = netlist.findInstanceTerm(output_sink, "A");
  const auto driver_term = netlist.findInstanceTerm(driver, "Y");
  assert(input_sink_term && driven_sink_term && output_sink_term && driver_term);
  netlist.connect(input_port, input_net);
  netlist.connect(input_sink_term, input_net);
  netlist.connect(driven_port, driven_net);
  netlist.connect(driver_term, driven_net);
  netlist.connect(driven_sink_term, driven_net);
  netlist.connect(output_port, output_net);
  netlist.connect(output_sink_term, output_net);

  ilvs::NetlistData reference;
  reference.set_design_name("native_lvs_io_root");
  ilvs::DataManager::initInst();
  auto& lvs = ilvs::LVSInterface::getInst();
  lvs.setDesignSource(context);
  lvs.setReferenceNetlist(&reference);
  lvs.wrapDatabaseFromEccdb();

  const auto& routing_map =
      ilvs::DataManager::getInst().getDatabase().get_def_data().get_def_routing_data().get_net_routing_data_map();
  assert(routing_map.at("input_net").get_driver_terminal_name() == "PIN/top_input");
  assert(routing_map.at("driven_net").get_driver_terminal_name() == "driver/Y");
  assert(routing_map.at("output_net").get_driver_terminal_name().empty());

  lvs.setReferenceNetlist(nullptr);
  lvs.setDesignSource(static_cast<eccdb::DatabaseContext*>(nullptr));
  ilvs::DataManager::destroyInst();
}

void testNativeEscapedDefNetNamesMatchReferenceVerilog()
{
  eccdb::TechStore technology;
  eccdb::LibraryStore library(technology.techRegistry());
  eccdb::DesignStore design(technology.techRegistry(), library.libraryRegistry());
  design.globalStorage().setInfo({.name = "native_lvs_escaped_net", .database_units_per_micron = 1000});
  eccdb::DatabaseContext context(technology, library, design);

  // The raw DEF spelling contains both a doubled slash (a literal slash) and
  // escaped bus delimiters.  The reference Verilog identifier is its semantic
  // spelling, not the DEF serialization spelling.
  const std::string def_name = R"(queue\\literal\[0\])";
  const std::string reference_name = R"(queue\literal[0])";
  static_cast<void>(design.netlistStorage().createNet({.name = def_name}));

  ilvs::NetlistData reference;
  reference.set_design_name("native_lvs_escaped_net");
  reference.get_net_map()[reference_name] = ilvs::Net{};

  ilvs::DataManager::initInst();
  auto& lvs = ilvs::LVSInterface::getInst();
  lvs.setDesignSource(context);
  lvs.setReferenceNetlist(&reference);
  lvs.wrapDatabaseFromEccdb();

  auto& database = ilvs::DataManager::getInst().getDatabase();
  const auto& def_data = database.get_def_data();
  assert(def_data.get_net_map().contains(reference_name));
  assert(!def_data.get_net_map().contains(def_name));
  assert(def_data.get_def_routing_data().get_net_routing_data_map().contains(reference_name));
  assert(!def_data.get_def_routing_data().get_net_routing_data_map().contains(def_name));

  ilvs::EntityChecker::initInst();
  ilvs::EntityChecker::getInst().check();
  assert(database.get_summary().ec_summary.net_difference_num == 0);
  ilvs::EntityChecker::destroyInst();

  lvs.setReferenceNetlist(nullptr);
  lvs.setDesignSource(static_cast<eccdb::DatabaseContext*>(nullptr));
  ilvs::DataManager::destroyInst();
}

void testNativeEscapedDefNetNameCollisionFailsClearly()
{
  eccdb::TechStore technology;
  eccdb::LibraryStore library(technology.techRegistry());
  eccdb::DesignStore design(technology.techRegistry(), library.libraryRegistry());
  design.globalStorage().setInfo({.name = "native_lvs_escaped_collision", .database_units_per_micron = 1000});
  eccdb::DatabaseContext context(technology, library, design);

  // These names remain distinct in the DEF-facing EccDB.  iLVS has only a
  // string net identity, so it must reject the lossy projection rather than
  // merge their geometry and terminals under one reference-net name.
  static_cast<void>(design.netlistStorage().createNet({.name = R"(literal\[0\])"}));
  static_cast<void>(design.netlistStorage().createNet({.name = "literal[0]"}));

  ilvs::NetlistData reference;
  reference.set_design_name("native_lvs_escaped_collision");
  ilvs::DataManager::initInst();
  auto& lvs = ilvs::LVSInterface::getInst();
  lvs.setDesignSource(context);
  lvs.setReferenceNetlist(&reference);
  bool rejected = false;
  try {
    lvs.wrapDatabaseFromEccdb();
  } catch (const std::runtime_error& error) {
    rejected = std::string(error.what()).find("both decode to 'literal[0]'") != std::string::npos;
  }
  assert(rejected);

  lvs.setReferenceNetlist(nullptr);
  lvs.setDesignSource(static_cast<eccdb::DatabaseContext*>(nullptr));
  ilvs::DataManager::destroyInst();
}

}  // namespace

int main()
{
  assert(ilvs::Utility::getIOName("IN") == "PIN/IN");
  assert(ilvs::Utility::getInstancePinName("u0", "A") == "u0/A");
  assert(ilvs::Utility::isIOName("PIN/IN"));
  assert(ilvs::Utility::getIOPinName("PIN/IN") == "IN");
  assert(ilvs::Utility::getIOPinName("IN") == "IN");

  idb::IdbLayout layout;
  layout.initDie(10, 20, 1000, 2000);
  idb::IdbDesign design(&layout);
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
  assert(!LVSUTIL.exist(netlist_data.get_terminal_connect_type_map(), std::string("PIN/IN")));
  assert(!LVSUTIL.exist(netlist_data.get_terminal_connect_type_map(), std::string("u0/A")));
  assert(netlist_data.get_terminal_connect_type_map().at("u0/VDD") == ilvs::ConnectType::kPower);
  assert(netlist_data.get_terminal_connect_type_map().at("u0/VSS") == ilvs::ConnectType::kGround);

  ilvs::DefData def_data = LVSI.wrapDefData(&design);
  assert(def_data.get_design_name() == "top");
  assert(def_data.get_die().get_real_ll_x() == 10);
  assert(def_data.get_die().get_real_ll_y() == 20);
  assert(def_data.get_die().get_real_ur_x() == 1000);
  assert(def_data.get_die().get_real_ur_y() == 2000);
  assert(LVSUTIL.exist(def_data.get_net_map(), std::string("n1")));
  assert(!LVSUTIL.exist(def_data.get_terminal_connect_type_map(), std::string("PIN/IN")));
  assert(!LVSUTIL.exist(def_data.get_terminal_connect_type_map(), std::string("u0/A")));
  ilvs::PhysicalGraph& physical_graph = def_data.get_physical_graph();
  assert(physical_graph.get_net_routing_graph_list().size() == 3);
  assert(physical_graph.getNetRoutingGraph("n1") != nullptr);
  assert(physical_graph.getNetRoutingGraph("VDD") != nullptr);
  assert(physical_graph.getNetRoutingGraph("VSS") != nullptr);
  assert(physical_graph.get_power_instance_pin_net_map().at("u0/VDD") == "VDD");
  assert(physical_graph.get_ground_instance_pin_net_map().at("u0/VSS") == "VSS");
  assert(vdd_net->get_instance_pin_list()->get_pin_list().empty());
  assert(vss_net->get_instance_pin_list()->get_pin_list().empty());
  assert(vdd_pin->get_special_net() == nullptr);
  assert(vss_pin->get_special_net() == nullptr);

  testNativeInputIoDriverSelection();
  testNativeEscapedDefNetNamesMatchReferenceVerilog();
  testNativeEscapedDefNetNameCollisionFailsClearly();
  ilvs::LVSInterface::destroyInst();
  return 0;
}
