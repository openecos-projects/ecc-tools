// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <boost/json.hpp>

#include "../../benchmarks/DrtTrace.h"
#include "odb/db.h"

// Shared, bounded projections of the ODB data read by TritonRoute's Parser.
// Keep these as data projections rather than wrappers around Parser objects so
// a replay can query an independently imported ODB database with identical
// field ordering.
namespace eccdb::tritonroute::trace_projection {
namespace json = boost::json;

inline json::value layerName(const odb::dbTechLayer* layer)
{
  return layer == nullptr ? json::value(nullptr) : json::value(layer->getName());
}

inline json::array rectangle(const odb::Rect& rect)
{
  return json::array{rect.xMin(), rect.yMin(), rect.xMax(), rect.yMax()};
}

inline json::value minStep(odb::dbTechLayer* layer)
{
  if (!layer->hasMinStep()) {
    return nullptr;
  }
  return json::array{
      static_cast<int64_t>(layer->getMinStep()),
      std::string(layer->getMinStepType().getString()),
      layer->hasMinStepMaxLength()
          ? json::value(static_cast<int64_t>(layer->getMinStepMaxLength()))
          : json::value(nullptr),
      layer->hasMinStepMaxEdges()
          ? json::value(static_cast<int64_t>(layer->getMinStepMaxEdges()))
          : json::value(nullptr)};
}

inline json::value routingV54SpacingRule(odb::dbTechLayerSpacingRule* rule)
{
  uint32_t eol_width = 0;
  uint32_t eol_within = 0;
  uint32_t parallel_space = 0;
  uint32_t parallel_within = 0;
  bool parallel_edge = false;
  bool two_edges = false;
  const bool has_eol = rule->getEol(eol_width,
                                    eol_within,
                                    parallel_edge,
                                    parallel_space,
                                    parallel_within,
                                    two_edges);

  json::value range = nullptr;
  if (rule->hasRange()) {
    uint32_t minimum = 0;
    uint32_t maximum = 0;
    rule->getRange(minimum, maximum);
    range = json::array{static_cast<int64_t>(minimum),
                        static_cast<int64_t>(maximum)};
  }

  json::value eol = nullptr;
  if (has_eol) {
    eol = json::array{static_cast<int64_t>(eol_width),
                      static_cast<int64_t>(eol_within),
                      parallel_edge,
                      static_cast<int64_t>(parallel_space),
                      static_cast<int64_t>(parallel_within),
                      two_edges};
  }

  const bool cut_same_net = rule->getCutSameNet();
  json::value same_net_pg_only = nullptr;
  if (cut_same_net) {
    same_net_pg_only = rule->getSameNetPgOnly();
  }

  // This exactly covers the LEF 5.4 branches Parser::addRoutingLayer reads.
  return json::array{static_cast<int64_t>(rule->getSpacing()),
                     std::move(range),
                     std::move(eol),
                     cut_same_net,
                     std::move(same_net_pg_only),
                     rule->hasLengthThreshold(),
                     rule->hasSpacingNotchLength(),
                     rule->hasSpacingEndOfNotchWidth()};
}

inline json::value cutV54SpacingRule(odb::dbTechLayerSpacingRule* rule)
{
  odb::dbTechLayer* second_layer = nullptr;
  json::value adjacent_cuts = nullptr;
  uint32_t count = 0;
  uint32_t within = 0;
  uint32_t spacing = 0;
  bool except_same_pg = false;
  if (rule->getAdjacentCuts(count, within, spacing, except_same_pg)) {
    adjacent_cuts = json::array{static_cast<int64_t>(count),
                                static_cast<int64_t>(within),
                                static_cast<int64_t>(spacing),
                                except_same_pg};
  }

  json::value layer4 = nullptr;
  if (rule->getCutLayer4Spacing(second_layer)) {
    layer4 = layerName(second_layer);
  }

  return json::array{static_cast<int64_t>(rule->getSpacing()),
                     static_cast<int64_t>(rule->getCutArea()),
                     rule->getCutCenterToCenter(),
                     rule->getCutSameNet(),
                     rule->getCutStacking(),
                     rule->getSameNetPgOnly(),
                     rule->getCutParallelOverlap(),
                     std::move(layer4),
                     std::move(adjacent_cuts)};
}

inline json::value minEnclosureRule(odb::dbTechMinEncRule* rule)
{
  uint32_t width = 0;
  json::value enclosure_width = nullptr;
  if (rule->getEnclosureWidth(width)) {
    enclosure_width = static_cast<int64_t>(width);
  }
  int64_t area = 0;
  rule->getEnclosure(area);
  return json::array{area, std::move(enclosure_width)};
}

inline json::value minimumCutRule(odb::dbTechMinCutRule* rule)
{
  uint32_t cuts = 0;
  uint32_t width = 0;
  if (!rule->getMinimumCuts(cuts, width)) {
    return nullptr;
  }
  uint32_t distance = 0;
  json::value cut_distance = nullptr;
  if (rule->getCutDistance(distance)) {
    cut_distance = static_cast<int64_t>(distance);
  }
  uint32_t length = 0;
  json::value length_for_cuts = nullptr;
  if (rule->getLengthForCuts(length, distance)) {
    length_for_cuts = json::array{static_cast<int64_t>(length),
                                  static_cast<int64_t>(distance)};
  }
  return json::array{static_cast<int64_t>(cuts),
                     static_cast<int64_t>(width),
                     std::move(cut_distance),
                     rule->isAboveOnly(),
                     rule->isBelowOnly(),
                     std::move(length_for_cuts)};
}

inline json::value v55Spacing(odb::dbTechLayer* layer)
{
  if (!layer->hasV55SpacingRules()) {
    return nullptr;
  }
  std::vector<uint32_t> widths;
  std::vector<uint32_t> lengths;
  std::vector<std::vector<uint32_t>> table;
  layer->getV55SpacingWidthsAndLengths(widths, lengths);
  layer->getV55SpacingTable(table);

  json::array json_widths;
  json::array json_lengths;
  json::array json_table;
  for (const auto value : widths) {
    json_widths.emplace_back(static_cast<int64_t>(value));
  }
  for (const auto value : lengths) {
    json_lengths.emplace_back(static_cast<int64_t>(value));
  }
  for (const auto& row : table) {
    json::array json_row;
    for (const auto value : row) {
      json_row.emplace_back(static_cast<int64_t>(value));
    }
    json_table.emplace_back(std::move(json_row));
  }
  return json::array{std::move(json_widths), std::move(json_lengths),
                     std::move(json_table)};
}

inline json::value twoWidthsSpacing(odb::dbTechLayer* layer)
{
  if (!layer->hasTwoWidthsSpacingRules()) {
    return nullptr;
  }
  std::vector<std::vector<uint32_t>> table;
  layer->getTwoWidthsSpacingTable(table);

  json::array rows;
  const auto row_count = layer->getTwoWidthsSpacingTableNumWidths();
  for (uint32_t index = 0; index < row_count; ++index) {
    const bool has_prl = layer->getTwoWidthsSpacingTableHasPRL(index);
    rows.emplace_back(json::array{
        static_cast<int64_t>(layer->getTwoWidthsSpacingTableWidth(index)),
        has_prl,
        has_prl ? json::value(static_cast<int64_t>(
                      layer->getTwoWidthsSpacingTablePRL(index)))
                : json::value(nullptr)});
  }
  json::array json_table;
  for (const auto& row : table) {
    json::array json_row;
    for (const auto value : row) {
      json_row.emplace_back(static_cast<int64_t>(value));
    }
    json_table.emplace_back(std::move(json_row));
  }
  return json::array{std::move(rows), std::move(json_table)};
}

inline json::array techLayers(odb::dbTech* tech)
{
  json::array values;
  for (auto* layer : tech->getLayers()) {
    if (layer->isBackside()) {
      continue;
    }
    values.emplace_back(json::array{layer->getName(),
                                    std::string(layer->getType().getString()),
                                    static_cast<int64_t>(layer->getNumber()),
                                    static_cast<int64_t>(layer->getRoutingLevel()),
                                    std::string(layer->getDirection().getString())});
  }
  return drt_trace::canonical(std::move(values));
}

inline json::array routingLayerRules(odb::dbTech* tech)
{
  json::array values;
  for (auto* layer : tech->getLayers()) {
    if (layer->isBackside()
        || layer->getType() != odb::dbTechLayerType::ROUTING
        || layer->getRoutingLevel() == 0) {
      continue;
    }

    json::array v54_rules;
    for (auto* rule : layer->getV54SpacingRules()) {
      v54_rules.emplace_back(routingV54SpacingRule(rule));
    }

    json::array influence_rules;
    for (auto* rule : layer->getV55InfluenceRules()) {
      uint32_t width = 0;
      uint32_t within = 0;
      uint32_t spacing = 0;
      rule->getV55InfluenceEntry(width, within, spacing);
      influence_rules.emplace_back(json::array{static_cast<int64_t>(width),
                                               static_cast<int64_t>(within),
                                               static_cast<int64_t>(spacing)});
    }

    json::array minimum_cuts;
    for (auto* rule : layer->getMinCutRules()) {
      if (auto value = minimumCutRule(rule); !value.is_null()) {
        minimum_cuts.emplace_back(std::move(value));
      }
    }

    json::array minimum_enclosures;
    std::vector<odb::dbTechMinEncRule*> rules;
    layer->getMinEnclosureRules(rules);
    for (auto* rule : rules) {
      minimum_enclosures.emplace_back(minEnclosureRule(rule));
    }

    values.emplace_back(json::array{
        layer->getName(),
        static_cast<int64_t>(layer->getWidth()),
        static_cast<int64_t>(layer->getMinWidth()),
        layer->hasArea() ? json::value(layer->getArea()) : json::value(nullptr),
        minStep(layer),
        drt_trace::canonical(std::move(v54_rules)),
        drt_trace::canonical(std::move(influence_rules)),
        v55Spacing(layer),
        twoWidthsSpacing(layer),
        drt_trace::canonical(std::move(minimum_cuts)),
        drt_trace::canonical(std::move(minimum_enclosures))});
  }
  return drt_trace::canonical(std::move(values));
}

inline json::array cutLayerRules(odb::dbTech* tech)
{
  json::array values;
  for (auto* layer : tech->getLayers()) {
    if (layer->isBackside() || layer->getType() != odb::dbTechLayerType::CUT
        || layer->getLef58Type() == odb::dbTechLayer::LEF58_TYPE::MIMCAP) {
      continue;
    }
    auto* lower_layer = layer->getLowerLayer();
    if (lower_layer != nullptr
        && lower_layer->getType() == odb::dbTechLayerType::CUT) {
      continue;
    }

    json::array v54_rules;
    for (auto* rule : layer->getV54SpacingRules()) {
      v54_rules.emplace_back(cutV54SpacingRule(rule));
    }

    json::value orthogonal_table = nullptr;
    if (layer->hasOrthSpacingTable()) {
      std::vector<std::pair<int, int>> entries;
      layer->getOrthSpacingTable(entries);
      json::array table;
      for (const auto& [within, spacing] : entries) {
        table.emplace_back(json::array{within, spacing});
      }
      orthogonal_table = drt_trace::canonical(std::move(table));
    }

    values.emplace_back(json::array{layer->getName(),
                                    drt_trace::canonical(std::move(v54_rules)),
                                    std::move(orthogonal_table)});
  }
  return drt_trace::canonical(std::move(values));
}

inline json::value viaParams(const odb::dbViaParams& params)
{
  return json::array{layerName(params.getBottomLayer()),
                     layerName(params.getCutLayer()),
                     layerName(params.getTopLayer()),
                     params.getXCutSize(),
                     params.getYCutSize(),
                     params.getXCutSpacing(),
                     params.getYCutSpacing(),
                     params.getXTopEnclosure(),
                     params.getYTopEnclosure(),
                     params.getXBottomEnclosure(),
                     params.getYBottomEnclosure(),
                     params.getNumCutRows(),
                     params.getNumCutCols(),
                     params.getXOrigin(),
                     params.getYOrigin(),
                     params.getXTopOffset(),
                     params.getYTopOffset(),
                     params.getXBottomOffset(),
                     params.getYBottomOffset()};
}

template <typename Via>
inline json::value via(Via* value)
{
  auto* rule = value->getViaGenerateRule();
  const bool has_params = value->hasParams();
  std::string kind;
  if (rule != nullptr && has_params) {
    kind = "generated";
  } else if (rule != nullptr) {
    kind = "rule_without_params";
  } else if (has_params) {
    kind = "params_without_rule";
  } else {
    kind = "rectangles";
  }

  json::array rectangles;
  for (auto* box : value->getBoxes()) {
    rectangles.emplace_back(json::array{layerName(box->getTechLayer()),
                                        box->xMin(),
                                        box->yMin(),
                                        box->xMax(),
                                        box->yMax()});
  }

  return json::array{value->getName(),
                     value->isDefault(),
                     std::move(kind),
                     rule == nullptr ? json::value(nullptr)
                                     : json::value(rule->getName()),
                     has_params ? viaParams(value->getViaParams())
                                : json::value(nullptr),
                     drt_trace::canonical(std::move(rectangles))};
}

inline json::array techVias(odb::dbTech* tech)
{
  json::array values;
  for (auto* value : tech->getVias()) {
    values.emplace_back(via(value));
  }
  return drt_trace::canonical(std::move(values));
}

inline json::array designVias(odb::dbBlock* block)
{
  json::array values;
  for (auto* value : block->getVias()) {
    values.emplace_back(via(value));
  }
  return drt_trace::canonical(std::move(values));
}

inline json::value viaLayerRule(odb::dbTechViaLayerRule* rule)
{
  json::value width = nullptr;
  if (rule->hasWidth()) {
    int minimum = 0;
    int maximum = 0;
    rule->getWidth(minimum, maximum);
    width = json::array{minimum, maximum};
  }
  json::value enclosure = nullptr;
  if (rule->hasEnclosure()) {
    int x = 0;
    int y = 0;
    rule->getEnclosure(x, y);
    enclosure = json::array{x, y};
  }
  json::value overhang = nullptr;
  if (rule->hasOverhang()) {
    overhang = rule->getOverhang();
  }
  json::value metal_overhang = nullptr;
  if (rule->hasMetalOverhang()) {
    metal_overhang = rule->getMetalOverhang();
  }
  json::value rect = nullptr;
  if (rule->hasRect()) {
    odb::Rect value;
    rule->getRect(value);
    rect = rectangle(value);
  }
  json::value spacing = nullptr;
  if (rule->hasSpacing()) {
    int x = 0;
    int y = 0;
    rule->getSpacing(x, y);
    spacing = json::array{x, y};
  }
  json::value resistance = nullptr;
  if (rule->hasResistance()) {
    resistance = rule->getResistance();
  }
  return json::array{layerName(rule->getLayer()),
                     std::string(rule->getDirection().getString()),
                     std::move(width),
                     std::move(enclosure),
                     std::move(overhang),
                     std::move(metal_overhang),
                     std::move(rect),
                     std::move(spacing),
                     std::move(resistance)};
}

inline json::array viaGenerateRules(odb::dbTech* tech)
{
  json::array values;
  for (auto* rule : tech->getViaGenerateRules()) {
    json::array layer_rules;
    for (uint32_t index = 0; index < rule->getViaLayerRuleCount(); ++index) {
      layer_rules.emplace_back(viaLayerRule(rule->getViaLayerRule(index)));
    }
    values.emplace_back(json::array{rule->getName(),
                                    rule->isDefault(),
                                    drt_trace::canonical(std::move(layer_rules))});
  }
  return drt_trace::canonical(std::move(values));
}

inline json::value ndr(odb::dbTechNonDefaultRule* rule,
                       std::string_view origin)
{
  std::vector<odb::dbTechLayerRule*> layer_rules;
  rule->getLayerRules(layer_rules);
  json::array projected_layers;
  for (auto* layer_rule : layer_rules) {
    auto* layer = layer_rule->getLayer();
    if (layer->isBackside()) {
      continue;
    }
    projected_layers.emplace_back(json::array{layer->getName(),
                                              layer_rule->getWidth(),
                                              layer_rule->getSpacing(),
                                              static_cast<int64_t>(
                                                  layer_rule->getWireExtension())});
  }

  std::vector<odb::dbTechVia*> vias;
  rule->getUseVias(vias);
  json::array projected_vias;
  for (auto* value : vias) {
    if (value->getBottomLayer()->isBackside()
        || value->getTopLayer()->isBackside()) {
      continue;
    }
    projected_vias.emplace_back(value->getName());
  }

  std::vector<odb::dbTechViaGenerateRule*> via_rules;
  rule->getUseViaRules(via_rules);
  json::array projected_via_rules;
  for (auto* value : via_rules) {
    bool has_backside = false;
    for (uint32_t index = 0; index < value->getViaLayerRuleCount(); ++index) {
      if (value->getViaLayerRule(index)->getLayer()->isBackside()) {
        has_backside = true;
        break;
      }
    }
    if (!has_backside) {
      projected_via_rules.emplace_back(value->getName());
    }
  }

  return json::array{std::string(origin),
                     rule->getName(),
                     rule->getHardSpacing(),
                     drt_trace::canonical(std::move(projected_layers)),
                     drt_trace::canonical(std::move(projected_vias)),
                     drt_trace::canonical(std::move(projected_via_rules))};
}

inline json::array ndrs(odb::dbDatabase* database)
{
  json::array values;
  for (auto* rule : database->getTech()->getNonDefaultRules()) {
    values.emplace_back(ndr(rule, "tech"));
  }
  if (auto* chip = database->getChip(); chip != nullptr) {
    if (auto* block = chip->getBlock(); block != nullptr) {
      for (auto* rule : block->getNonDefaultRules()) {
        values.emplace_back(ndr(rule, "block"));
      }
    }
  }
  return drt_trace::canonical(std::move(values));
}

inline json::array libraryMasters(odb::dbDatabase* database)
{
  json::array values;
  for (auto* library : database->getLibs()) {
    for (auto* master : library->getMasters()) {
      const odb::Point origin = master->getOrigin();
      values.emplace_back(json::array{master->getName(),
                                      std::string(master->getType().getString()),
                                      origin.x(),
                                      origin.y(),
                                      static_cast<int64_t>(master->getWidth()),
                                      static_cast<int64_t>(master->getHeight())});
    }
  }
  return drt_trace::canonical(std::move(values));
}

inline json::array libraryTerms(odb::dbDatabase* database)
{
  json::array values;
  for (auto* library : database->getLibs()) {
    for (auto* master : library->getMasters()) {
      for (auto* term : master->getMTerms()) {
        int mpin_count = 0;
        for (auto* ignored : term->getMPins()) {
          static_cast<void>(ignored);
          ++mpin_count;
        }
        values.emplace_back(json::array{master->getName(),
                                        term->getName(),
                                        std::string(term->getSigType().getString()),
                                        mpin_count});
      }
    }
  }
  return drt_trace::canonical(std::move(values));
}

inline json::array libraryMPinGeometry(odb::dbDatabase* database)
{
  json::array values;
  for (auto* library : database->getLibs()) {
    for (auto* master : library->getMasters()) {
      for (auto* term : master->getMTerms()) {
        int ordinal = 0;
        for (auto* mpin : term->getMPins()) {
          json::array geometry;
          for (auto* box : mpin->getGeometry()) {
            if (auto* layer = box->getTechLayer(); layer != nullptr) {
              geometry.emplace_back(json::array{"rect",
                                                layer->getName(),
                                                box->xMin(),
                                                box->yMin(),
                                                box->xMax(),
                                                box->yMax()});
            } else if (auto* via = box->getTechVia(); via != nullptr) {
              const odb::Point point = box->getViaXY();
              geometry.emplace_back(
                  json::array{"tech_via", via->getName(), point.x(), point.y()});
            } else {
              // Parser::setMasters assumes this case cannot occur. Preserve a
              // typed value if a future ODB producer violates that assumption.
              geometry.emplace_back(json::array{"unsupported", nullptr});
            }
          }
          values.emplace_back(json::array{master->getName(),
                                          term->getName(),
                                          ordinal++,
                                          drt_trace::canonical(std::move(geometry))});
        }
      }
    }
  }
  return drt_trace::canonical(std::move(values));
}

inline json::array libraryObstructions(odb::dbDatabase* database)
{
  json::array values;
  for (auto* library : database->getLibs()) {
    for (auto* master : library->getMasters()) {
      for (auto* obstruction : master->getObstructions()) {
        values.emplace_back(json::array{master->getName(),
                                        layerName(obstruction->getTechLayer()),
                                        obstruction->xMin(),
                                        obstruction->yMin(),
                                        obstruction->xMax(),
                                        obstruction->yMax(),
                                        obstruction->getDesignRuleWidth()});
      }
    }
  }
  return drt_trace::canonical(std::move(values));
}

}  // namespace eccdb::tritonroute::trace_projection
