// Final solved EM data in native DBU for geometry and Replay.
#include <cstdlib>
#include <filesystem>
#include <fstream>

#include "DataManager.hpp"
#include "EMIRReporter.hpp"
#include "GeometryDisplayWriter.h"

namespace iemir {
void EMIRReporter::outputReplayAnalysis()
{
  {
    using Json = nlohmann::json;
    uint32_t network = 0;
    for (auto& [net, graph] : EMIRDM.getDatabase().get_power_graph_map()) {
      using Writer = ecc::geometry::GeometryDisplayWriter;
      const auto key = "em-" + std::to_string(network++);
      Writer binary(std::filesystem::path(EMIRDM.getConfig().er_temp_directory_path) / (key + ".gdisplay"),
                    {{"kind", "em"}, {"key", key}, {"label", net + " · EM evaluation"}, {"unit", "%"}}, Writer::Encoding::kRecords);
      for (auto& edge : graph.get_edge_list()) {
        if (!edge.get_is_em_checkable())
          continue;
        const auto& evaluation = edge.get_em_evaluation();
        const bool checked = evaluation.status == EMEvalStatus::kEvaluated;
        if (edge.get_first_node_id() < 0 || edge.get_second_node_id() < 0 || static_cast<size_t>(edge.get_first_node_id()) >= graph.get_node_list().size()
            || static_cast<size_t>(edge.get_second_node_id()) >= graph.get_node_list().size())
          continue;
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
        binary.record(item);
      }
      binary.finish();
    }
  }
}
}  // namespace iemir
