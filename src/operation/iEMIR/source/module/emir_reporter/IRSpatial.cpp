// Full precision solved IR samples for both the standalone viewer and Replay.
#include <array>
#include <filesystem>
#include <fstream>
#include <limits>
#include <map>

#include "DataManager.hpp"
#include "EMIRReporter.hpp"
#include "GeometryDisplayWriter.h"
#include "PowerNetType.hpp"

namespace iemir {
void EMIRReporter::outputIRSpatial()
{
  using Writer = ecc::geometry::GeometryDisplayWriter;
  using Json = nlohmann::json;
  const auto directory = std::filesystem::path(EMIRDM.getConfig().er_temp_directory_path);
  // Retain the public text export for scripts and inspection, but stream nodes
  // so compatibility output does not create a second in-memory result tree.
  const auto text_path = directory / "ir_drop.json";
  std::ofstream text;
  text.exceptions(std::ios::failbit | std::ios::badbit);
  text.open(text_path.string() + ".tmp");
  text << "{\"schema\":\"ecos.emir.ir.v1\",\"coordinate_unit\":\"dbu\",\"dbu_per_micron\":" << EMIRDM.getDatabase().get_micron_dbu() << ",\"views\":[";
  uint32_t network = 0;
  for (auto& [net, graph] : EMIRDM.getDatabase().get_power_graph_map()) {
    if (graph.get_node_list().empty())
      continue;
    int64_t lx = INT32_MAX, ly = INT32_MAX, hx = INT32_MIN, hy = INT32_MIN;
    double maximum = 0;
    for (auto& node : graph.get_node_list()) {
      lx = std::min(lx, int64_t(node.get_x()));
      ly = std::min(ly, int64_t(node.get_y()));
      hx = std::max(hx, int64_t(node.get_x()));
      hy = std::max(hy, int64_t(node.get_y()));
      maximum = std::max(maximum, getIRSeverity(graph, node));
    }
    if (hx == INT32_MAX || hy == INT32_MAX)
      throw std::runtime_error("IR spatial bounds overflow");
    ++hx;
    ++hy;
    const int64_t width = hx - lx, height = hy - ly;
    const int64_t pitch = std::max(int64_t(1), (std::max(width, height) + 127) / 128);
    const uint32_t cols = (width + pitch - 1) / pitch, rows = (height + pitch - 1) / pitch;
    if (network)
      text << ',';
    text << "{\"kind\":\"ir\",\"basis\":\"final\",\"unit\":\"V\",\"label\":" << Json(net + " · IR drop").dump() << ",\"summary\":"
         << Json({{"net", net},
                  {"nodes", graph.get_node_list().size()},
                  {"quantity", graph.get_net_type() == PowerNetType::kGround ? "ground_bounce" : "voltage_drop"}})
                .dump()
         << ",\"items\":[";
    bool first_node = true;
    const auto prefix = "ir-" + std::to_string(network++);
    Json common = {{"kind", "ir"},
                   {"key", prefix},
                   {"label", net + " · IR drop"},
                   {"network", net},
                   {"unit", "V"},
                   {"coordinate_unit", "dbu"},
                   {"dbu_per_micron", EMIRDM.getDatabase().get_micron_dbu()},
                   {"rows", rows},
                   {"columns", cols},
                   {"maximum", maximum},
                   {"nodes", graph.get_node_list().size()}};
    Json raw_meta = common;
    raw_meta["kind"] = "ir_nodes";
    raw_meta["key"] = prefix + ":nodes";
    Writer raw(directory / (prefix + "-nodes.gdisplay"), raw_meta, Writer::Encoding::kRecords);
    std::map<std::string, std::map<uint64_t, double>> grids;
    double ideal = graph.get_source_voltage();
    if (graph.get_net_type() == PowerNetType::kPower && ideal <= EMIR_ERROR)
      ideal = getSupplyVoltage(graph);
    for (auto& node : graph.get_node_list()) {
      const double value = getIRSeverity(graph, node);
      const auto layer = getLayerName(graph, node.get_layer_idx());
      if (!first_node)
        text << ',';
      first_node = false;
      text << Json({{"label", net + ":" + std::to_string(node.get_node_id())},
                    {"value", value},
                    {"bboxes", {{node.get_x(), node.get_y(), node.get_x(), node.get_y()}}},
                    {"details", {{"voltage_v", node.get_voltage()}, {"ideal_voltage_v", ideal}, {"source", node.get_is_source()}, {"layer", layer}}}})
                  .dump();
      raw.record({{"x", node.get_x()},
                  {"y", node.get_y()},
                  {"layer", layer},
                  {"value", value},
                  {"voltage_v", node.get_voltage()},
                  {"ideal_voltage_v", ideal},
                  {"source", node.get_is_source()}});
      const uint32_t col = ((int64_t(node.get_x()) - lx + 1) * cols - 1) / width;
      const uint32_t row = rows - 1 - ((int64_t(node.get_y()) - ly + 1) * rows - 1) / height;
      const uint64_t cell = uint64_t(row) * cols + col;
      for (const auto& key : {std::string{}, layer}) {
        auto [it, inserted] = grids[key].emplace(cell, value);
        if (!inserted)
          it->second = std::max(it->second, value);
      }
    }
    text << "]}";
    raw.finish();
    uint32_t layer_id = 0;
    for (const auto& [layer, cells] : grids) {
      auto meta = common;
      meta["layer"] = layer;
      meta["key"] = prefix + ":" + layer;
      Writer writer(directory / (prefix + "-" + std::to_string(layer_id++) + ".gdisplay"), meta, Writer::Encoding::kCells);
      for (const auto& [index, value] : cells) {
        const uint32_t row = index / cols, col = index % cols;
        writer.cell(row, col,
                    {int32_t(lx + width * col / cols), int32_t(ly + height * (rows - row - 1) / rows), int32_t(lx + width * (col + 1) / cols),
                     int32_t(ly + height * (rows - row) / rows)},
                    value);
      }
      writer.finish();
    }
  }
  text << "]}";
  text.close();
  std::filesystem::rename(text_path.string() + ".tmp", text_path);
}
}  // namespace iemir
