// Full precision solved IR samples for both the standalone viewer and Replay.
#include <filesystem>
#include <fstream>

#include "DataManager.hpp"
#include "EMIRReporter.hpp"
#include "PowerNetType.hpp"

namespace iemir {
nlohmann::json EMIRReporter::buildIRView(PowerGraph& graph)
{
  using Json = nlohmann::json;
  auto items = Json::array();
  const auto& net = graph.get_net_name();
  double ideal = graph.get_source_voltage();
  if (graph.get_net_type() == PowerNetType::kPower && ideal <= EMIR_ERROR) {
    ideal = getSupplyVoltage(graph);
  }
  for (auto& node : graph.get_node_list()) {
    const auto x = node.get_x(), y = node.get_y();
    items.push_back({{"label", net + ":" + std::to_string(node.get_node_id())},
                     {"value", getIRSeverity(graph, node)},
                     {"bboxes", {{x, y, x, y}}},
                     {"details",
                      {{"voltage_v", node.get_voltage()},
                       {"ideal_voltage_v", ideal},
                       {"source", node.get_is_source()},
                       {"layer", getLayerName(graph, node.get_layer_idx())}}}});
  }
  return {{"label", net + " · IR drop"},
          {"kind", "ir"},
          {"basis", "final"},
          {"unit", "V"},
          {"items", std::move(items)},
          {"summary",
           {{"net", net},
            {"nodes", graph.get_node_list().size()},
            {"quantity", graph.get_net_type() == PowerNetType::kGround ? "ground_bounce" : "voltage_drop"}}}};
}

void EMIRReporter::outputIRSpatial()
{
  auto views = nlohmann::json::array();
  for (auto& [net, graph] : EMIRDM.getDatabase().get_power_graph_map()) {
    views.push_back(buildIRView(graph));
  }
  const auto path = std::filesystem::path(EMIRDM.getConfig().er_temp_directory_path) / "ir_drop.json";
  const auto temporary = path.string() + ".tmp";
  std::ofstream stream;
  stream.exceptions(std::ios::failbit | std::ios::badbit);
  stream.open(temporary);
  stream << nlohmann::json{{"schema", "ecos.emir.ir.v1"},
                           {"coordinate_unit", "dbu"},
                           {"dbu_per_micron", EMIRDM.getDatabase().get_micron_dbu()},
                           {"views", std::move(views)}}
                .dump();
  stream.close();
  std::filesystem::rename(temporary, path);
}
}  // namespace iemir
