// SPDX-License-Identifier: MulanPSL-2.0
#include "DataManager.hpp"
#include "IdbCellMaster.h"
#include "IdbInstance.h"
#include "IdbNet.h"
#include "IdbSpecialNet.h"
#include "Logger.hpp"
#include "STAInterface.hpp"

using namespace ista;

void require(bool condition, const std::string& message)
{
  if (!condition) {
    throw std::runtime_error(message);
  }
}

void testRegularNet(idb::IdbConnectType net_type, idb::IdbConnectType pin_type, idb::IdbConnectType port_type, bool excluded,
                    bool has_signal_load = false)
{
  idb::IdbCellMaster master;
  master.set_name("cell");
  master.set_type(idb::CellMasterType::kCore);
  master.add_term("contact")->set_direction(idb::IdbConnectDirection::kInput);
  master.findTerm("contact")->set_type(pin_type);
  master.add_term("A")->set_direction(idb::IdbConnectDirection::kInput);
  master.findTerm("A")->set_type(idb::IdbConnectType::kSignal);
  idb::IdbInstance instance;
  instance.set_name("cell0");
  instance.set_cell_master(&master);
  idb::IdbPin* cell_pin = instance.get_pin("contact");
  idb::IdbPin* signal_pin = instance.get_pin("A");

  idb::IdbPin port;
  port.set_pin_name("external");
  port.set_as_io();
  port.set_term()->set_direction(idb::IdbConnectDirection::kInput);
  port.get_term()->set_type(port_type);

  idb::IdbNet net;
  // A signal named VDD must not be discarded; a supply need not be named VDD.
  net.set_net_name(excluded ? "supply_connection" : "VDD");
  net.set_connect_type(net_type);
  net.add_instance_pin(cell_pin);
  cell_pin->set_net(&net);
  net.add_io_pin(&port);
  port.set_net(&net);
  if (has_signal_load) {
    net.add_instance_pin(signal_pin);
    signal_pin->set_net(&net);
  }

  DataManager::initInst();
  STAI.wrapInstance(&instance);
  STAI.wrapInstancePinList(&instance);
  STAI.wrapPortPin(&port);
  STAI.wrapNet(&net);

  Database& database = STADM.getDatabase();
  bool keep_cell_pin = STAI.wrapSignalConnectType(pin_type);
  bool keep_port = !excluded && STAI.wrapSignalConnectType(port_type);
  require(database.get_instance_map().count("cell0") == 1, "supply filtering removed the instance");
  require(database.get_pin_map().count("cell0:contact") == (keep_cell_pin ? 1U : 0U), "incorrect supply pin membership");
  require(database.get_pin_map().count("external") == (keep_port ? 1U : 0U), "supply port was not filtered with its net");
  require(database.get_pin_map().count("cell0:A") == 1, "unrelated or mixed-net signal pin was removed");
  if (keep_cell_pin && (net_type == idb::IdbConnectType::kPower || net_type == idb::IdbConnectType::kGround)) {
    require(database.get_pin_map().at("cell0:contact").get_constant_value()
                == (net_type == idb::IdbConnectType::kPower ? TimingCaseValue::kOne : TimingCaseValue::kZero),
            "supply-tied logic input lost its constant");
  }
  require(database.get_net_map().count(net.get_net_name()) == (excluded ? 0U : 1U), "incorrect supply net membership");
  if (!excluded) {
    for (const std::string& pin_name : database.get_net_map().at(net.get_net_name()).get_pin_name_list()) {
      require(database.get_pin_map().count(pin_name) == 1, "net references a filtered pin");
    }
  }
  require(port.get_net() == &net && cell_pin->get_net() == &net, "IDB connectivity changed");
  require(net.get_connect_type() == net_type && cell_pin->get_term()->get_type() == pin_type, "IDB types changed");
  DataManager::destroyInst();
}

void testSpecialNet(idb::IdbConnectType net_type)
{
  idb::IdbCellMaster master;
  master.set_type(idb::CellMasterType::kCore);
  master.add_term("contact")->set_direction(idb::IdbConnectDirection::kInOut);
  master.findTerm("contact")->set_type(idb::IdbConnectType::kSignal);
  idb::IdbInstance instance;
  instance.set_name("cell0");
  instance.set_cell_master(&master);
  idb::IdbPin* cell_pin = instance.get_pin("contact");
  idb::IdbPin port;
  port.set_pin_name("external");
  port.set_as_io();
  port.set_term()->set_direction(idb::IdbConnectDirection::kInOut);

  idb::IdbSpecialNet net;
  net.set_net_name("supply_connection");
  net.set_connect_type(net_type);
  net.add_instance_pin(cell_pin);
  cell_pin->set_special_net(&net);
  net.add_io_pin(&port);
  port.set_special_net(&net);

  DataManager::initInst();
  STAI.wrapInstance(&instance);
  STAI.wrapInstancePinList(&instance);
  STAI.wrapPortPin(&port);
  bool excluded = net_type == idb::IdbConnectType::kPower || net_type == idb::IdbConnectType::kGround;
  require(STADM.getDatabase().get_pin_map().size() == (excluded ? 1U : 2U), "incorrect special net pin filtering");
  if (excluded) {
    require(STADM.getDatabase().get_pin_map().at("cell0:contact").get_constant_value()
                == (net_type == idb::IdbConnectType::kPower ? TimingCaseValue::kOne : TimingCaseValue::kZero),
            "special supply-tied logic input lost its constant");
  }
  require(port.get_special_net() == &net && cell_pin->get_special_net() == &net, "IDB special connectivity changed");
  DataManager::destroyInst();
}

void testSupplyEvidence()
{
  idb::IdbPin power_pin;
  power_pin.set_term()->set_type(idb::IdbConnectType::kPower);
  idb::IdbPin ground_pin;
  ground_pin.set_term()->set_type(idb::IdbConnectType::kGround);
  idb::IdbNet net;
  net.set_connect_type(idb::IdbConnectType::kSignal);
  require(!STAI.isPGNet(nullptr) && !STAI.isPGNet(&net), "missing supply evidence was accepted");
  net.add_instance_pin(&power_pin);
  require(STAI.isPGNet(&net), "LEF power pin was not recognized");
  net.set_connect_type(idb::IdbConnectType::kClock);
  require(!STAI.isPGNet(&net), "explicit clock type was overridden");
  net.set_connect_type(idb::IdbConnectType::kSignal);
  net.add_io_pin(&ground_pin);
  require(!STAI.isPGNet(&net), "conflicting port and cell types were ignored");
  net.add_instance_pin(&ground_pin);
  require(!STAI.isPGNet(&net), "conflicting cell PG types were ignored");
}

int main()
{
  Logger::initInst();
  for (idb::IdbConnectType pg_type : {idb::IdbConnectType::kPower, idb::IdbConnectType::kGround}) {
    testRegularNet(pg_type, idb::IdbConnectType::kSignal, idb::IdbConnectType::kSignal, true);
    testRegularNet(pg_type, pg_type, idb::IdbConnectType::kNone, true);
    testRegularNet(idb::IdbConnectType::kSignal, pg_type, idb::IdbConnectType::kSignal, true);
    testRegularNet(idb::IdbConnectType::kNone, pg_type, pg_type, true);
    testRegularNet(idb::IdbConnectType::kSignal, pg_type, idb::IdbConnectType::kSignal, false, true);
    testSpecialNet(pg_type);
  }
  for (idb::IdbConnectType signal_type : {idb::IdbConnectType::kNone, idb::IdbConnectType::kSignal, idb::IdbConnectType::kClock,
                                        idb::IdbConnectType::kReset, idb::IdbConnectType::kScan, idb::IdbConnectType::kTieOff}) {
    testRegularNet(signal_type, idb::IdbConnectType::kSignal, idb::IdbConnectType::kSignal, false);
  }
  testSpecialNet(idb::IdbConnectType::kClock);
  testSupplyEvidence();
  STAInterface::destroyInst();
  Logger::destroyInst();
  return 0;
}
