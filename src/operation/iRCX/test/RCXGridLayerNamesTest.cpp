#include <cmath>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <tuple>
#include <vector>

#include "CapExtractor.hpp"
#include "Utility.hpp"

using namespace ircx;

namespace {

void checkLayerNames(const std::string& geometry)
{
  DataManager::destroyInst();
  DataManager::initInst();
  auto& db = RCXDM.getDatabase();
  RCXDM.getConfig().thread_number = 3;
  db.get_layout_data().set_dbu_per_micron(10);
  auto process = std::make_shared<ProcessTechnology>();
  auto& layers = db.get_layer_table();
  for (const auto& [id, design, name] : {std::tuple{7, "MET1", "M1"}, std::tuple{14, "MET2", "M2"},
                                        std::tuple{21, "T4M2", "TM"}, std::tuple{28, "RDL", "RDL"}}) {
    layers.register_design_layer(id, design);
    layers.register_process_layer(id, name);
    layers.register_mapping(design, name);
    ProcessConductor conductor;
    conductor.set_layer_name(name);
    if (geometry == "side_tangent") conductor.set_side_tangent(.1);
    if (geometry == "polynomial") conductor.get_thickness_polynomial().emplace();
    process->conductors.push_back(conductor);
  }

  struct Context
  {
    int master, below, above;
    const char* header;
  };
  const std::vector<Context> contexts{
      {7, 0, -1, "P MET1 SUBSTRATE - .5"}, {7, 0, 14, "P MET1 SUBSTRATE MET2 .5"},
      {14, 7, -1, "P MET2 MET1 - .5"}, {14, 0, 21, "P MET2 SUBSTRATE T4M2 .5"},
      {14, 7, 21, "P MET2 MET1 T4M2 .5"}, {21, 14, -1, "P T4M2 MET2 - .5"},
      {28, 21, -1, "P RDL T4M2 - .5"}, {28, 0, -1, "P RDL SUBSTRATE - .5"}};
  std::ostringstream input;
  db.get_layout_data().get_net_list().resize(contexts.size());
  db.get_net_env_list().resize(contexts.size());
  for (int net = 0; net < static_cast<int>(contexts.size()); ++net) {
    const auto& context = contexts[net];
    input << context.header << '\n';
    for (int left : {-1, 1})
      for (int right : {-1, 1})
        input << left << ' ' << right << ' ' << net + 1 << " 0 0\n";
    TopoEdge edge(net);
    edge.set_layer_idx(context.master);
    // 10 um wires, spaced 20 um apart to isolate the layer-name lookup.
    edge.set_shape(GTLRectInt(0, net * 200, 100, net * 200 + 5));
    db.get_topo_pool().add_net({}, {edge});
    EdgeEnvInterval interval;
    interval.set_start_coord(0);
    interval.set_end_coord(100);
    CrossLayerOverlap crossing;
    crossing.set_start_coord(0);
    crossing.set_end_coord(100);
    crossing.set_below_layer_idx(context.below);
    crossing.set_above_layer_idx(context.above);
    interval.get_cross_layer_overlap_list().push_back(crossing);
    db.get_net_env_list()[net].append_edge_interval_list({interval});
  }
  auto grid = std::make_shared<pattern::PatternGrid>();
  std::istringstream stream(input.str());
  grid->read(stream);
  db.get_corner_data_list().resize(1);
  db.get_corner_data_list()[0].set_process_technology(process);
  db.get_corner_data_list()[0].set_pattern_grid(grid);
  db.get_rc_data().init(1, contexts.size(), db.get_topo_pool());
  RCXCE.extract();
  for (int net = 0; net < static_cast<int>(contexts.size()); ++net) {
    const auto& context = contexts[net];
    const double coefficient = net + 1;
    const auto& ground = db.get_rc_data().get_corner_net_ground_capacitance_list(CornerNetIdx(0, net));
    if (ground.size() != 1 || std::abs(ground[0] - 10 * coefficient) > 1e-10)
      throw std::runtime_error(std::string("incorrect design-layer grid result: ") + context.header);
  }
}

}  // namespace

int main(int argc, char** argv)
{
  setenv("ECC_LOGGER_THROW_ON_ERROR", "1", 1);
  for (const char* name : {"IRCX_MULTI_MODE", "IRCX_MULTI_RANK", "IRCX_PATTERN_GRID", "IRCX_PATTERN_TABLE", "IRCX_MULTI_OUTPUT",
                           "IRCX_XLAYER_TABLE"}) unsetenv(name);
  Logger::initInst();
  Utility::initInst();
  CapExtractor::initInst();
  const std::string geometry = argc > 1 ? argv[1] : "";
  int result = EXIT_SUCCESS;
  try {
    checkLayerNames(geometry);
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    result = EXIT_FAILURE;
  }
  CapExtractor::destroyInst();
  DataManager::destroyInst();
  Utility::destroyInst();
  Logger::destroyInst();
  return result;
}
