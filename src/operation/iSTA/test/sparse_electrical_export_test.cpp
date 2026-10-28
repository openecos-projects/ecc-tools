// SPDX-License-Identifier: MulanPSL-2.0
#include <stdexcept>
#include <string>
#include <tuple>

#include "Database.hpp"
#include "GraphBuilder.hpp"
#include "Logger.hpp"
#include "SparseElectricalExport.hpp"
#include "SparseTimingSnapshot.hpp"
#include "TimingData.hpp"

using namespace ista;

static void require(bool value, const char* message)
{
  if (!value)
    throw std::runtime_error(message);
}

int main()
{
  Logger::initInst();
  Database database;
  TimingData snapshot;
  for (const auto& [instance_name, input, output] : {
           std::tuple{"lat", "D", "Q"}, std::tuple{"merge", "A", "Y"}}) {
    auto& instance = database.network().instances()[instance_name];
    instance.set_instance_name(instance_name);
    instance.set_cell_name(instance_name);
    auto& cell = database.liberty().get_cell_map()[instance_name];
    cell.set_cell_name(instance_name);
    for (const auto* port_name : {input, output}) {
      const std::string name = std::string(instance_name) + ":" + port_name;
      auto& pin = database.network().pins()[name];
      pin.set_full_name(name);
      pin.set_pin_name(port_name);
      pin.set_instance_name(instance_name);
      pin.set_direction(port_name == input ? PinDirection::kInput : PinDirection::kOutput);
      instance.get_pin_name_list().push_back(name);
      auto& port = cell.get_port_map()[port_name];
      port.set_is_input(port_name == input);
      port.set_is_output(port_name == output);
    }
    TimingCellArc arc;
    arc.set_source_port(input);
    arc.set_sink_port(output);
    cell.get_cell_arc_list().push_back(arc);
    TimingGraphArcData base;
    base.source_pin = std::string(instance_name) + ":" + input;
    base.sink_pin = std::string(instance_name) + ":" + output;
    base.arc_type = 1;
    base.sense = 1;
    base.library_arc_id = 0;
    snapshot.graph_arcs.push_back(base);
  }
  auto& net = database.network().nets()["connection"];
  net.set_net_name("connection");
  net.set_pin_name_list({"lat:Q", "merge:A"});
  for (const auto* name : {"lat:Q", "merge:A"})
    database.network().pins().at(name).set_net_name("connection");
  snapshot.net_arcs.push_back({"lat:Q", "merge:A"});
  GraphBuilder(database).build();

  SparseTimingSnapshot view;
  view.domains = {"core", ""};
  view.tags = {"core"};
  view.launch_tags = {"core"};
  for (const auto* name : {"lat:D", "merge:Y"}) {
    SparseTimingState state;
    state.pin = name;
    state.domain = state.tag = state.launch_tag = 0;
    state.source_type = 1;
    state.level = database.graph().points().at(name).get_level();
    state.slew_group = view.states.size();
    state.valid = {true, true};
    view.states.push_back(state);
  }
  appendSparseElectricalEdges(snapshot, database, view);
  require(view.unsupported.empty(), "electrical chain was not a forward DAG");
  require(view.states.size() == 4 && view.slew_only_cell_edges.size() == 2 && view.net_edges.size() == 1,
          "electrical path through pins without arrival states was omitted");
  require(view.net_edges.front().transition_mask == 0, "electrical net acquired an arrival transition");
  require(view.checks.empty(), "electrical path created a timing check");
  for (std::size_t index = 2; index < view.states.size(); ++index) {
    const auto& state = view.states[index];
    require(state.source_type == 0 && !state.valid[0] && !state.valid[1] && !state.seed[0] && !state.seed[1],
            "electrical-only state acquired a data launch or arrival");
  }
  Logger::destroyInst();
}
