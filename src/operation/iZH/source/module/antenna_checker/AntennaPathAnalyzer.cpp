// ***************************************************************************************
// Copyright (c) 2023-2025 Peng Cheng Laboratory
// Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences
// Copyright (c) 2023-2025 Beijing Institute of Open Source Chip
// ***************************************************************************************

#include "AntennaPathAnalyzer.hpp"

#include "ACEdge.hpp"
#include "ACGraphNode.hpp"
#include "ACPinShape.hpp"
#include "ACUFNode.hpp"
#include "ACUnionFind.hpp"
#include "AntennaGeometry.hpp"
#include "AntennaRuleEvaluator.hpp"
#include "Utility.hpp"

#include "IdbDesign.h"
#include "IdbInstance.h"
#include "IdbLayer.h"
#include "IdbLayout.h"
#include "IdbNet.h"
#include "IdbPins.h"
#include "IdbRegularWire.h"
#include "IdbTerm.h"
#include "IdbVias.h"

namespace izh {

void AntennaPathAnalyzer::readPinAntennaInfo(ACModel& ac_model, idb::IdbPin* pin, const bool instance_pin, double& gate_area,
                                             double& diff_area, bool& provides_diff)
{
  std::atomic<int64_t>& pins_missing_antenna_info = ac_model.get_pins_missing_antenna_info();
  std::atomic<int64_t>& pins_with_gate_area = ac_model.get_pins_with_gate_area();
  gate_area = 0.0;
  diff_area = 0.0;
  provides_diff = false;

  if (pin == nullptr) {
    return;
  }

  idb::IdbTerm* term = pin->get_term();
  if (term == nullptr) {
    if (instance_pin) {
      pins_missing_antenna_info.fetch_add(1, std::memory_order_relaxed);
    }
    return;
  }

  if (term->has_antenna_gate_area()) {
    gate_area = term->get_antenna_gate_area();
    if (instance_pin) {
      pins_with_gate_area.fetch_add(1, std::memory_order_relaxed);
    }
  }

  if (term->has_antenna_diff_area()) {
    diff_area = term->get_antenna_diff_area();
    provides_diff = true;
  }

  if (!Utility::equalDoubleByError(diff_area, 0.0, ZH_ERROR)) {
    provides_diff = true;
  }

  idb::IdbConnectDirection dir = term->get_direction();
  if (dir == idb::IdbConnectDirection::kOutput || dir == idb::IdbConnectDirection::kOutputTriState
      || dir == idb::IdbConnectDirection::kInOut) {
    provides_diff = true;
  }

  if (instance_pin && !term->has_antenna_gate_area() && !term->has_antenna_diff_area()) {
    pins_missing_antenna_info.fetch_add(1, std::memory_order_relaxed);
  }
}

void AntennaPathAnalyzer::checkNet(ACModel& ac_model, idb::IdbDesign* design, idb::IdbNet* net, std::vector<ACViolation>& out_violations)
{
  const int micron_dbu = ac_model.get_micron_dbu();
  const std::vector<double>& thickness_by_order = ac_model.get_thickness_by_order();
  const std::vector<bool>& conductive_order = ac_model.get_conductive_order();
  const int max_layer_order = ac_model.get_max_layer_order();
  std::atomic<int64_t>& comps_without_gate = ac_model.get_comps_without_gate();
  std::atomic<int64_t>& skipped_segments = ac_model.get_skipped_segments();
  std::atomic<int64_t>& conductors_out_of_range = ac_model.get_conductors_out_of_range();
  std::atomic<int64_t>& partial_areas_dropped = ac_model.get_partial_areas_dropped();
  if (net == nullptr) {
    return;
  }

  const std::string& net_name = net->get_net_name();

  std::vector<ACGraphNode> nodes;
  std::vector<ACEdge> all_edges;

  size_t estimated_nodes = 0;

  if (net->get_instance_pin_list() != nullptr) {
    estimated_nodes += net->get_instance_pin_list()->get_pin_list().size() * 2;
  }

  if (net->get_io_pins() != nullptr) {
    estimated_nodes += net->get_io_pins()->get_pin_list().size() * 2;
  }

  if (net->get_wire_list() != nullptr) {
    estimated_nodes += net->get_wire_list()->get_wire_list().size() * 4;
  }

  nodes.reserve(estimated_nodes);
  all_edges.reserve(estimated_nodes * 2);

  int max_time = 0;
  int max_shape_layer = 0;

  auto addEdge = [&](int a, int b, int t) {
    if (a < 0 || b < 0 || a == b) {
      return;
    }
    if (a > b) {
      std::swap(a, b);
    }
    if (t < 0) {
      t = 0;
    }
    if (t > max_layer_order) {
      return;
    }

    max_time = std::max(max_time, t);
    all_edges.push_back({a, b, t});
  };

  auto createGateNode = [&](double gate_area, double diff_area, bool provides_diff, const std::string& pin_name,
                            const std::string& inst_name) -> int {
    if (Utility::equalDoubleByError(gate_area, 0.0, ZH_ERROR) && Utility::equalDoubleByError(diff_area, 0.0, ZH_ERROR) && !provides_diff) {
      return -1;
    }

    ACGraphNode n;
    n.id = static_cast<int>(nodes.size());
    n.time = 0;
    n.is_conductor = false;

    n.gate_area = gate_area;
    n.diff_area = diff_area;
    n.provides_diff = provides_diff || !Utility::equalDoubleByError(diff_area, 0.0, ZH_ERROR);
    n.pin_name = pin_name;
    n.inst_name = inst_name;

    nodes.push_back(std::move(n));
    return static_cast<int>(nodes.size()) - 1;
  };

  auto createConductorNode = [&](int order, bool is_routing, bool is_cut, const std::vector<idb::IdbRect>& rects) -> int {
    if (order < 0 || order > max_layer_order || rects.empty()) {
      return -1;
    }

    ACGraphNode n;
    n.id = static_cast<int>(nodes.size());
    n.time = order;
    n.is_conductor = true;
    n.is_routing = is_routing;
    n.is_cut = is_cut;

    n.shapes.reserve(rects.size());
    for (const auto& r : rects) {
      n.shapes.push_back({order, r});
    }

    max_time = std::max(max_time, order);
    max_shape_layer = std::max(max_shape_layer, order);

    nodes.push_back(std::move(n));
    return static_cast<int>(nodes.size()) - 1;
  };

  auto isRoutingLike = [&](idb::IdbLayer* layer, int order) -> bool {
    if (layer == nullptr) {
      return false;
    }
    if (layer->is_routing()) {
      return true;
    }
    if (layer->get_type() == idb::IdbLayerType::kLayerMasterslice && order >= 0 && static_cast<size_t>(order) < conductive_order.size()
        && conductive_order[order]) {
      return true;
    }
    return false;
  };

  auto createVirtualConductorNode = [&](int order, double area, bool is_routing, bool is_side, bool is_cut) -> int {
    ACGraphNode n;
    n.id = static_cast<int>(nodes.size());
    n.time = order;
    n.is_conductor = true;
    n.is_routing = is_routing;
    n.is_side = is_side;
    n.is_cut = is_cut;
    n.declared_area = area;

    max_time = std::max(max_time, order);
    nodes.push_back(std::move(n));
    return static_cast<int>(nodes.size()) - 1;
  };

  auto processPin = [&](idb::IdbPin* pin, bool instance_pin) {
    if (pin == nullptr) {
      return;
    }

    double gate_area = 0.0;
    double diff_area = 0.0;
    bool provides_diff = false;

    readPinAntennaInfo(ac_model, pin, instance_pin, gate_area, diff_area, provides_diff);
    std::string pin_name = pin->get_pin_name();
    std::string inst_name;
    if (instance_pin && pin->get_instance() != nullptr) {
      inst_name = pin->get_instance()->get_name();
    }
    int gate_node = createGateNode(gate_area, diff_area, provides_diff, pin_name, inst_name);

    std::vector<ACPinShape> pin_shapes;

    for (idb::IdbLayerShape* shape : pin->get_port_box_list()) {
      if (shape == nullptr || shape->get_layer() == nullptr) {
        continue;
      }

      idb::IdbLayer* layer = shape->get_layer();
      int order = static_cast<int>(layer->get_order());

      if (order < 0 || order > max_layer_order) {
        conductors_out_of_range.fetch_add(1, std::memory_order_relaxed);
        continue;
      }

      bool routing = isRoutingLike(layer, order);
      bool cut = layer->is_cut();

      for (idb::IdbRect* r : shape->get_rect_list()) {
        if (r != nullptr) {
          pin_shapes.push_back({order, routing, cut, *r});
        }
      }
    }

    int first_shape_node = -1;

    if (!pin_shapes.empty()) {
      std::sort(pin_shapes.begin(), pin_shapes.end(), [](const ACPinShape& a, const ACPinShape& b) { return a.order < b.order; });

      size_t i = 0;
      while (i < pin_shapes.size()) {
        int order = pin_shapes[i].order;

        bool routing = false;
        bool cut = false;
        std::vector<idb::IdbRect> rects;

        while (i < pin_shapes.size() && pin_shapes[i].order == order) {
          routing = routing || pin_shapes[i].routing;
          cut = cut || pin_shapes[i].cut;
          rects.push_back(pin_shapes[i].rect);
          ++i;
        }

        int shape_node = createConductorNode(order, routing, cut, rects);

        if (shape_node >= 0 && first_shape_node < 0) {
          first_shape_node = shape_node;
        }

        if (gate_node >= 0 && shape_node >= 0) {
          addEdge(gate_node, shape_node, order);
        }
      }
    }

    idb::IdbTerm* term = pin->get_term();
    if (term != nullptr
        && (term->has_antenna_partial_metal_area() || term->has_antenna_partial_metal_side_area() || term->has_antenna_partial_cut_area())) {
      int anchor = (gate_node >= 0) ? gate_node : first_shape_node;

      idb::IdbLayers* layers = (design && design->get_layout()) ? design->get_layout()->get_layers() : nullptr;

      auto addPartialArea = [&](const std::string& layer_name, double area, bool is_routing, bool is_side, bool is_cut) {
        if (anchor < 0 || layers == nullptr) {
          partial_areas_dropped.fetch_add(1, std::memory_order_relaxed);
          return;
        }

        int order = layers->get_layer_order(layer_name);
        if (order < 0 || order > max_layer_order) {
          partial_areas_dropped.fetch_add(1, std::memory_order_relaxed);
          return;
        }

        int vnode = createVirtualConductorNode(order, area, is_routing, is_side, is_cut);
        addEdge(anchor, vnode, order);
      };

      for (const auto& [layer_name, area] : term->get_antenna_partial_metal_area()) {
        addPartialArea(layer_name, area, true, false, false);
      }
      for (const auto& [layer_name, area] : term->get_antenna_partial_metal_side_area()) {
        addPartialArea(layer_name, area, false, true, false);
      }
      for (const auto& [layer_name, area] : term->get_antenna_partial_cut_area()) {
        addPartialArea(layer_name, area, false, false, true);
      }
    }
  };

  if (net->get_instance_pin_list() != nullptr) {
    for (idb::IdbPin* pin : net->get_instance_pin_list()->get_pin_list()) {
      processPin(pin, true);
    }
  }

  if (net->get_io_pins() != nullptr) {
    for (idb::IdbPin* pin : net->get_io_pins()->get_pin_list()) {
      processPin(pin, false);
    }
  }

  if (net->get_wire_list() != nullptr) {
    for (idb::IdbRegularWire* wire : net->get_wire_list()->get_wire_list()) {
      if (wire == nullptr) {
        continue;
      }

      for (idb::IdbRegularWireSegment* seg : wire->get_segment_list()) {
        if (seg == nullptr) {
          continue;
        }

        if (seg->is_wire()) {
          idb::IdbLayer* layer = seg->get_layer();

          if (layer != nullptr) {
            int order = static_cast<int>(layer->get_order());

            if (isRoutingLike(layer, order)) {
              if (order < 0 || order > max_layer_order) {
                conductors_out_of_range.fetch_add(1, std::memory_order_relaxed);
              } else {
                std::vector<idb::IdbRect> rects;
                rects.push_back(seg->get_segment_rect());
                createConductorNode(order, true, false, rects);
              }
            } else {
              skipped_segments.fetch_add(1, std::memory_order_relaxed);
            }
          } else {
            skipped_segments.fetch_add(1, std::memory_order_relaxed);
          }
        }

        if (seg->is_via()) {
          for (idb::IdbVia* via : seg->get_via_list()) {
            if (via == nullptr) {
              continue;
            }

            auto collectShapeRects = [&](idb::IdbLayerShape& shape, int& order, bool& is_routing, bool& is_cut) -> std::vector<idb::IdbRect> {
              std::vector<idb::IdbRect> rects;
              idb::IdbLayer* layer = shape.get_layer();
              if (layer == nullptr) {
                return rects;
              }

              order = static_cast<int>(layer->get_order());
              is_routing = layer->is_routing();
              is_cut = layer->is_cut();

              if (order < 0 || order > max_layer_order) {
                return rects;
              }

              for (idb::IdbRect* r : shape.get_rect_list()) {
                if (r != nullptr) {
                  rects.push_back(*r);
                }
              }

              return rects;
            };

            idb::IdbLayerShape bottom_shape = via->get_bottom_layer_shape();
            idb::IdbLayerShape cut_shape = via->get_cut_layer_shape();
            idb::IdbLayerShape top_shape = via->get_top_layer_shape();

            int bottom_order = -1;
            bool bottom_routing = false;
            bool bottom_cut = false;

            int cut_order = -1;
            bool cut_routing = false;
            bool cut_is_cut = false;

            int top_order = -1;
            bool top_routing = false;
            bool top_cut = false;

            auto bottom_rects = collectShapeRects(bottom_shape, bottom_order, bottom_routing, bottom_cut);
            auto cut_rects = collectShapeRects(cut_shape, cut_order, cut_routing, cut_is_cut);
            auto top_rects = collectShapeRects(top_shape, top_order, top_routing, top_cut);

            int b_id = createConductorNode(bottom_order, bottom_routing, bottom_cut, bottom_rects);
            int c_id = createConductorNode(cut_order, cut_routing, cut_is_cut, cut_rects);
            int t_id = createConductorNode(top_order, top_routing, top_cut, top_rects);

            if (b_id >= 0 && c_id >= 0) {
              addEdge(b_id, c_id, nodes[c_id].time);
            }
            if (c_id >= 0 && t_id >= 0) {
              addEdge(c_id, t_id, nodes[t_id].time);
            }
            if (b_id >= 0 && t_id >= 0 && c_id < 0) {
              addEdge(b_id, t_id, nodes[t_id].time);
            }
          }
        }

        if (!seg->is_wire() && !seg->is_via()) {
          skipped_segments.fetch_add(1, std::memory_order_relaxed);
        }
      }
    }
  }

  if (nodes.empty()) {
    return;
  }

  int max_layer_idx = std::max(max_time, max_shape_layer);
  if (max_layer_idx < 0) {
    return;
  }

  std::vector<std::vector<std::pair<BGRectInt, int>>> items_by_layer(static_cast<size_t>(max_layer_idx) + 1);

  for (size_t i = 0; i < nodes.size(); ++i) {
    if (!nodes[i].is_conductor) {
      continue;
    }

    for (const auto& s : nodes[i].shapes) {
      if (s.layer_order < 0 || s.layer_order > max_layer_idx) {
        continue;
      }

      BGPointInt p_min(s.rect.get_low_x(), s.rect.get_low_y());
      BGPointInt p_max(s.rect.get_high_x(), s.rect.get_high_y());
      BGRectInt b_rect(p_min, p_max);

      items_by_layer[s.layer_order].emplace_back(b_rect, static_cast<int>(i));
    }
  }

  constexpr size_t kBruteForceThreshold = 8;
  using RTree = bgi::rtree<std::pair<BGRectInt, int>, bgi::quadratic<16>>;

  std::vector<std::optional<RTree>> rtrees(static_cast<size_t>(max_layer_idx) + 1);

  for (int layer = 0; layer <= max_layer_idx; ++layer) {
    auto& items = items_by_layer[layer];
    if (items.empty()) {
      continue;
    }

    if (items.size() < kBruteForceThreshold) {
      for (size_t a = 0; a < items.size(); ++a) {
        for (size_t b = a + 1; b < items.size(); ++b) {
          if (items[a].second == items[b].second) {
            continue;
          }

          if (bg::intersects(items[a].first, items[b].first)) {
            addEdge(items[a].second, items[b].second, layer);
          }
        }
      }
    } else {
      rtrees[layer].emplace(items.begin(), items.end());
    }
  }

  for (size_t i = 0; i < nodes.size(); ++i) {
    if (!nodes[i].is_conductor) {
      continue;
    }

    for (const auto& s : nodes[i].shapes) {
      if (s.layer_order < 0 || s.layer_order > max_layer_idx) {
        continue;
      }

      auto& items = items_by_layer[s.layer_order];
      if (items.size() < kBruteForceThreshold) {
        continue;
      }

      BGPointInt p_min(s.rect.get_low_x(), s.rect.get_low_y());
      BGPointInt p_max(s.rect.get_high_x(), s.rect.get_high_y());
      BGRectInt b_rect(p_min, p_max);

      std::vector<std::pair<BGRectInt, int>> results;
      if (rtrees[s.layer_order].has_value()) {
        rtrees[s.layer_order]->query(bgi::intersects(b_rect), std::back_inserter(results));
      }

      for (const auto& res : results) {
        if (res.second > static_cast<int>(i)) {
          addEdge(static_cast<int>(i), res.second, s.layer_order);
        }
      }
    }
  }

  std::vector<std::vector<ACEdge>> edges_by_time(static_cast<size_t>(max_time) + 1);
  for (auto& e : all_edges) {
    if (e.time >= 0 && e.time <= max_time) {
      edges_by_time[e.time].push_back(std::move(e));
    }
  }
  std::vector<ACEdge>().swap(all_edges);

  std::vector<std::vector<int>> nodes_by_time(static_cast<size_t>(max_time) + 1);
  for (size_t i = 0; i < nodes.size(); ++i) {
    if (!nodes[i].is_conductor) {
      continue;
    }

    int t = nodes[i].time;
    if (t >= 0 && t <= max_time) {
      nodes_by_time[t].push_back(static_cast<int>(i));
    }
  }

  ACUnionFind uf(static_cast<int>(nodes.size()), nodes);

  std::vector<std::vector<int>> comps_by_root(nodes.size());
  std::vector<int> active_roots;

  std::vector<idb::IdbRect> routing_rects;
  std::vector<idb::IdbRect> cut_rects;

  auto edgeCmp = [](const ACEdge& a, const ACEdge& b) { return std::tie(a.time, a.a, a.b) < std::tie(b.time, b.a, b.b); };

  auto edgeEq = [](const ACEdge& a, const ACEdge& b) { return a.time == b.time && a.a == b.a && a.b == b.b; };

  for (int T = 0; T <= max_time; ++T) {
    auto& edges = edges_by_time[T];

    if (!edges.empty()) {
      std::sort(edges.begin(), edges.end(), edgeCmp);
      edges.erase(std::unique(edges.begin(), edges.end(), edgeEq), edges.end());

      for (const auto& e : edges) {
        uf.merge(e.a, e.b);
      }
    }

    if (nodes_by_time[T].empty()) {
      continue;
    }

    const ACAntennaRule* routing_rule = AntennaRuleEvaluator::pickRule(ac_model, T, true);
    const ACAntennaRule* cut_rule = AntennaRuleEvaluator::pickRule(ac_model, T, false);

    active_roots.clear();

    for (int u : nodes_by_time[T]) {
      int root = uf.find(u);

      if (comps_by_root[root].empty()) {
        active_roots.push_back(root);
      }

      comps_by_root[root].push_back(u);
    }

    if (active_roots.empty()) {
      continue;
    }

    if (routing_rule == nullptr && cut_rule == nullptr) {
      for (int root : active_roots) {
        ACUFNode& root_data = uf.d[root];
        for (int u : comps_by_root[root]) {
          if (nodes[u].is_conductor && nodes[u].declared_area > 0.0) {
            if (nodes[u].is_cut) {
              root_data.cum_cut_num += nodes[u].declared_area;
            } else if (nodes[u].is_side) {
              root_data.cum_side_num += nodes[u].declared_area;
            } else if (nodes[u].is_routing) {
              root_data.cum_area_num += nodes[u].declared_area;
            }
          }
        }
        comps_by_root[root].clear();
      }
      continue;
    }

    for (int root : active_roots) {
      auto& comp_nodes = comps_by_root[root];
      ACUFNode& root_data = uf.d[root];

      routing_rects.clear();
      cut_rects.clear();

      bool has_bbox = false;
      int64_t bbox_lx = std::numeric_limits<int64_t>::max();
      int64_t bbox_ly = std::numeric_limits<int64_t>::max();
      int64_t bbox_hx = std::numeric_limits<int64_t>::min();
      int64_t bbox_hy = std::numeric_limits<int64_t>::min();

      auto addRectToBBox = [&](const idb::IdbRect& r) {
        int64_t lx = std::min(r.get_low_x(), r.get_high_x());
        int64_t ly = std::min(r.get_low_y(), r.get_high_y());
        int64_t hx = std::max(r.get_low_x(), r.get_high_x());
        int64_t hy = std::max(r.get_low_y(), r.get_high_y());

        bbox_lx = std::min(bbox_lx, lx);
        bbox_ly = std::min(bbox_ly, ly);
        bbox_hx = std::max(bbox_hx, hx);
        bbox_hy = std::max(bbox_hy, hy);
        has_bbox = true;
      };

      double declared_routing_area = 0.0;
      double declared_side_area = 0.0;
      double declared_cut_area = 0.0;
      std::string victim_pin;
      std::string victim_inst;

      auto collectVictim = [&](int node_idx) {
        int gate_root = uf.find(node_idx);
        if (gate_root < 0 || gate_root >= static_cast<int>(nodes.size())) {
          return;
        }
        if (!victim_pin.empty()) {
          return;
        }
        for (size_t gi = 0; gi < nodes.size(); ++gi) {
          if (nodes[gi].is_conductor) {
            continue;
          }
          if (uf.find(static_cast<int>(gi)) != gate_root) {
            continue;
          }
          if (!nodes[gi].pin_name.empty()) {
            victim_pin = nodes[gi].pin_name;
            victim_inst = nodes[gi].inst_name;
            return;
          }
        }
      };

      for (int u : comp_nodes) {
        if (!nodes[u].is_conductor) {
          continue;
        }

        if (nodes[u].declared_area > 0.0) {
          if (nodes[u].is_cut) {
            declared_cut_area += nodes[u].declared_area;
          } else if (nodes[u].is_side) {
            declared_side_area += nodes[u].declared_area;
          } else if (nodes[u].is_routing) {
            declared_routing_area += nodes[u].declared_area;
          }
        }

        for (const auto& s : nodes[u].shapes) {
          if (s.layer_order != T) {
            continue;
          }

          if (nodes[u].is_routing) {
            routing_rects.push_back(s.rect);
            addRectToBBox(s.rect);
          } else if (nodes[u].is_cut) {
            cut_rects.push_back(s.rect);
            addRectToBBox(s.rect);
          }
        }
      }

      collectVictim(root);

      if (routing_rule == nullptr) {
        root_data.cum_area_num += declared_routing_area;
        root_data.cum_side_num += declared_side_area;
      }
      if (cut_rule == nullptr) {
        root_data.cum_cut_num += declared_cut_area;
      }

      double cut_area_um = declared_cut_area;
      if (!cut_rects.empty()) {
        double physical_cut_area_um = 0.0;
        AntennaGeometry::unionArea(cut_rects, micron_dbu, physical_cut_area_um);
        cut_area_um += physical_cut_area_um;
      }

      double metal_area_um = declared_routing_area;
      double side_area_um = declared_side_area;
      if (!routing_rects.empty()) {
        double physical_metal_area_um = 0.0;
        double physical_metal_perimeter_um = 0.0;
        AntennaGeometry::unionAreaPerimeter(routing_rects, micron_dbu, physical_metal_area_um, physical_metal_perimeter_um);
        metal_area_um += physical_metal_area_um;

        double thickness_um = routing_rule ? routing_rule->thickness_um : 0.0;
        if (thickness_um <= 0.0 && static_cast<size_t>(T) < thickness_by_order.size()) {
          thickness_um = thickness_by_order[T];
        }
        side_area_um += physical_metal_perimeter_um * thickness_um;
      }

      const bool has_routing
          = !Utility::equalDoubleByError(metal_area_um, 0.0, ZH_ERROR) || !Utility::equalDoubleByError(side_area_um, 0.0, ZH_ERROR);
      const bool has_cut = !Utility::equalDoubleByError(cut_area_um, 0.0, ZH_ERROR);

      if (!has_routing && !has_cut) {
        comps_by_root[root].clear();
        continue;
      }

      if (Utility::equalDoubleByError(root_data.gate_area, 0.0, ZH_ERROR) && Utility::equalDoubleByError(root_data.diff_area, 0.0, ZH_ERROR)
          && !root_data.diff_connected) {
        comps_without_gate.fetch_add(1, std::memory_order_relaxed);
        comps_by_root[root].clear();
        continue;
      }

      const bool diff_active = root_data.diff_connected || !Utility::equalDoubleByError(root_data.diff_area, 0.0, ZH_ERROR);

      if (!has_bbox) {
        for (int u : comp_nodes) {
          if (has_bbox) {
            break;
          }
          for (const auto& s : nodes[u].shapes) {
            addRectToBBox(s.rect);
          }
        }
      }

      if (has_cut && cut_rule != nullptr) {
        const double gate_plus_diff_factor = (cut_rule->gate_plus_diff >= 0.0) ? cut_rule->gate_plus_diff : 0.0;
        const double eff_gate = root_data.gate_area + gate_plus_diff_factor * root_data.diff_area;
        if (Utility::equalDoubleByError(eff_gate, 0.0, ZH_ERROR)) {
          comps_without_gate.fetch_add(1, std::memory_order_relaxed);
        } else {
          AntennaGeometry::evaluateCut(*cut_rule, root_data, cut_area_um, diff_active, micron_dbu, has_bbox, bbox_lx, bbox_ly, bbox_hx,
                                       bbox_hy, net_name, victim_pin, victim_inst, T, root_data.gate_area, root_data.diff_area,
                                       metal_area_um, out_violations, root_data.cum_cut_num);
        }
      }

      if (has_routing && routing_rule != nullptr) {
        const double gate_plus_diff_factor = (routing_rule->gate_plus_diff >= 0.0) ? routing_rule->gate_plus_diff : 0.0;
        const double eff_gate = root_data.gate_area + gate_plus_diff_factor * root_data.diff_area;
        if (Utility::equalDoubleByError(eff_gate, 0.0, ZH_ERROR)) {
          comps_without_gate.fetch_add(1, std::memory_order_relaxed);
        } else {
          AntennaGeometry::evaluateRouting(*routing_rule, root_data, metal_area_um, side_area_um, diff_active, micron_dbu, has_bbox, bbox_lx,
                                           bbox_ly, bbox_hx, bbox_hy, net_name, victim_pin, victim_inst, T, root_data.gate_area,
                                           root_data.diff_area, cut_area_um, out_violations, root_data.cum_area_num, root_data.cum_side_num);
        }
      }

      comps_by_root[root].clear();
    }
  }
}

}  // namespace izh
