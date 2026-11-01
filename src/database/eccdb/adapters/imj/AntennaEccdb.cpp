// SPDX-License-Identifier: MulanPSL-2.0
// Copyright (c) 2026 Beijing Institute of Open Source Chip

#include "AntennaEccdb.h"

#include <algorithm>
#include <any>
#include <cstdint>
#include <fstream>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include "MJInterface.hpp"
#include "antenna/AntennaKernel.hpp"
#include "ACModel.hpp"
#include "ACViolationType.hpp"
#include "context/DatabaseContext.h"
#include "geometry/PhysicalDesignService.h"
#include "geometry/PolygonRectDecomposer.h"
#include "service/antenna/AntennaService.h"
#include "design/routing/component/RoutingComponents.h"
#include "design/routing/storage/DesignRoutingStorage.h"
#include "library/master_term/model/MasterTermComponents.h"
#include "tech/routing_layer/model/RoutingLayerComponents.h"

namespace imj {
namespace {

[[nodiscard]] bool isSignalNet(eccdb::DesignSignalUse use) noexcept
{
  return use != eccdb::DesignSignalUse::kPower && use != eccdb::DesignSignalUse::kGround;
}

[[nodiscard]] bool isOutputLike(eccdb::LibraryMasterTermDirection direction) noexcept
{
  return direction == eccdb::LibraryMasterTermDirection::kOutput
         || direction == eccdb::LibraryMasterTermDirection::kOutputTriState
         || direction == eccdb::LibraryMasterTermDirection::kInOut;
}

[[nodiscard]] bool isOutputLike(eccdb::DesignIoPinDirection direction) noexcept
{
  return direction == eccdb::DesignIoPinDirection::kOutput || direction == eccdb::DesignIoPinDirection::kInOut;
}

[[nodiscard]] std::string violationTypeName(ACViolationType type)
{
  switch (type) {
    case ACViolationType::kAntennaPar: return "PAR";
    case ACViolationType::kAntennaDiffPar: return "DiffPAR";
    case ACViolationType::kAntennaCar: return "CAR";
    case ACViolationType::kAntennaDiffCar: return "DiffCAR";
    case ACViolationType::kAntennaPsr: return "PSR";
    case ACViolationType::kAntennaDiffPsr: return "DiffPSR";
    case ACViolationType::kAntennaCsr: return "CSR";
    case ACViolationType::kAntennaDiffCsr: return "DiffCSR";
    case ACViolationType::kAntennaCutPar: return "CutPAR";
    case ACViolationType::kAntennaCutCar: return "CutCAR";
    case ACViolationType::kAntennaDiffCutPar: return "DiffCutPAR";
    case ACViolationType::kAntennaDiffCutCar: return "DiffCutCAR";
    case ACViolationType::kNone: return "None";
  }
  return "Unknown";
}

struct PinAntennaInfo
{
  double gate_area = 0.0;
  double diff_area = 0.0;
  bool provides_diff = false;
  bool has_gate_annotation = false;
  bool has_any_annotation = false;
  struct PartialArea {
    eccdb::AntennaMetric metric = eccdb::AntennaMetric::kUnknown;
    std::string layer;
    double value = 0.0;
  };
  std::vector<PartialArea> partial_areas;
};

void appendPinValues(PinAntennaInfo& output,
                     const std::vector<eccdb::AntennaValue>& values,
                     const std::vector<eccdb::AntennaLayerValue>& layer_values)
{
  const auto consume = [&output](eccdb::AntennaMetric metric, std::string_view layer, double value) {
    switch (metric) {
      case eccdb::AntennaMetric::kGateArea:
        output.gate_area += value;
        output.has_gate_annotation = true;
        output.has_any_annotation = true;
        break;
      case eccdb::AntennaMetric::kDiffArea:
        output.diff_area += value;
        output.provides_diff = true;
        output.has_any_annotation = true;
        break;
      case eccdb::AntennaMetric::kPartialMetalArea:
      case eccdb::AntennaMetric::kPartialMetalSideArea:
      case eccdb::AntennaMetric::kPartialCutArea:
        output.partial_areas.push_back({.metric = metric, .layer = std::string{layer}, .value = value});
        output.has_any_annotation = true;
        break;
      default: break;
    }
  };
  for (const auto& value : values) {
    consume(value.metric, {}, value.value);
  }
  for (const auto& value : layer_values) {
    consume(value.metric, value.layer, value.value);
  }
}

template <typename PinAntenna>
[[nodiscard]] PinAntennaInfo readPinAntenna(const PinAntenna* antenna,
                                             const eccdb::AntennaModel* selected_model,
                                             bool output_like,
                                             NativeAntennaSummary& summary,
                                             std::string_view owner)
{
  PinAntennaInfo result;
  if (antenna == nullptr) {
    result.provides_diff = output_like;
    return result;
  }
  appendPinValues(result, antenna->values, antenna->layer_values);

  if (selected_model != nullptr) {
    appendPinValues(result, selected_model->values, selected_model->layer_values);
    if (antenna->models.size() > 1u) {
      summary.unsupported_oxide_model_count += antenna->models.size() - 1u;
      summary.coverage_diagnostics.push_back("multiple ANTENNAMODEL entries on " + std::string{owner}
                                             + "; native checker selected the OXIDE1 model");
    }
  } else if (!antenna->models.empty()) {
    summary.unsupported_oxide_model_count += antenna->models.size();
    summary.coverage_diagnostics.push_back("no OXIDE1 ANTENNAMODEL entry on " + std::string{owner});
  }
  result.provides_diff = result.provides_diff || output_like;
  return result;
}

template <typename Value>
void setRuleValue(ACAntennaRule& rule, const Value& value)
{
  switch (value.metric) {
    case eccdb::AntennaMetric::kAreaRatio: rule.area_ratio = value.value; break;
    case eccdb::AntennaMetric::kCumAreaRatio: rule.cum_area_ratio = value.value; break;
    case eccdb::AntennaMetric::kAreaFactor:
    case eccdb::AntennaMetric::kCutAreaFactor:
      rule.area_factor = value.value;
      rule.area_factor_diffuse_only = (value.flags & eccdb::AntennaValueFlag::kDiffuseOnly) != 0u;
      break;
    case eccdb::AntennaMetric::kSideAreaRatio: rule.side_area_ratio = value.value; break;
    case eccdb::AntennaMetric::kCumSideAreaRatio: rule.cum_side_area_ratio = value.value; break;
    case eccdb::AntennaMetric::kSideAreaFactor:
      rule.side_area_factor = value.value;
      rule.side_area_factor_diffuse_only = (value.flags & eccdb::AntennaValueFlag::kDiffuseOnly) != 0u;
      break;
    case eccdb::AntennaMetric::kGatePlusDiff: rule.gate_plus_diff = value.value; break;
    case eccdb::AntennaMetric::kAreaMinusDiff: rule.area_minus_diff = value.value; break;
    case eccdb::AntennaMetric::kDiffAreaRatio: rule.diff_area_ratio = value.value; break;
    case eccdb::AntennaMetric::kCumDiffAreaRatio: rule.cum_diff_area_ratio = value.value; break;
    case eccdb::AntennaMetric::kDiffSideAreaRatio: rule.diff_side_area_ratio = value.value; break;
    case eccdb::AntennaMetric::kCumDiffSideAreaRatio: rule.cum_diff_side_area_ratio = value.value; break;
    case eccdb::AntennaMetric::kCumRoutingPlusCut: rule.cum_routing_plus_cut = value.value != 0.0; break;
    default: break;
  }
}

void setRulePwl(ACAntennaRule& rule, const eccdb::AntennaPwl& pwl)
{
  std::vector<std::pair<double, double>> points;
  points.reserve(pwl.points.size());
  for (const auto& point : pwl.points) {
    points.emplace_back(point.x, point.y);
  }
  switch (pwl.metric) {
    case eccdb::AntennaMetric::kDiffAreaRatioPwl: rule.diff_area_ratio_pwl = std::move(points); break;
    case eccdb::AntennaMetric::kCumDiffAreaRatioPwl: rule.cum_diff_area_ratio_pwl = std::move(points); break;
    case eccdb::AntennaMetric::kDiffSideAreaRatioPwl: rule.diff_side_area_ratio_pwl = std::move(points); break;
    case eccdb::AntennaMetric::kCumDiffSideAreaRatioPwl: rule.cum_diff_side_area_ratio_pwl = std::move(points); break;
    case eccdb::AntennaMetric::kAreaDiffReducePwl: rule.area_diff_reduce_pwl = std::move(points); break;
    default: break;
  }
}

[[nodiscard]] bool modelHasRules(const eccdb::AntennaModel& model)
{
  return !model.values.empty() || !model.layer_values.empty() || !model.pwls.empty();
}

void appendLayerRule(ACModel& model,
                     const eccdb::PhysicalDesignService& physical,
                     eccdb::AntennaService& antenna,
                     eccdb::TechLayerId layer,
                     bool routing,
                     double thickness_um,
                     NativeAntennaSummary& summary)
{
  const auto* layer_antenna = antenna.layerAntenna(layer);
  const auto* selected_model = antenna.layerAntennaModel(layer, "OXIDE1");
  if (selected_model == nullptr || !modelHasRules(*selected_model)) {
    return;
  }
  ACAntennaRule rule;
  rule.layer_name = physical.layerInfo(layer).name;
  rule.layer_order = static_cast<int>(physical.layerPosition(layer).value_or(0u));
  rule.is_routing = routing;
  rule.thickness_um = thickness_um;
  for (const auto& value : selected_model->values) {
    setRuleValue(rule, value);
  }
  for (const auto& value : selected_model->layer_values) {
    setRuleValue(rule, value);
  }
  for (const auto& pwl : selected_model->pwls) {
    setRulePwl(rule, pwl);
  }
  model.get_rule_list().push_back(std::move(rule));
  if (layer_antenna != nullptr && layer_antenna->models.size() > 1u) {
    summary.unsupported_oxide_model_count += layer_antenna->models.size() - 1u;
    summary.coverage_diagnostics.push_back("multiple antenna oxide models on technology layer " + physical.layerInfo(layer).name
                                           + "; selected the OXIDE1 model");
  }
}

struct NetGraphBuilder
{
  const eccdb::PhysicalDesignService& physical;
  NativeAntennaSummary& summary;
  const std::set<int>& conductive_masterslice_layers;
  AntennaGraphInput graph;
  bool saw_pin = false;
  bool saw_gate_annotation = false;
  bool saw_terminal_antenna_info = false;
  bool saw_route_geometry = false;
  std::set<int> observed_layers;

  [[nodiscard]] int layerOrder(eccdb::TechLayerId layer)
  {
    const auto position = physical.layerPosition(layer);
    return position.has_value() ? static_cast<int>(*position) : -1;
  }

  [[nodiscard]] int addGate(const PinAntennaInfo& antenna)
  {
    if (!antenna.has_any_annotation && !antenna.provides_diff) {
      return -1;
    }
    const int id = static_cast<int>(graph.nodes.size());
    graph.nodes.push_back({.id = id,
                           .time = 0,
                           .is_conductor = false,
                           .gate_area = antenna.gate_area,
                           .diff_area = antenna.diff_area,
                           .provides_diff = antenna.provides_diff});
    saw_gate_annotation = saw_gate_annotation || antenna.has_gate_annotation;
    saw_terminal_antenna_info = saw_terminal_antenna_info || antenna.gate_area > 0.0 || antenna.diff_area > 0.0;
    return id;
  }

  [[nodiscard]] int addConductor(eccdb::TechLayerId layer,
                                 std::vector<eccdb::Rect> rectangles,
                                 double declared_area = 0.0,
                                 bool side = false)
  {
    const int order = layerOrder(layer);
    if (order < 0) {
      ++summary.conductors_out_of_range;
      return -1;
    }
    const auto kind = physical.layerKind(layer);
    const bool masterslice = conductive_masterslice_layers.contains(order);
    if (kind != eccdb::GeometryLayerKind::kRouting && kind != eccdb::GeometryLayerKind::kCut && !masterslice) {
      ++summary.skipped_segments;
      ++summary.unsupported_route_shape_count;
      return -1;
    }
    if (rectangles.empty() && declared_area <= 0.0) {
      return -1;
    }
    const int id = static_cast<int>(graph.nodes.size());
    AntennaGraphNode node;
    node.id = id;
    node.time = order;
    node.is_conductor = true;
    node.is_routing = kind == eccdb::GeometryLayerKind::kRouting || masterslice;
    node.is_cut = kind == eccdb::GeometryLayerKind::kCut;
    node.is_side = side;
    node.declared_area = declared_area;
    node.shapes.reserve(rectangles.size());
    for (const auto& rectangle : rectangles) {
      node.shapes.push_back({.layer_order = order,
                             .rectangle = {.ll_x = rectangle.ll_x,
                                           .ll_y = rectangle.ll_y,
                                           .ur_x = rectangle.ur_x,
                                           .ur_y = rectangle.ur_y}});
    }
    graph.nodes.push_back(std::move(node));
    observed_layers.insert(order);
    return id;
  }

  void connect(int first, int second, int time)
  {
    if (first >= 0 && second >= 0 && first != second && time >= 0) {
      graph.edges.push_back({.first = first, .second = second, .time = time});
    }
  }

  void appendVia(eccdb::TechViaMasterId tech_via,
                 eccdb::DesignViaId design_via,
                 eccdb::Point origin,
                 eccdb::DesignOrientation orientation,
                 int attachment)
  {
    struct LayerRects {
      eccdb::TechLayerId layer;
      std::vector<eccdb::Rect> rectangles;
    };
    std::vector<LayerRects> by_layer;
    const auto collect = [&](const eccdb::PhysicalLayerRect& rectangle) {
      auto found = std::find_if(by_layer.begin(), by_layer.end(), [&](const auto& item) {
        return item.layer.entity() == rectangle.layer.entity();
      });
      if (found == by_layer.end()) {
        found = by_layer.emplace(by_layer.end(), LayerRects{.layer = rectangle.layer});
      }
      found->rectangles.push_back(rectangle.rectangle);
    };
    const eccdb::PhysicalGeometryVisitor visitor{.rectangle = collect};
    if (tech_via) {
      physical.forEachTechViaGeometry(tech_via, origin, orientation, visitor);
    } else if (design_via) {
      physical.forEachDesignViaGeometry(design_via, origin, orientation, visitor);
    } else {
      ++summary.skipped_segments;
      ++summary.unsupported_route_shape_count;
      return;
    }
    std::vector<std::pair<int, int>> nodes;
    nodes.reserve(by_layer.size());
    for (auto& item : by_layer) {
      const int node = addConductor(item.layer, std::move(item.rectangles));
      if (node >= 0) {
        nodes.emplace_back(layerOrder(item.layer), node);
        connect(attachment, node, layerOrder(item.layer));
      }
    }
    std::sort(nodes.begin(), nodes.end());
    for (size_t index = 1; index < nodes.size(); ++index) {
      connect(nodes[index - 1].second, nodes[index].second, nodes[index].first);
    }
    if (!nodes.empty()) {
      ++summary.via_geometry_count;
    }
  }

  template <typename GeometryFunction>
  [[nodiscard]] int appendPinGeometry(GeometryFunction&& visit_geometry, int attachment)
  {
    int first_conductor = -1;
    const auto append_rectangle = [&](const eccdb::PhysicalLayerRect& rectangle) {
      const int node = addConductor(rectangle.layer, {rectangle.rectangle});
      if (first_conductor < 0) {
        first_conductor = node;
      }
      connect(attachment, node, layerOrder(rectangle.layer));
    };
    const auto append_polygon = [&](const eccdb::PhysicalLayerPolygon& polygon) {
      const auto rectangles = eccdb::decomposePolygonToRectangles(polygon.points);
      if (rectangles.empty() && !polygon.points.empty()) {
        ++summary.unsupported_polygon_count;
      }
      const int node = addConductor(polygon.layer, rectangles);
      if (first_conductor < 0) {
        first_conductor = node;
      }
      connect(attachment, node, layerOrder(polygon.layer));
    };
    const auto append_via = [&](const eccdb::PhysicalViaPlacement& via) {
      appendVia(via.tech_via, via.design_via, via.origin, via.orientation, attachment);
    };
    visit_geometry(eccdb::PhysicalGeometryVisitor{.rectangle = append_rectangle, .polygon = append_polygon, .via = append_via});
    return first_conductor;
  }

  void appendPartialAreas(const PinAntennaInfo& antenna, int anchor)
  {
    for (const auto& partial : antenna.partial_areas) {
      if (anchor < 0 || partial.layer.empty()) {
        ++summary.partial_areas_dropped;
        continue;
      }
      const auto layer = physical.findLayer(partial.layer);
      if (!layer) {
        ++summary.partial_areas_dropped;
        continue;
      }
      const bool side = partial.metric == eccdb::AntennaMetric::kPartialMetalSideArea;
      const int node = addConductor(layer, {}, partial.value, side);
      connect(anchor, node, layerOrder(layer));
    }
  }
};

void appendWireGeometry(NetGraphBuilder& builder,
                        const eccdb::PhysicalDesignService& physical,
                        eccdb::DesignRouteShapeId id,
                        const eccdb::DesignRouteShape& shape)
{
  const auto node_count_before = builder.graph.nodes.size();
  std::visit(
      [&](const auto& geometry) {
        using Geometry = std::remove_cvref_t<decltype(geometry)>;
        if constexpr (std::is_same_v<Geometry, eccdb::DesignRouteSegment>) {
          if (geometry.begin.position.x != geometry.end.position.x && geometry.begin.position.y != geometry.end.position.y) {
            ++builder.summary.skipped_segments;
            ++builder.summary.unsupported_route_shape_count;
            return;
          }
          static_cast<void>(builder.addConductor(eccdb::TechLayerId{shape.layer.entity()}, {physical.routeShapeBounds(id)}));
        } else if constexpr (std::is_same_v<Geometry, eccdb::DesignRoutePatch>) {
          static_cast<void>(builder.addConductor(eccdb::TechLayerId{shape.layer.entity()}, {geometry.rectangle}));
        } else if constexpr (std::is_same_v<Geometry, eccdb::DesignRouteVia>) {
          const auto& via = geometry.via;
          const uint32_t rows = std::max(1u, via.rows);
          const uint32_t columns = std::max(1u, via.columns);
          for (uint32_t row = 0; row < rows; ++row) {
            for (uint32_t column = 0; column < columns; ++column) {
              builder.appendVia(via.tech_via, via.design_via,
                                {.x = geometry.origin.position.x + static_cast<int32_t>(column) * via.step_x,
                                 .y = geometry.origin.position.y + static_cast<int32_t>(row) * via.step_y},
                                via.orientation, -1);
            }
          }
        } else if constexpr (std::is_same_v<Geometry, eccdb::DesignRoutePoint>) {
          // A lone DEF point has no metal and cannot contribute antenna area.
        }
      },
      shape.geometry);
  // Pin and wire geometry share addConductor(). A physical PORT must remain
  // available to the connectivity graph, but only a wire shape that actually
  // materialized a conductor makes this net routed for coverage accounting.
  builder.saw_route_geometry = builder.saw_route_geometry || builder.graph.nodes.size() > node_count_before;
}

void appendViolationStrings(NativeAntennaSummary& summary, std::vector<ACViolation> violations)
{
  std::sort(violations.begin(), violations.end(), [](const auto& lhs, const auto& rhs) {
    return std::tie(lhs.net_name, lhs.layer_name, lhs.type, lhs.ratio, lhs.threshold, lhs.lx, lhs.ly, lhs.hx, lhs.hy)
           < std::tie(rhs.net_name, rhs.layer_name, rhs.type, rhs.ratio, rhs.threshold, rhs.lx, rhs.ly, rhs.hx, rhs.hy);
  });
  summary.violation_count = violations.size();
  for (size_t index = 0; index < violations.size(); ++index) {
    const auto& violation = violations[index];
    std::ostringstream text;
    text << violation.net_name << ':' << violation.layer_name << ' ' << violationTypeName(violation.type)
         << " ratio=" << violation.ratio << " threshold=" << violation.threshold << " bbox=(" << violation.lx << ','
         << violation.ly << "," << violation.hx << ',' << violation.hy << ')';
    summary.violations.push_back(text.str());
  }
}

void maybeWriteReport(const NativeAntennaSummary& summary, const std::map<std::string, std::any>& config)
{
  const auto found = config.find("-antenna_report_file");
  if (found == config.end()) {
    return;
  }
  const auto* path = std::any_cast<std::string>(&found->second);
  if (path == nullptr || path->empty()) {
    throw std::invalid_argument("iMJ antenna option '-antenna_report_file' must be a non-empty string");
  }
  std::ofstream report(*path);
  if (!report) {
    throw std::runtime_error("cannot open iMJ antenna report: " + *path);
  }
  report << "checked=" << summary.checked << " coverage_complete=" << summary.coverage_complete << '\n';
  report << "net_count=" << summary.net_count << " connected_net_count=" << summary.connected_net_count
         << " wire_count=" << summary.wire_count << " violation_count=" << summary.violation_count << '\n';
  for (const auto& diagnostic : summary.coverage_diagnostics) {
    report << "coverage: " << diagnostic << '\n';
  }
  for (const auto& violation : summary.violations) {
    report << violation << '\n';
  }
}

}  // namespace

NativeAntennaSummary checkAntennaEccdb(eccdb::DatabaseContext& context, std::map<std::string, std::any>& config)
{
  if (context.technology() == nullptr || context.library() == nullptr) {
    throw std::logic_error("iMJ EccDB antenna checking requires Technology and Library stores");
  }

  eccdb::PhysicalDesignService physical(context);
  eccdb::AntennaService antenna(context);
  ACModel model;
  model.get_micron_dbu() = physical.databaseUnitsPerMicron();
  NativeAntennaSummary summary;

  physical.forEachRoutingLayer([&](eccdb::TechRoutingLayerId layer,
                                   uint32_t,
                                   uint32_t,
                                   const eccdb::TechLayerInfo&,
                                   const eccdb::TechRoutingLayer& routing) {
    appendLayerRule(model, physical, antenna, eccdb::TechLayerId{layer.entity()}, true,
                    static_cast<double>(routing.thickness) / static_cast<double>(std::max(1, model.get_micron_dbu())), summary);
  });
  physical.forEachCutLayer([&](eccdb::TechCutLayerId layer, uint32_t, const eccdb::TechLayerInfo&, const eccdb::TechCutLayer&) {
    appendLayerRule(model, physical, antenna, eccdb::TechLayerId{layer.entity()}, false, 0.0, summary);
  });
  std::set<int> conductive_masterslice_layers;
  physical.forEachMastersliceLayer([&](eccdb::TechMastersliceLayerId layer,
                                       uint32_t position,
                                       const eccdb::TechLayerInfo&,
                                       const eccdb::TechMastersliceLayer&) {
    const auto previous_count = model.get_rule_list().size();
    appendLayerRule(model, physical, antenna, eccdb::TechLayerId{layer.entity()}, true, 0.0, summary);
    if (model.get_rule_list().size() != previous_count) {
      conductive_masterslice_layers.insert(static_cast<int>(position));
    }
  });
  AntennaKernel::initializeRuleIndex(model);
  summary.rule_layer_count = model.get_rule_list().size();
  if (model.get_rule_list().empty()) {
    summary.coverage_diagnostics.push_back("technology LEF contains no supported antenna layer rule; no antenna ratios were evaluated");
    maybeWriteReport(summary, config);
    if (const auto report = config.find("-antenna_report_file"); report != config.end()) {
      summary.report_path = std::any_cast<std::string>(report->second);
    }
    return summary;
  }

  std::vector<ACViolation> violations;
  std::set<int> covered_layers;
  std::set<int> missing_rule_layers;
  uint64_t signal_nets = 0;
  physical.forEachRegularNet([&](eccdb::DesignNetId net_id, const eccdb::DesignNet& net) {
    if (!isSignalNet(net.use)) {
      return;
    }
    ++signal_nets;
    ++summary.net_count;
    NetGraphBuilder builder{.physical = physical,
                            .summary = summary,
                            .conductive_masterslice_layers = conductive_masterslice_layers,
                            .graph = {.net_name = net.name}};

    physical.forEachNetPin(net_id, {
                                        .instance_pin = [&](eccdb::DesignInstanceTermId term_id,
                                                            const eccdb::DesignInstanceTerm& term,
                                                            const eccdb::DesignInstance&,
                                                            const eccdb::LibraryMasterTerm& master_term) {
                                          builder.saw_pin = true;
                                          const auto* pin_antenna = antenna.masterTermAntenna(term.master_term);
                                          const auto* model = antenna.masterTermAntennaModel(term.master_term, "OXIDE1");
                                          const auto info = readPinAntenna(pin_antenna, model,
                                                                           isOutputLike(master_term.direction), summary,
                                                                           master_term.name);
                                          if (!info.has_gate_annotation && !info.has_any_annotation) {
                                            ++summary.pins_missing_antenna_info;
                                          }
                                          if (info.gate_area > 0.0) {
                                            ++summary.pins_with_gate_area;
                                          }
                                          const int gate = builder.addGate(info);
                                          const int first = builder.appendPinGeometry(
                                              [&](const eccdb::PhysicalGeometryVisitor& visitor) {
                                                physical.forEachPlacedInstancePinGeometry(term_id, visitor);
                                              },
                                              gate);
                                          builder.appendPartialAreas(info, gate >= 0 ? gate : first);
                                        },
                                        .io_pin = [&](eccdb::DesignBlockTermId term_id, const eccdb::DesignBlockTerm& term) {
                                          builder.saw_pin = true;
                                          const auto* pin_antenna = antenna.blockTermAntenna(term_id);
                                          const auto* model = antenna.blockTermAntennaModel(term_id, "OXIDE1");
                                          const auto info = readPinAntenna(pin_antenna, model, isOutputLike(term.direction),
                                                                           summary, term.name);
                                          const int gate = builder.addGate(info);
                                          const int first = builder.appendPinGeometry(
                                              [&](const eccdb::PhysicalGeometryVisitor& visitor) { physical.forEachIoPinGeometry(term_id, visitor); },
                                              gate);
                                          builder.appendPartialAreas(info, gate >= 0 ? gate : first);
                                        }});

    physical.forEachNetWire(net_id, [&](eccdb::DesignWireId, const eccdb::DesignWire&) { ++summary.wire_count; });
    physical.forEachNetWireShape(net_id, [&](eccdb::DesignWireId,
                                             const eccdb::DesignWire&,
                                             eccdb::DesignRouteShapeId shape_id,
                                             const eccdb::DesignRouteShape& shape,
                                             const eccdb::DesignRouteShapeAttributes*) {
      appendWireGeometry(builder, physical, shape_id, shape);
    });

    if (builder.saw_pin) {
      ++summary.connected_net_count;
    }
    if (builder.saw_route_geometry && !builder.saw_gate_annotation) {
      ++summary.nets_without_gate;
    }
    if (builder.saw_route_geometry && !builder.saw_terminal_antenna_info) {
      ++summary.nets_without_terminal_antenna_info;
    }
    for (const int layer : builder.observed_layers) {
      bool has_rule = false;
      if (layer >= 0 && static_cast<size_t>(layer) < model.get_rule_map().size()) {
        has_rule = !model.get_rule_map()[static_cast<size_t>(layer)].empty();
      }
      (has_rule ? covered_layers : missing_rule_layers).insert(layer);
    }
    AntennaKernel::checkNet(model, builder.graph, violations);
  });
  model.get_signal_net_cnt() = static_cast<int64_t>(signal_nets);

  summary.covered_rule_layer_count = covered_layers.size();
  summary.skipped_rule_layer_count = missing_rule_layers.size();
  for (const int layer : missing_rule_layers) {
    summary.coverage_diagnostics.push_back("routed geometry on technology layer position " + std::to_string(layer)
                                           + " has no antenna rule");
  }
  const auto kernel_summary = AntennaKernel::summary(model);
  // Pin metadata and route conversion are counted during EccDB graph
  // construction. The graph kernel contributes the per-component result.
  summary.components_without_gate = kernel_summary.components_without_gate;
  if (summary.skipped_segments != 0u || summary.conductors_out_of_range != 0u || summary.partial_areas_dropped != 0u) {
    summary.coverage_diagnostics.push_back("native antenna graph dropped unsupported geometry or layer-qualified antenna metadata");
  }
  if (summary.components_without_gate != 0u) {
    summary.coverage_diagnostics.push_back(std::to_string(summary.components_without_gate)
                                           + " conductor components had no connected gate at their layer-event evaluation time"
                                             " (diagnostic only; final net coverage uses nets_without_terminal_antenna_info)");
  }
  if (summary.nets_without_gate != 0u) {
    summary.coverage_diagnostics.push_back(std::to_string(summary.nets_without_gate)
                                           + " routed nets have no explicit ANTENNAGATEAREA"
                                             " (diagnostic; diffusion-only terminal metadata remains valid)");
  }
  if (summary.pins_missing_antenna_info != 0u) {
    summary.coverage_diagnostics.push_back(std::to_string(summary.pins_missing_antenna_info)
                                           + " instance pins have no antenna gate/diffusion annotation");
  }
  appendViolationStrings(summary, std::move(violations));
  summary.checked = true;
  summary.coverage_complete = summary.net_count != 0u && summary.skipped_rule_layer_count == 0u
                              && summary.unsupported_polygon_count == 0u && summary.unsupported_route_shape_count == 0u
                              && summary.pins_missing_antenna_info == 0u && summary.nets_without_terminal_antenna_info == 0u
                              && summary.skipped_segments == 0u && summary.conductors_out_of_range == 0u
                              && summary.partial_areas_dropped == 0u && summary.unsupported_oxide_model_count == 0u;
  if (summary.net_count == 0u) {
    summary.coverage_diagnostics.push_back("design contains no signal net; antenna rules were loaded but no net was evaluated");
  }
  if (!summary.coverage_complete && summary.coverage_diagnostics.empty()) {
    summary.coverage_diagnostics.push_back("antenna graph did not establish complete gate/routing coverage");
  }
  maybeWriteReport(summary, config);
  if (const auto report = config.find("-antenna_report_file"); report != config.end()) {
    summary.report_path = std::any_cast<std::string>(report->second);
  }
  return summary;
}

}  // namespace imj
