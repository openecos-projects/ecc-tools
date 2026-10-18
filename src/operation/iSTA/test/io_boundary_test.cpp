// SPDX-License-Identifier: MulanPSL-2.0
#include "DataManager.hpp"
#include "IdbCellMaster.h"
#include "IdbInstance.h"
#include "IdbNet.h"
#include "Logger.hpp"
#include "SdcCommand.hpp"
#include "STAInterface.hpp"

using namespace ista;

void require(bool condition, const std::string& message)
{
  if (!condition) {
    throw std::runtime_error(message);
  }
}

void testNet(idb::CellMasterType cell_type, idb::IdbConnectDirection port_direction, idb::IdbConnectDirection cell_direction,
             int32_t port_num, bool has_core_load)
{
  idb::IdbCellMaster io_master;
  io_master.set_name("boundary_cell");
  io_master.set_type(cell_type);
  // Signal connectivity must survive wrapping regardless of PAD classification or pin name.
  io_master.add_term("package_contact")->set_direction(cell_direction);
  idb::IdbInstance io_instance;
  io_instance.set_name("io_cell");
  io_instance.set_cell_master(&io_master);
  idb::IdbPin* io_cell_pin = io_instance.get_pin_list()->get_pin_list().front();

  idb::IdbCellMaster core_master;
  core_master.set_name("core_cell");
  core_master.set_type(idb::CellMasterType::kCore);
  core_master.add_term("A")->set_direction(idb::IdbConnectDirection::kInput);
  idb::IdbInstance core_instance;
  core_instance.set_name("core");
  core_instance.set_cell_master(&core_master);
  idb::IdbPin* core_pin = core_instance.get_pin_list()->get_pin_list().front();

  idb::IdbPin port;
  port.set_pin_name("external");
  port.set_as_io();
  port.set_term()->set_direction(port_direction);
  idb::IdbPin other_port;
  other_port.set_pin_name("other_external");
  other_port.set_as_io();
  other_port.set_term()->set_direction(port_direction);

  idb::IdbNet net;
  net.set_net_name("connection");
  net.set_connect_type(idb::IdbConnectType::kSignal);
  net.add_instance_pin(io_cell_pin);
  io_cell_pin->set_net(&net);
  if (port_num > 0) {
    net.add_io_pin(&port);
    port.set_net(&net);
  }
  if (port_num > 1) {
    net.add_io_pin(&other_port);
    other_port.set_net(&net);
  }
  if (has_core_load) {
    net.add_instance_pin(core_pin);
    core_pin->set_net(&net);
  }

  DataManager::initInst();
  STAI.wrapInstance(&io_instance);
  STAI.wrapInstancePinList(&io_instance);
  if (has_core_load) {
    STAI.wrapInstance(&core_instance);
    STAI.wrapInstancePinList(&core_instance);
  }
  for (idb::IdbPin* pin : net.get_io_pins()->get_pin_list()) {
    STAI.wrapPortPin(pin);
  }
  STAI.wrapNet(&net);

  Database& database = STADM.getDatabase();
  require(database.get_instance_map().count("io_cell") == 1, "IO cell instance was removed");
  require(database.get_pin_map().count("io_cell:package_contact") == 1, "IO cell pin was removed");
  require(database.get_net_map().count("connection") == 1, "signal net was removed");
  require(database.get_pin_map()["io_cell:package_contact"].get_direction() == STAI.wrapPinDirection(cell_direction),
          "wrapping changed IO cell pin direction");
  for (idb::IdbPin* pin : net.get_io_pins()->get_pin_list()) {
    require(database.get_pin_map().count(pin->get_pin_name()) == 1, "top port was removed");
    require(database.get_pin_map()[pin->get_pin_name()].get_direction() == STAI.wrapPinDirection(port_direction),
            "wrapping changed top port direction");
  }
  if (has_core_load) {
    require(database.get_pin_map().count("core:A") == 1, "core-facing load was removed");
  }
  for (auto& [name, wrapped_net] : database.get_net_map()) {
    require(wrapped_net.get_pin_name_list().size() == port_num + (has_core_load ? 2U : 1U), "signal net lost pin membership");
    for (const std::string& pin_name : wrapped_net.get_pin_name_list()) {
      require(database.get_pin_map().count(pin_name) == 1, "wrapped net references a filtered pin");
    }
  }

  if (port_num > 0) {
    SdcCommand& interpreter = SdcCommand::getInst();
    std::string clock_command = "create_clock -name external_clock -period 10";
    if (port_direction != idb::IdbConnectDirection::kOutput) {
      clock_command += " [get_ports external]";
    }
    require(interpreter.evalString(clock_command) == TCL_OK, "clock constraint failed at retained port");
    TimingConstraint& constraint = database.get_timing_constraint();
    if (port_direction != idb::IdbConnectDirection::kOutput) {
      require(constraint.get_clock_map()["external_clock"].get_source_list() == std::vector<std::string>{"external"},
              "clock source was not retained at the original port");
      require(interpreter.evalString("set_input_delay -clock external_clock 1 [get_ports external]") == TCL_OK,
              "input delay failed at retained port");
      require(constraint.get_port_constraint_map()["external"].get_input_delay_max() == 1.0, "input delay was lost");
    }
    if (port_direction != idb::IdbConnectDirection::kInput) {
      require(interpreter.evalString("set_output_delay -clock external_clock 2 [get_ports external]") == TCL_OK,
              "output delay failed at retained port");
      require(interpreter.evalString("set_load 1 [get_ports external]") == TCL_OK, "external load failed at retained port");
      require(constraint.get_port_constraint_map()["external"].get_output_delay_max() == 2.0, "output delay was lost");
      require(constraint.get_port_constraint_map()["external"].get_load() == 1.0, "external load was lost");
    }
  }

  require(net.get_io_pins()->get_pin_num() == static_cast<uint32_t>(port_num), "IDB ports were modified");
  require(net.get_instance_pin_list()->get_pin_num() == (has_core_load ? 2U : 1U), "IDB instance pins were modified");
  require(io_cell_pin->get_term()->get_direction() == cell_direction, "IDB direction was modified");
  DataManager::destroyInst();
}

void testUnconnectedPort()
{
  DataManager::initInst();
  idb::IdbPin port;
  port.set_pin_name("unconnected");
  port.set_as_io();
  port.set_term()->set_direction(idb::IdbConnectDirection::kInput);
  STAI.wrapPortPin(&port);
  require(STADM.getDatabase().get_pin_map().count("unconnected") == 1, "unconnected port was removed");
  DataManager::destroyInst();
}

int main()
{
  Logger::initInst();
  SdcCommand::initInst();
  sdc::registerSdcCommands(SdcCommand::getInst());
  for (idb::IdbConnectDirection port_direction :
       {idb::IdbConnectDirection::kInput, idb::IdbConnectDirection::kOutput, idb::IdbConnectDirection::kInOut}) {
    for (idb::IdbConnectDirection cell_direction :
         {idb::IdbConnectDirection::kInput, idb::IdbConnectDirection::kOutput, idb::IdbConnectDirection::kInOut}) {
      testNet(idb::CellMasterType::kPad, port_direction, cell_direction, 1, false);
      testNet(idb::CellMasterType::kCore, port_direction, cell_direction, 1, false);
    }
  }
  testNet(idb::CellMasterType::kPadInput, idb::IdbConnectDirection::kInOut, idb::IdbConnectDirection::kInOut, 1, false);
  testNet(idb::CellMasterType::kPad, idb::IdbConnectDirection::kInput, idb::IdbConnectDirection::kOutput, 0, true);
  testNet(idb::CellMasterType::kPad, idb::IdbConnectDirection::kInput, idb::IdbConnectDirection::kInOut, 1, true);
  testNet(idb::CellMasterType::kPad, idb::IdbConnectDirection::kInput, idb::IdbConnectDirection::kInOut, 2, false);
  testUnconnectedPort();
  SdcCommand::destroyInst();
  STAInterface::destroyInst();
  Logger::destroyInst();
  return 0;
}
