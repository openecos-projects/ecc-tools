// SPDX-License-Identifier: MulanPSL-2.0
#include <stdexcept>
#include <string>
#include <vector>

#include "DataManager.hpp"
#include "IdbLayout.h"
#include "Logger.hpp"
#include "ReadNetwork.hpp"
#include "idm.h"

using namespace ista;

void require(bool value, const char* message)
{
  if (!value)
    throw std::runtime_error(message);
}

// Temporarily install a real iDB design, retaining the caller's external state.
struct Fixture
{
  idb::IdbLayout layout;
  idb::IdbDefService service{&layout};
  idb::IdbDefService* saved_service = dmInst->get_idb_def_service();
  std::string saved_sdc = dmInst->get_config().get_sdc_path();
  idb::IdbPin* input = nullptr;
  idb::IdbPin* supply_input = nullptr;
  idb::IdbNet* signal = nullptr;

  Fixture()
  {
    auto* design = service.get_design();
    design->set_design_name("netlist_contract");
    auto* master = layout.get_cell_master_list()->set_cell_master("PAD");
    master->set_type(idb::CellMasterType::kPad);
    for (const auto& name : {"contact", "tie", "VDD"}) {
      auto* term = master->add_term(name);
      term->set_direction(std::string(name) == "contact" ? idb::IdbConnectDirection::kInOut : idb::IdbConnectDirection::kInput);
      term->set_type(std::string(name) == "VDD" ? idb::IdbConnectType::kPower : idb::IdbConnectType::kSignal);
    }
    auto* instance = design->get_instance_list()->add_instance("pad/escaped");
    instance->set_cell_master(master);
    input = instance->get_pin("contact");
    supply_input = instance->get_pin("tie");
    input->set_average_coordinate(17, 29);
    auto* port = design->get_io_pin_list()->add_pin_list("external[0]");
    port->set_as_io();
    port->set_term()->set_direction(idb::IdbConnectDirection::kInOut);
    port->set_average_coordinate(31, 43);
    auto* unused = design->get_io_pin_list()->add_pin_list("unused");
    unused->set_as_io();
    unused->set_term()->set_direction(idb::IdbConnectDirection::kInput);
    signal = design->get_net_list()->add_net("VDD", idb::IdbConnectType::kSignal);
    signal->add_io_pin(port);
    port->set_net(signal);
    signal->add_instance_pin(input);
    input->set_net(signal);
    auto* supply = design->get_net_list()->add_net("supply", idb::IdbConnectType::kPower);
    for (auto* pin : {supply_input, instance->get_pin("VDD")}) {
      supply->add_instance_pin(pin);
      pin->set_net(supply);
    }
    dmInst->set_idb_def_service(&service);
    dmInst->get_config().get_sdc_path() = "contract.sdc";
  }
  ~Fixture()
  {
    dmInst->set_idb_def_service(saved_service);
    dmInst->get_config().get_sdc_path() = saved_sdc;
  }
};

void checkImported(Database& database)
{
  require(database.network().name() == "netlist_contract", "design identity lost");
  require(database.sdc().get_sdc_file_path() == "owned-by-session.sdc", "network import modified constraints");
  auto& instances = database.network().instances();
  require(instances.size() == 1 && instances.at("pad/escaped").get_cell_name() == "PAD" && instances.at("pad/escaped").get_is_io_cell(),
          "instance metadata lost");
  auto& pins = database.network().pins();
  require(pins.size() == 4 && !pins.contains("pad/escaped:VDD"), "supply terminal filtering changed");
  auto& input = pins.at("pad/escaped:contact");
  require(input.get_full_name() == "pad/escaped:contact" && input.get_pin_name() == "contact" && input.get_instance_name() == "pad/escaped"
              && !input.get_is_port() && input.get_direction() == PinDirection::kInout && input.get_x() == 17 && input.get_y() == 29,
          "instance pin identity/direction/coordinate changed");
  auto& port = pins.at("external[0]");
  require(port.get_is_port() && port.get_direction() == PinDirection::kInout && port.get_x() == 31 && port.get_y() == 43, "port role or coordinate changed");
  require(pins.at("unused").get_is_port(), "unconnected port lost");
  require(pins.at("pad/escaped:tie").get_constant_value() == TimingCaseValue::kOne, "supply-tied logic input lost constant");
  auto& nets = database.network().nets();
  require(nets.size() == 1 && nets.at("VDD").get_pin_name_list() == std::vector<std::string>{"external[0]", "pad/escaped:contact"},
          "net order, membership or supply filtering changed");
}

int main()
{
  Logger::initInst();
  DataManager::initInst();
  {
    Fixture fixture;
    Database database;
    database.sdc().set_sdc_file_path("owned-by-session.sdc");
    STADM.getDatabase().network().setName("singleton_sentinel");
    readNetwork(database.network());
    checkImported(database);
    require(STADM.getDatabase().network().name() == "singleton_sentinel" && STADM.getDatabase().network().pins().empty(), "import ignored borrowed database");
    readNetwork(database.network());
    checkImported(database);
    require(fixture.input->get_net() == fixture.signal && fixture.signal->get_instance_pin_list()->get_pin_num() == 1
                && fixture.signal->get_io_pins()->get_pin_num() == 1 && fixture.input->get_average_coordinate()->get_x() == 17,
            "import mutated iDB connectivity/coordinates");
  }
  DataManager::destroyInst();
  Logger::destroyInst();
}
