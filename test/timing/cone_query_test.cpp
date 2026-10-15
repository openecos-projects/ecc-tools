// SPDX-License-Identifier: MulanPSL-2.0
#include <stdexcept>

#include "SdcCommand.hpp"
#include "object/ObjectQuery.hpp"

namespace {
void require(bool value)
{
  if (!value)
    throw std::runtime_error("SDC cone query test failed");
}
}  // namespace

int main()
{
  using namespace ista;
  Database database;
  auto addPin = [&](std::string name, std::string instance, std::string port, PinDirection direction, std::string net) {
    Pin pin;
    pin.set_full_name(name);
    pin.set_pin_name(port);
    pin.set_instance_name(instance);
    pin.set_direction(direction);
    pin.set_net_name(net);
    pin.set_is_port(instance.empty());
    database.get_pin_map()[name] = pin;
    database.get_net_map()[net].get_pin_name_list().push_back(name);
    if (!instance.empty()) {
      auto& cell = database.get_instance_map()[instance];
      cell.set_instance_name(instance);
      cell.get_pin_name_list().push_back(name);
    }
  };
  addPin("launch:Q", "launch", "Q", PinDirection::kOutput, "a");
  addPin("buf:A", "buf", "A", PinDirection::kInput, "a");
  addPin("buf:Y", "buf", "Y", PinDirection::kOutput, "b");
  addPin("capture:D", "capture", "D", PinDirection::kInput, "b");
  addPin("capture:Q", "capture", "Q", PinDirection::kOutput, "c");
  addPin("out", "", "out", PinDirection::kOutput, "c");
  for (auto name : {"launch", "capture"})
    database.get_instance_map()[name].set_is_sequential(true);
  TimingCheckArc check;
  check.set_data_port("capture:D");
  check.set_clock_port("capture:CK");
  check.set_check_type(TimingCheckType::kSetup);
  database.get_instance_map()["capture"].get_check_arc_list().push_back(check);
  database.get_instance_map()["buf"].set_cell_name("BUF");
  TimingCellArc arc;
  arc.set_source_port("A");
  arc.set_sink_port("Y");
  arc.set_is_timing_graph_arc(true);
  database.get_timing_library().get_cell_map()["BUF"].get_cell_arc_list().push_back(arc);

  require(database.get_arc_list().empty());
  require((sdc::findFanin(database, {"b"}, true, true) == std::vector<std::string>{"launch"}));
  require((sdc::findFanout(database, {"a"}, true, true) == std::vector<std::string>{"capture"}));
  require((sdc::findFanout(database, {"capture:Q"}, true, false) == std::vector<std::string>{"out"}));
  require(sdc::findFanout(database, {}, true, true).empty());
  require((sdc::findFanin(database, {"capture:D"}, false, false) == std::vector<std::string>{"buf:A", "buf:Y", "capture:D", "launch:Q"}));
  database.get_timing_library().get_cell_map()["BUF"].get_cell_arc_list()[0].set_is_disable_arc(true);
  require(sdc::findFanout(database, {"launch:Q"}, true, true).empty());

  SdcCommand::initInst();
  require((sdc::getFullNames(database, "launch capture:Q launch") == std::vector<std::string>{"launch", "capture/Q"}));
  require(sdc::getFullNames(database, "").empty());
  SdcCommand::destroyInst();
}
