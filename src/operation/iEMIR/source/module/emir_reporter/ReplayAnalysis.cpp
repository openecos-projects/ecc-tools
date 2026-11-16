// Final solved EM/IR data in native DBU. Disabled replay never traverses graphs.
#include <cstdlib>
#include <filesystem>
#include <fstream>

#include "DataManager.hpp"
#include "EMIRReporter.hpp"
#include "ReplayCapture.h"

namespace iemir {
void EMIRReporter::outputReplayAnalysis()
{
  ecc::replay::guarded([&] {
    using Json = nlohmann::json;
    auto views = Json::array();
    for (auto& [net, graph] : EMIRDM.getDatabase().get_power_graph_map()) {
      auto ir = Json::array();
      auto em = Json::array();
      size_t evaluated = 0, missing = 0;
      for (auto& node : graph.get_node_list()) {
        const auto x = node.get_x(), y = node.get_y();
        ir.push_back(
            {{"label", net + ":" + std::to_string(node.get_node_id())},
             {"value", getIRSeverity(graph, node)},
             {"bboxes", {{x, y, x, y}}},
             {"details", {{"voltage_v", node.get_voltage()}, {"source", node.get_is_source()}, {"layer", getLayerName(graph, node.get_layer_idx())}}}});
      }
      for (auto& edge : graph.get_edge_list()) {
        if (!edge.get_is_em_checkable())
          continue;
        const auto& evaluation = edge.get_em_evaluation();
        const bool checked = evaluation.status == EMEvalStatus::kEvaluated;
        if (checked)
          ++evaluated;
        else
          ++missing;
        auto& first = graph.get_node_list().at(edge.get_first_node_id());
        auto& second = graph.get_node_list().at(edge.get_second_node_id());
        Json item = {{"label", net + ":" + std::to_string(edge.get_edge_id())},
                     {"bboxes",
                      {{std::min(first.get_x(), second.get_x()), std::min(first.get_y(), second.get_y()), std::max(first.get_x(), second.get_x()),
                        std::max(first.get_y(), second.get_y())}}},
                     {"details",
                      {{"current_a", edge.get_current()},
                       {"resistance_ohm", edge.get_resistance()},
                       {"status", emStatusName(evaluation.status)},
                       {"rule", edge.get_em_rule_name()},
                       {"layer", edge.get_layer_name()},
                       {"via", edge.get_via_name()}}}};
        if (checked) {
          item["value"] = edge.get_em_ratio_percent();
          item["details"]["limit_a"] = edge.get_em_limit();
          item["details"]["violation"] = edge.get_is_violation();
        }
        em.push_back(std::move(item));
      }
      views.push_back(
          {{"label", net + " · IR drop"}, {"kind", "ir"}, {"unit", "V"}, {"items", std::move(ir)}, {"summary", {{"nodes", graph.get_node_list().size()}}}});
      views.push_back({{"label", net + " · EM evaluation"},
                       {"kind", "em"},
                       {"unit", "%"},
                       {"items", std::move(em)},
                       {"summary", {{"evaluated", evaluated}, {"not_evaluated", missing}}}});
    }
    const auto directory = std::filesystem::path(std::getenv("ECC_REPLAY_CAPTURE_DIR")) / "analysis";
    std::filesystem::create_directories(directory);
    std::ofstream stream(directory / "emir.json.tmp");
    stream.exceptions(std::ios::failbit | std::ios::badbit);
    stream << Json{{"views", views}}.dump();
    stream.close();
    std::filesystem::rename(directory / "emir.json.tmp", directory / "emir.json");
  });
}
}  // namespace iemir
