// SPDX-License-Identifier: MulanPSL-2.0
#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "Database.hpp"
#include "graph/Graph.hpp"
#include "liberty/LibertyLibrary.hpp"
#include "network/Network.hpp"
#include "sdc/ExceptionMatcher.hpp"
#include "sdc/Sdc.hpp"

namespace {
using namespace ista;
void require(bool value, const char* message)
{
  if (!value)
    throw std::runtime_error(message);
}

Network makeNetwork()
{
  Network network;
  network.setName("top");
  auto& input = network.pins()["in"];
  input.set_is_port(true);
  input.set_direction(PinDirection::kInput);
  input.set_net_name("n");
  auto& load = network.pins()["b/u:A"];
  load.set_instance_name("b/u");
  load.set_pin_name("A");
  load.set_direction(PinDirection::kInput);
  load.set_net_name("n");
  network.instances()["b/u"].set_cell_name("BUF");
  network.nets()["n"].set_pin_name_list({"in", "b/u:A"});
  network.hierarchyIndices()["b/u"] = 0;
  auto& boundary = network.hierarchyPins()["b/A"];
  boundary.instance = "b";
  boundary.port = "A";
  boundary.net = "n";
  boundary.direction = PinDirection::kInput;
  boundary.leaf_begin = 0;
  boundary.leaf_end = 1;
  boundary.depth = 1;
  return network;
}

void networkOwnership()
{
  auto original = makeNetwork();
  auto copied = original;
  original.pins().at("in").set_net_name("other");
  Network moved = std::move(copied);
  const auto& network = moved;
  require(network.name() == "top", "network name lost on move");
  require(network.hierarchyDrivers("b/A") == std::vector<std::string>{"in"}, "copied hierarchy still references original");
  require(network.hierarchyCrossingPosition(network.checkedHierarchyPin("b/A"), "in", "b/u:A") == 1, "incoming hierarchy crossing changed");
  LibertyLibrary library;
  library.get_cell_map()["BUF"].get_port_map()["A"].set_fanout_load(2.5);
  const auto* port = network.libertyPort(library, network.pins().at("b/u:A"));
  require(port && port->get_fanout_load() == 2.5, "explicit library lookup changed");
  require(!network.libertyPort(library, network.pins().at("in")), "missing instance unexpectedly found a library port");
}

void graphOwnership()
{
  Graph graph;
  graph.points()["io"].set_arrival(2.0);
  graph.driverPoints()["io"].set_arrival(7.0);
  graph.points()["other"].set_arrival(3.0);
  Arc arc;
  arc.set_source_pin("io");
  arc.set_sink_pin("io");
  arc.set_type(ArcType::kNet);
  require(&graph.sourcePoint(arc) == &graph.driverPoints().at("io"), "net source lost driver identity");
  require(&graph.sinkPoint(arc) == &graph.points().at("io"), "net sink lost receiver identity");
  arc.set_type(ArcType::kCell);
  require(&graph.sourcePoint(arc) == &graph.points().at("io"), "cell source lost receiver identity");
  require(&graph.sinkPoint(arc) == &graph.driverPoints().at("io"), "cell sink lost driver identity");
  require(&graph.driverPoint("other") == &graph.points().at("other"), "driver fallback changed");
  for (const auto& [name, driver] : {std::pair{"io", false}, std::pair{"io", true}, std::pair{"other", true}}) {
    auto& vertex = graph.vertices().emplace_back();
    vertex.set_pin_name(name);
    vertex.set_is_driver(driver);
    vertex.set_timing_point(driver ? &graph.driverPoint(name) : &graph.points().at(name));
  }
  auto copy = graph;
  require(&copy.vertices()[0].get_timing_point() == &copy.points().at("io"), "copied receiver vertex refers to original graph");
  require(&copy.vertices()[1].get_timing_point() == &copy.driverPoints().at("io"), "copied driver vertex refers to original graph");
  require(&copy.vertices()[2].get_timing_point() == &copy.points().at("other"), "copied ordinary driver lost its point");
  Graph assigned;
  assigned = graph;
  assigned = assigned;
  require(&assigned.vertices()[1].get_timing_point() == &assigned.driverPoints().at("io"), "copy assignment lost vertex ownership");
  graph.driverPoints().clear();
  Graph moved = std::move(copy);
  require(moved.sinkPoint(arc).get_arrival() == 7.0 && moved.sourcePoint(arc).get_arrival() == 2.0, "copied graph refers to original graph state");
  moved.clearTiming();
  require(!std::isfinite(moved.driverPoints().at("io").get_arrival()), "clearTiming did not clear moved graph's own driver state");
  require(assigned.driverPoints().at("io").get_arrival() == 7.0, "clearing a graph changed another graph");
}

void fallingClock()
{
  Network network;
  auto& output = network.pins()["ff:Q"];
  output.set_instance_name("ff");
  auto& instance = network.instances()["ff"];
  instance.set_is_sequential(true);
  TimingArc arc;
  arc.set_trigger_trans_type(TransType::kFall);
  instance.get_clock_to_q_arc().get_timing_arc_list().push_back(arc);
  Sdc sdc;
  TimingException exception;
  exception.set_from_objects({{ExceptionObjectType::kClock, "clk"}});
  exception.set_from_trans_type(TransType::kFall);
  sdc.get_path_exception_list().push_back(exception);
  const ExceptionMatcher matcher(sdc, network);
  const auto state = matcher.initState("ff:Q", "clk", TransType::kRise, AnalysisType::kMax, TransType::kFall);
  require(state == std::vector<int32_t>{0}, "clock selector used data transition instead of falling launch edge");
  require(matcher.makePathStateTag("clk", TransType::kFall, state) == std::string("clk\x1e\x1f") + "0", "falling clock path tag changed");
  require(network.launchClockTransition("missing") == TransType::kRise, "launch transition fallback changed");
}

void databaseOwnership()
{
  Database copy;
  {
    Database original;
    original.network() = makeNetwork();
    original.graph().points()["in"].set_arrival(4.0);
    copy = original;
  }
  Database moved = std::move(copy);
  require(moved.network().hierarchyDrivers("b/A") == std::vector<std::string>{"in"}, "Database retained a dead owner reference");
  require(moved.graph().points().at("in").get_arrival() == 4.0, "Database graph move lost timing state");
}
}  // namespace

int main()
{
  networkOwnership();
  graphOwnership();
  fallingClock();
  databaseOwnership();
}
