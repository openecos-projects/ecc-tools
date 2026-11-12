// SPDX-License-Identifier: MulanPSL-2.0
#include <stdexcept>
#include <string>

#include "Database.hpp"
#include "network/LinkNetwork.hpp"
#include "sdc/ClockEdges.hpp"

namespace {
void require(bool value, const char* message)
{
  if (!value)
    throw std::runtime_error(message);
}
}  // namespace

int main()
{
  using namespace ista;
  Database db;
  auto& network = db.network();
  auto& instance = network.instances()["ff"];
  instance.set_instance_name("ff");
  instance.set_cell_name("DFF");
  for (const auto* name : {"CK", "D", "Q", "QN"}) {
    auto& pin = network.pins()[std::string("ff:") + name];
    pin.set_instance_name("ff");
    pin.set_pin_name(name);
  }
  network.pins()["in"].set_is_port(true);
  network.pins()["in"].set_net_name("stale");
  auto& net = network.nets()["data"];
  net.get_pin_name_list() = {"in", "ff:D", "ff:D"};
  net.set_driver_pin("stale");
  auto& output_net = network.nets()["output"];
  output_net.get_pin_name_list() = {"ff:QN"};
  auto& cell = db.liberty().get_cell_map()["DFF"];
  cell.set_is_sequential(true);
  TimingCellArc clock_arc;
  clock_arc.set_source_port("CK");
  clock_arc.set_sink_port("Q");
  clock_arc.set_is_clock_arc(true);
  clock_arc.set_delay(0.125);
  cell.get_cell_arc_list().push_back(clock_arc);
  TimingCellArc inverted_clock_arc = clock_arc;
  inverted_clock_arc.set_sink_port("QN");
  cell.get_cell_arc_list().push_back(inverted_clock_arc);
  for (auto type : {TimingCheckType::kHold, TimingCheckType::kSetup}) {
    TimingCheckArc check;
    check.set_clock_port("CK");
    check.set_data_port("D");
    check.set_check_type(type);
    check.set_check_time(0.25);
    cell.get_check_arc_list().push_back(check);
  }
  auto& missing = network.instances()["unknown"];
  missing.set_cell_name("missing_library_cell");
  missing.set_output_pin_name("unchanged");
  LinkNetwork linker(network, db.liberty());
  linker.link();
  linker.link();
  require(instance.get_pin_name_list().size() == 4, "repeat link duplicated instance pins");
  require(net.get_load_pin_list().size() == 2 && net.get_driver_pin().empty(), "net reconstruction changed");
  require(network.pins().at("in").get_net_name() == "data", "pin net identity was not rebound");
  require(instance.get_is_sequential() && instance.get_output_pin_name() == "ff:QN", "connected clock-to-output binding lost");
  require(instance.get_check_arc_list().size() == 2 && instance.get_clock_pin_name() == "ff:CK" && instance.get_data_pin_name() == "ff:D",
          "instance check binding lost");
  require(instance.get_check_arc_list().front().get_clock_port() == "ff:CK", "check port was not qualified");
  require(missing.get_output_pin_name() == "unchanged", "missing library changed existing metadata");
  require(clockEdgeSeparation(10, 20, 0, 0, AnalysisType::kMax) == 10, "setup edge separation changed");
  require(clockEdgeSeparation(10, 20, 0, 0, AnalysisType::kMin) == 0, "hold edge separation changed");
  require(launchClockEdge(db, "in", "absent") == 0, "missing-clock edge fallback changed");
}
