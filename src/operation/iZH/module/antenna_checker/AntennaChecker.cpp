// ***************************************************************************************
// Copyright (c) 2023-2025 Peng Cheng Laboratory
// Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences
// Copyright (c) 2023-2025 Beijing Institute of Open Source Chip
//
// iEDA is licensed under Mulan PSL v2.
// You can use this software according to the terms and conditions of the Mulan PSL v2.
// You may obtain a copy of Mulan PSL v2 at:
// http://license.coscl.org.cn/MulanPSL2
//
// THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
// EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
// MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
//
// See the Mulan PSL v2 for more details.
// ***************************************************************************************
#include "AntennaChecker.hpp"

#include <omp.h>

#include <algorithm>
#include <any>
#include <atomic>
#include <fstream>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "../../../basic/geometry/boost_definition.h"
#include "IdbDesign.h"
#include "IdbLayer.h"
#include "IdbLayout.h"
#include "IdbNet.h"
#include "IdbPins.h"
#include "IdbRegularWire.h"
#include "IdbTerm.h"
#include "IdbUnits.h"
#include "IdbVias.h"
#include "idm.h"

namespace izh {

namespace {

enum class ViolationType
{
  kAntennaPar,
  kAntennaDiffPar,
  kAntennaCar,
  kAntennaDiffCar,
  kAntennaPsr,
  kAntennaDiffPsr,
  kAntennaCsr,
  kAntennaDiffCsr,
  kAntennaCutPar,
  kAntennaCutCar
};

struct Violation
{
  std::string net_name;
  std::string layer_name;
  ViolationType type;
  double ratio;
  double threshold;
  double lx = 0.0, ly = 0.0, hx = 0.0, hy = 0.0;
};

class AntennaRule
{
 public:
  std::string layer_name;
  int layer_order;
  bool is_routing;
  int32_t thickness = 0;

  double area_ratio = -1;
  double cum_area_ratio = -1;
  double area_factor = -1;
  bool area_factor_diffuse_only = false;
  double side_area_ratio = -1;
  double cum_side_area_ratio = -1;
  double side_area_factor = -1;
  bool side_area_factor_diffuse_only = false;

  double gate_plus_diff = -1;
  double area_minus_diff = -1;

  double diff_area_ratio = -1;
  double cum_diff_area_ratio = -1;
  double diff_side_area_ratio = -1;
  double cum_diff_side_area_ratio = -1;

  std::vector<std::pair<double, double>> diff_area_ratio_pwl;
  std::vector<std::pair<double, double>> cum_diff_area_ratio_pwl;
  std::vector<std::pair<double, double>> diff_side_area_ratio_pwl;
  std::vector<std::pair<double, double>> cum_diff_side_area_ratio_pwl;

  bool cum_routing_plus_cut = false;

  double cut_area_factor = -1;
  bool cut_area_factor_diffuse_only = false;
};

double evalPwl(const std::vector<std::pair<double, double>>& pwl, double x)
{
  if (pwl.empty()) {
    return 0;
  }
  if (x <= pwl.front().first) {
    return pwl.front().second;
  }
  if (x >= pwl.back().first) {
    return pwl.back().second;
  }
  for (size_t i = 0; i + 1 < pwl.size(); i++) {
    if (x >= pwl[i].first && x <= pwl[i + 1].first) {
      double dx = pwl[i + 1].first - pwl[i].first;
      if (dx < 1e-10) {
        return pwl[i].second;
      }
      double t = (x - pwl[i].first) / dx;
      return pwl[i].second + t * (pwl[i + 1].second - pwl[i].second);
    }
  }
  return pwl.back().second;
}

struct Shape
{
  int layer_order;
  idb::IdbRect rect;
};

struct GraphNode
{
  int id;
  int available_time;

  bool is_pin = false;
  double gate_area = 0.0;
  double diff_area = 0.0;

  bool is_routing = false;
  double metal_area = 0.0;
  double side_area = 0.0;

  bool is_cut = false;
  double cut_area = 0.0;

  std::vector<Shape> shapes;
};

struct UFNode
{
  int parent;
  int rank;

  double gate_area;
  double diff_area;
  double cum_metal;
  double cum_side;
  double cum_cut;
};

struct UnionFind
{
  std::vector<UFNode> d;
  UnionFind(int n, const std::vector<GraphNode>& nodes)
  {
    d.resize(n);
    for (int i = 0; i < n; i++) {
      d[i].parent = i;
      d[i].rank = 0;
      d[i].gate_area = nodes[i].gate_area;
      d[i].diff_area = nodes[i].diff_area;
      d[i].cum_metal = 0;
      d[i].cum_side = 0;
      d[i].cum_cut = 0;
    }
  }
  int find(int i)
  {
    if (d[i].parent == i)
      return i;
    return d[i].parent = find(d[i].parent);
  }
  void merge(int i, int j)
  {
    int root_i = find(i);
    int root_j = find(j);
    if (root_i != root_j) {
      if (d[root_i].rank < d[root_j].rank) {
        std::swap(root_i, root_j);
      }
      d[root_j].parent = root_i;
      if (d[root_i].rank == d[root_j].rank) {
        d[root_i].rank++;
      }
      d[root_i].gate_area += d[root_j].gate_area;
      d[root_i].diff_area += d[root_j].diff_area;
      d[root_i].cum_metal += d[root_j].cum_metal;
      d[root_i].cum_side += d[root_j].cum_side;
      d[root_i].cum_cut += d[root_j].cum_cut;
    }
  }
};

class AntennaCheckerImpl
{
 public:
  AntennaCheckerImpl(idb::IdbDesign* design) : _design(design)
  {
    if (_design && _design->get_layout() && _design->get_layout()->get_units()) {
      _micron_dbu = _design->get_layout()->get_units()->get_micron_dbu();
    }
  }

  void run()
  {
    _violations.clear();
    if (!_design)
      return;
    initLayers();
    if (_rules.empty()) {
      ZHLOG.info(Loc::current(), "No antenna rules defined in technology; skipping antenna check");
      return;
    }
    idb::IdbNetList* net_list = _design->get_net_list();
    if (!net_list)
      return;

    std::vector<idb::IdbNet*> signal_nets;
    for (idb::IdbNet* net : net_list->get_net_list()) {
      if (net && net->is_signal()) {
        signal_nets.push_back(net);
      }
    }

    const int nthreads = std::max(1u, std::thread::hardware_concurrency());
    std::vector<std::vector<Violation>> local_violations(nthreads);

#pragma omp parallel for schedule(dynamic, 16) num_threads(nthreads)
    for (size_t i = 0; i < signal_nets.size(); i++) {
      int tid = omp_get_thread_num();
      checkNet(signal_nets[i], local_violations[tid]);
    }

    for (auto& v : local_violations) {
      _violations.insert(_violations.end(), std::make_move_iterator(v.begin()), std::make_move_iterator(v.end()));
    }

    std::sort(_violations.begin(), _violations.end(), [](const Violation& a, const Violation& b) {
      return std::tie(a.net_name, a.layer_name, a.type, a.ratio, a.lx, a.ly, a.hx, a.hy)
             < std::tie(b.net_name, b.layer_name, b.type, b.ratio, b.lx, b.ly, b.hx, b.hy);
    });

    if (_pins_missing_antenna_info.load() > 0) {
      ZHLOG.warn(Loc::current(), _pins_missing_antenna_info.load(),
                 " instance pins have no antenna gate/diffusion area annotation; affected nets may be under-checked");
    }
    if (_comps_without_gate.load() > 0) {
      ZHLOG.info(Loc::current(), _comps_without_gate.load(), " components had conductor area but no gate below them (skipped)");
    }
    if (_skipped_segments.load() > 0) {
      ZHLOG.warn(Loc::current(), _skipped_segments.load(), " segments were neither wire nor via and were skipped");
    }
  }

  int get_violation_count() const { return _violations.size(); }
  const std::vector<Violation>& get_violations() const { return _violations; }

 private:
  void initLayers()
  {
    idb::IdbLayers* layers = _design->get_layout()->get_layers();

    _max_layer_order = 0;
    for (idb::IdbLayer* layer : layers->get_routing_layers())
      _max_layer_order = std::max(_max_layer_order, layer->get_order());
    for (idb::IdbLayer* layer : layers->get_cut_layers())
      _max_layer_order = std::max(_max_layer_order, layer->get_order());

    for (idb::IdbLayer* layer : layers->get_routing_layers()) {
      idb::IdbLayerRouting* routing = dynamic_cast<idb::IdbLayerRouting*>(layer);
      if (routing == nullptr) {
        continue;
      }
      AntennaRule rule;
      rule.layer_name = routing->get_name();
      rule.layer_order = routing->get_order();
      rule.is_routing = true;
      rule.thickness = routing->get_thickness();

      if (routing->has_antenna_area_ratio())
        rule.area_ratio = routing->get_antenna_area_ratio();
      if (routing->has_antenna_cum_area_ratio())
        rule.cum_area_ratio = routing->get_antenna_cum_area_ratio();
      if (routing->has_antenna_area_factor())
        rule.area_factor = routing->get_antenna_area_factor();
      if (routing->has_antenna_side_area_ratio())
        rule.side_area_ratio = routing->get_antenna_side_area_ratio();
      if (routing->has_antenna_cum_side_area_ratio())
        rule.cum_side_area_ratio = routing->get_antenna_cum_side_area_ratio();
      if (routing->has_antenna_side_area_factor())
        rule.side_area_factor = routing->get_antenna_side_area_factor();
      if (routing->has_antenna_gate_plus_diff())
        rule.gate_plus_diff = routing->get_antenna_gate_plus_diff();
      if (routing->has_antenna_area_minus_diff())
        rule.area_minus_diff = routing->get_antenna_area_minus_diff();
      if (routing->has_antenna_diff_area_ratio())
        rule.diff_area_ratio = routing->get_antenna_diff_area_ratio();
      if (routing->has_antenna_cum_diff_area_ratio())
        rule.cum_diff_area_ratio = routing->get_antenna_cum_diff_area_ratio();
      if (routing->has_antenna_diff_side_area_ratio())
        rule.diff_side_area_ratio = routing->get_antenna_diff_side_area_ratio();
      if (routing->has_antenna_cum_diff_side_area_ratio())
        rule.cum_diff_side_area_ratio = routing->get_antenna_cum_diff_side_area_ratio();
      rule.cum_routing_plus_cut = routing->get_antenna_cum_routing_plus_cut();
      rule.area_factor_diffuse_only = routing->get_antenna_area_factor_diffuse_only();
      rule.side_area_factor_diffuse_only = routing->get_antenna_side_area_factor_diffuse_only();

      if (routing->has_antenna_diff_area_ratio_pwl())
        rule.diff_area_ratio_pwl = routing->get_antenna_diff_area_ratio_pwl();
      if (routing->has_antenna_cum_diff_area_ratio_pwl())
        rule.cum_diff_area_ratio_pwl = routing->get_antenna_cum_diff_area_ratio_pwl();
      if (routing->has_antenna_diff_side_area_ratio_pwl())
        rule.diff_side_area_ratio_pwl = routing->get_antenna_diff_side_area_ratio_pwl();
      if (routing->has_antenna_cum_diff_side_area_ratio_pwl())
        rule.cum_diff_side_area_ratio_pwl = routing->get_antenna_cum_diff_side_area_ratio_pwl();

      _rules.push_back(rule);
    }

    for (idb::IdbLayer* layer : layers->get_cut_layers()) {
      idb::IdbLayerCut* cut = dynamic_cast<idb::IdbLayerCut*>(layer);
      if (cut == nullptr) {
        continue;
      }
      const bool has_any_antenna_rule = cut->has_antenna_cut_area_factor() || cut->has_antenna_area_ratio() || cut->has_antenna_cum_area_ratio()
                                        || cut->has_antenna_diff_area_ratio() || cut->has_antenna_cum_diff_area_ratio();
      if (!has_any_antenna_rule) {
        continue;
      }

      AntennaRule rule;
      rule.layer_name = cut->get_name();
      rule.layer_order = cut->get_order();
      rule.is_routing = false;
      rule.cut_area_factor = cut->has_antenna_cut_area_factor() ? cut->get_antenna_cut_area_factor() : 1.0;
      rule.cut_area_factor_diffuse_only = cut->get_antenna_cut_area_factor_diffuse_only();

      if (cut->has_antenna_area_ratio())
        rule.area_ratio = cut->get_antenna_area_ratio();
      if (cut->has_antenna_cum_area_ratio())
        rule.cum_area_ratio = cut->get_antenna_cum_area_ratio();
      if (cut->has_antenna_diff_area_ratio())
        rule.diff_area_ratio = cut->get_antenna_diff_area_ratio();
      if (cut->has_antenna_cum_diff_area_ratio())
        rule.cum_diff_area_ratio = cut->get_antenna_cum_diff_area_ratio();
      if (cut->has_antenna_gate_plus_diff())
        rule.gate_plus_diff = cut->get_antenna_gate_plus_diff();
      if (cut->has_antenna_area_minus_diff())
        rule.area_minus_diff = cut->get_antenna_area_minus_diff();
      rule.cum_routing_plus_cut = cut->get_antenna_cum_routing_plus_cut();

      if (cut->has_antenna_diff_area_ratio_pwl())
        rule.diff_area_ratio_pwl = cut->get_antenna_diff_area_ratio_pwl();
      if (cut->has_antenna_cum_diff_area_ratio_pwl())
        rule.cum_diff_area_ratio_pwl = cut->get_antenna_cum_diff_area_ratio_pwl();

      _rules.push_back(rule);
    }

    int max_rule_order = 0;
    for (const auto& r : _rules) {
      max_rule_order = std::max(max_rule_order, r.layer_order);
    }
    _rule_map.assign(max_rule_order + 1, nullptr);
    for (const auto& r : _rules) {
      _rule_map[r.layer_order] = &r;
    }
  }

  void checkNet(idb::IdbNet* net, std::vector<Violation>& out_violations)
  {
    if (net == nullptr)
      return;
    const std::string& net_name = net->get_net_name();

    std::vector<GraphNode> nodes;
    std::vector<std::pair<int, int>> all_edges;

    // Estimate sizes to avoid reallocation
    size_t estimated_nodes = 0;
    if (net->get_instance_pin_list() != nullptr)
      estimated_nodes += net->get_instance_pin_list()->get_pin_list().size();
    if (net->get_io_pins() != nullptr)
      estimated_nodes += net->get_io_pins()->get_pin_list().size();
    if (net->get_wire_list() != nullptr)
      estimated_nodes += net->get_wire_list()->get_wire_list().size() * 3;  // Rough guess for segments/vias
    nodes.reserve(estimated_nodes);
    all_edges.reserve(estimated_nodes);

    auto getRectMicrons = [&](int64_t dbu_val) -> double { return static_cast<double>(dbu_val) / _micron_dbu; };

    // 1. Instance pins
    if (net->get_instance_pin_list() != nullptr) {
      for (idb::IdbPin* pin : net->get_instance_pin_list()->get_pin_list()) {
        if (pin == nullptr)
          continue;
        GraphNode n;
        n.id = nodes.size();
        n.available_time = 0;
        n.is_pin = true;
        idb::IdbTerm* term = pin->get_term();
        if (term != nullptr) {
          n.gate_area = term->has_antenna_gate_area() ? term->get_antenna_gate_area() : 0.0;
          n.diff_area = term->has_antenna_diff_area() ? term->get_antenna_diff_area() : 0.0;
          if (!term->has_antenna_gate_area() && !term->has_antenna_diff_area()) {
            _pins_missing_antenna_info.fetch_add(1, std::memory_order_relaxed);
          }
        } else {
          _pins_missing_antenna_info.fetch_add(1, std::memory_order_relaxed);
        }
        for (idb::IdbLayerShape* shape : pin->get_port_box_list()) {
          if (shape != nullptr && shape->get_layer() != nullptr) {
            int l_order = shape->get_layer()->get_order();
            if (l_order < 0 || l_order > _max_layer_order) {
              ZHLOG.warn(Loc::current(), "Skipping instance pin shape with suspicious layer_order=", l_order, " net=", net_name);
              continue;
            }
            for (idb::IdbRect* r : shape->get_rect_list()) {
              if (r != nullptr)
                n.shapes.push_back({l_order, *r});
            }
          }
        }
        nodes.push_back(n);
      }
    }

    // 2. IO pins
    if (net->get_io_pins() != nullptr) {
      for (idb::IdbPin* pin : net->get_io_pins()->get_pin_list()) {
        if (pin == nullptr)
          continue;
        GraphNode n;
        n.id = nodes.size();
        n.available_time = 0;
        n.is_pin = true;
        for (idb::IdbLayerShape* shape : pin->get_port_box_list()) {
          if (shape != nullptr && shape->get_layer() != nullptr) {
            int l_order = shape->get_layer()->get_order();
            if (l_order < 0 || l_order > _max_layer_order) {
              ZHLOG.warn(Loc::current(), "Skipping IO pin shape with suspicious layer_order=", l_order, " net=", net_name);
              continue;
            }
            for (idb::IdbRect* r : shape->get_rect_list()) {
              if (r != nullptr)
                n.shapes.push_back({l_order, *r});
            }
          }
        }
        nodes.push_back(n);
      }
    }

    // 3. Routing Wires and Vias
    if (net->get_wire_list() != nullptr) {
      for (idb::IdbRegularWire* wire : net->get_wire_list()->get_wire_list()) {
        if (wire == nullptr)
          continue;
        for (idb::IdbRegularWireSegment* seg : wire->get_segment_list()) {
          if (seg == nullptr)
            continue;
          if (seg->is_wire()) {
            idb::IdbLayer* layer = seg->get_layer();
            if (layer != nullptr && layer->is_routing()) {
              idb::IdbLayerRouting* routing = dynamic_cast<idb::IdbLayerRouting*>(layer);
              GraphNode n;
              n.id = nodes.size();
              n.available_time = layer->get_order();
              if (n.available_time < 0 || n.available_time > _max_layer_order) {
                ZHLOG.warn(Loc::current(), "Skipping wire segment with suspicious layer_order=", n.available_time, " net=", net_name);
                continue;
              }
              n.is_routing = true;

              idb::IdbRect rect = seg->get_segment_rect();
              double w_um = getRectMicrons(rect.get_width());
              double h_um = getRectMicrons(rect.get_height());
              n.metal_area = w_um * h_um;
              if (routing != nullptr) {
                double t_um = getRectMicrons(routing->get_thickness());
                double perimeter_um = 2.0 * (w_um + h_um);
                n.side_area = perimeter_um * t_um;
              }
              n.shapes.push_back({layer->get_order(), rect});
              nodes.push_back(n);
            }
          }
          if (seg->is_via()) {
            for (idb::IdbVia* via : seg->get_via_list()) {
              if (via == nullptr)
                continue;

              int b_id = -1, c_id = -1, t_id = -1;

              idb::IdbLayerShape bottom_shape = via->get_bottom_layer_shape();
              idb::IdbLayer* bottom_layer = bottom_shape.get_layer();
              if (bottom_layer != nullptr && bottom_layer->is_routing()) {
                GraphNode n;
                n.id = nodes.size();
                n.available_time = bottom_layer->get_order();
                if (n.available_time < 0 || n.available_time > _max_layer_order) {
                  ZHLOG.warn(Loc::current(), "Skipping via bottom shape with suspicious layer_order=", n.available_time, " net=", net_name);
                  continue;
                }
                n.is_routing = true;
                for (idb::IdbRect* r : bottom_shape.get_rect_list()) {
                  if (r != nullptr)
                    n.shapes.push_back({bottom_layer->get_order(), *r});
                }
                b_id = n.id;
                nodes.push_back(n);
              }

              idb::IdbLayerShape cut_shape = via->get_cut_layer_shape();
              idb::IdbLayer* cut_layer = cut_shape.get_layer();
              if (cut_layer != nullptr && cut_layer->is_cut()) {
                GraphNode n;
                n.id = nodes.size();
                n.available_time = cut_layer->get_order();
                if (n.available_time < 0 || n.available_time > _max_layer_order) {
                  ZHLOG.warn(Loc::current(), "Skipping via cut shape with suspicious layer_order=", n.available_time, " net=", net_name);
                  continue;
                }
                n.is_cut = true;
                for (idb::IdbRect* r : cut_shape.get_rect_list()) {
                  if (r == nullptr)
                    continue;
                  double w_um = getRectMicrons(r->get_width());
                  double h_um = getRectMicrons(r->get_height());
                  n.cut_area += w_um * h_um;
                  n.shapes.push_back({cut_layer->get_order(), *r});
                }
                c_id = n.id;
                nodes.push_back(n);
              }

              idb::IdbLayerShape top_shape = via->get_top_layer_shape();
              idb::IdbLayer* top_layer = top_shape.get_layer();
              if (top_layer != nullptr && top_layer->is_routing()) {
                GraphNode n;
                n.id = nodes.size();
                n.available_time = top_layer->get_order();
                if (n.available_time < 0 || n.available_time > _max_layer_order) {
                  ZHLOG.warn(Loc::current(), "Skipping via top shape with suspicious layer_order=", n.available_time, " net=", net_name);
                  continue;
                }
                n.is_routing = true;
                for (idb::IdbRect* r : top_shape.get_rect_list()) {
                  if (r != nullptr)
                    n.shapes.push_back({top_layer->get_order(), *r});
                }
                t_id = n.id;
                nodes.push_back(n);
              }

              if (b_id != -1 && c_id != -1)
                all_edges.push_back({b_id, c_id});
              if (c_id != -1 && t_id != -1)
                all_edges.push_back({c_id, t_id});
              if (b_id != -1 && t_id != -1 && c_id == -1)
                all_edges.push_back({b_id, t_id});
            }
          }
          if (!seg->is_wire() && !seg->is_via()) {
            _skipped_segments.fetch_add(1, std::memory_order_relaxed);
          }
        }
      }
    }

    int max_time = 0;
    int max_shape_layer = 0;
    for (const auto& n : nodes) {
      max_time = std::max(max_time, n.available_time);
      for (const auto& s : n.shapes) {
        max_shape_layer = std::max(max_shape_layer, s.layer_order);
      }
    }
    int max_layer_idx = std::max(max_time, max_shape_layer);

    std::vector<std::vector<std::pair<ieda_solver::BgRect, int>>> items_by_layer(max_layer_idx + 1);
    for (size_t i = 0; i < nodes.size(); i++) {
      for (auto s : nodes[i].shapes) {
        ieda_solver::BgPoint p_min(s.rect.get_low_x(), s.rect.get_low_y());
        ieda_solver::BgPoint p_max(s.rect.get_high_x(), s.rect.get_high_y());
        ieda_solver::BgRect b_rect(p_min, p_max);
        items_by_layer[s.layer_order].emplace_back(b_rect, i);
      }
    }

    constexpr size_t kBruteForceThreshold = 8;
    std::vector<std::optional<ieda_solver::bg::index::rtree<std::pair<ieda_solver::BgRect, int>, ieda_solver::bg::index::quadratic<16>>>> rtrees(max_layer_idx
                                                                                                                                                 + 1);

    for (int layer = 0; layer <= max_layer_idx; layer++) {
      auto& items = items_by_layer[layer];
      if (items.empty())
        continue;

      if (items.size() < kBruteForceThreshold) {
        for (size_t a = 0; a < items.size(); a++) {
          for (size_t b = a + 1; b < items.size(); b++) {
            if (items[a].second != items[b].second && ieda_solver::bg::intersects(items[a].first, items[b].first)) {
              all_edges.push_back({items[a].second, items[b].second});
            }
          }
        }
      } else {
        rtrees[layer].emplace(items.begin(), items.end());
      }
    }

    for (size_t i = 0; i < nodes.size(); i++) {
      for (auto s : nodes[i].shapes) {
        if (items_by_layer[s.layer_order].size() < kBruteForceThreshold)
          continue;

        ieda_solver::BgPoint p_min(s.rect.get_low_x(), s.rect.get_low_y());
        ieda_solver::BgPoint p_max(s.rect.get_high_x(), s.rect.get_high_y());
        ieda_solver::BgRect b_rect(p_min, p_max);

        std::vector<std::pair<ieda_solver::BgRect, int>> results;
        if (rtrees[s.layer_order].has_value()) {
          rtrees[s.layer_order]->query(ieda_solver::bg::index::intersects(b_rect), std::back_inserter(results));
        }

        for (const auto& res : results) {
          if (res.second > i) {
            all_edges.push_back({i, res.second});
          }
        }
      }
    }

    std::sort(all_edges.begin(), all_edges.end());
    all_edges.erase(std::unique(all_edges.begin(), all_edges.end()), all_edges.end());

    std::vector<std::vector<std::pair<int, int>>> edges_by_time(max_time + 1);
    for (const auto& e : all_edges) {
      int t = std::max(nodes[e.first].available_time, nodes[e.second].available_time);
      edges_by_time[t].push_back(e);
    }

    std::vector<std::vector<int>> nodes_by_time(max_time + 1);
    for (size_t i = 0; i < nodes.size(); i++) {
      nodes_by_time[nodes[i].available_time].push_back(i);
    }

    UnionFind uf(nodes.size(), nodes);

    struct ThresholdPick
    {
      bool available = false;
      double threshold = 0.0;
      bool is_diff = false;
    };

    auto pick = [&](double plain_ratio, double diff_ratio, const std::vector<std::pair<double, double>>& diff_pwl, double comp_diff) -> ThresholdPick {
      const bool has_diff = diff_ratio >= 0 || !diff_pwl.empty();
      const bool has_plain = plain_ratio >= 0;
      const bool use_diff = has_diff && (comp_diff > 1e-10 || !has_plain);
      if (use_diff) {
        double threshold = !diff_pwl.empty() ? evalPwl(diff_pwl, comp_diff) : diff_ratio;
        return {true, threshold, true};
      }
      if (has_plain) {
        return {true, plain_ratio, false};
      }
      return {};
    };

    for (int T = 0; T <= max_time; T++) {
      if (edges_by_time[T].empty() && nodes_by_time[T].empty())
        continue;

      for (auto& edge : edges_by_time[T]) {
        uf.merge(edge.first, edge.second);
      }

      std::map<int, std::vector<int>> comps;
      for (int u : nodes_by_time[T]) {
        comps[uf.find(u)].push_back(u);
      }

      const AntennaRule* r = nullptr;
      if (T >= 0 && static_cast<size_t>(T) < _rule_map.size()) {
        r = _rule_map[T];
      }

      for (auto& [root, comp_nodes] : comps) {
        UFNode& root_data = uf.d[root];

        double par_metal = 0, par_side = 0, par_cut = 0;
        bool has_routing = false, has_cut = false;
        double v_lx = 0.0, v_ly = 0.0, v_hx = 0.0, v_hy = 0.0;
        bool has_coord = false;

        for (int u : comp_nodes) {
          if (nodes[u].available_time == T) {
            if (!has_coord && !nodes[u].shapes.empty()) {
              v_lx = getRectMicrons(nodes[u].shapes[0].rect.get_low_x());
              v_ly = getRectMicrons(nodes[u].shapes[0].rect.get_low_y());
              v_hx = getRectMicrons(nodes[u].shapes[0].rect.get_high_x());
              v_hy = getRectMicrons(nodes[u].shapes[0].rect.get_high_y());
              has_coord = true;
            }
            if (nodes[u].is_routing) {
              par_metal += nodes[u].metal_area;
              par_side += nodes[u].side_area;
              has_routing = true;
            }
            if (nodes[u].is_cut) {
              par_cut += nodes[u].cut_area;
              has_cut = true;
            }
          }
        }

        if (par_metal < 1e-10 && par_side < 1e-10 && par_cut < 1e-10)
          continue;

        root_data.cum_metal += par_metal;
        root_data.cum_side += par_side;
        root_data.cum_cut += par_cut;

        if (r == nullptr)
          continue;

        const double eff_gate = root_data.gate_area + (r->gate_plus_diff >= 0 ? r->gate_plus_diff * root_data.diff_area : 0.0);
        if (eff_gate <= 1e-10) {
          _comps_without_gate.fetch_add(1, std::memory_order_relaxed);
          continue;
        }

        auto factor = [&](double f, bool diffuse_only) { return (f >= 0 && (!diffuse_only || root_data.diff_area > 1e-10)) ? f : 1.0; };
        const double area_scale = factor(r->area_factor, r->area_factor_diffuse_only);
        const double side_scale = factor(r->side_area_factor, r->side_area_factor_diffuse_only);
        const double cut_scale = factor(r->cut_area_factor, r->cut_area_factor_diffuse_only);

        const double diff_sub = (r->area_minus_diff >= 0) ? r->area_minus_diff * root_data.diff_area : 0.0;
        auto ratio_of = [&](double area, double sc) { return std::max(0.0, area * sc - diff_sub) / eff_gate; };

        const double par_area = ratio_of(par_metal, area_scale);
        const double par_side_val = par_side * side_scale / eff_gate;
        const double par_cut_val = ratio_of(par_cut, cut_scale);

        double car_num = std::max(0.0, root_data.cum_metal * area_scale - diff_sub);
        if (r->cum_routing_plus_cut) {
          car_num += std::max(0.0, root_data.cum_cut * cut_scale - diff_sub);
        }
        const double car_area = car_num / eff_gate;
        const double car_side_val = root_data.cum_side * side_scale / eff_gate;
        const double car_cut_val = ratio_of(root_data.cum_cut, cut_scale);

        auto emit = [&](const std::string& layer_name, ViolationType type, double ratio, double threshold) {
          if (ratio > threshold) {
            Violation v;
            v.net_name = net_name;
            v.layer_name = layer_name;
            v.type = type;
            v.ratio = ratio;
            v.threshold = threshold;
            if (has_coord) {
              v.lx = v_lx;
              v.ly = v_ly;
              v.hx = v_hx;
              v.hy = v_hy;
            }
            out_violations.push_back(v);
          }
        };

        if (has_routing) {
          ThresholdPick p = pick(r->area_ratio, r->diff_area_ratio, r->diff_area_ratio_pwl, root_data.diff_area);
          if (p.available)
            emit(r->layer_name, p.is_diff ? ViolationType::kAntennaDiffPar : ViolationType::kAntennaPar, par_area, p.threshold);

          p = pick(r->cum_area_ratio, r->cum_diff_area_ratio, r->cum_diff_area_ratio_pwl, root_data.diff_area);
          if (p.available)
            emit(r->layer_name, p.is_diff ? ViolationType::kAntennaDiffCar : ViolationType::kAntennaCar, car_area, p.threshold);

          p = pick(r->side_area_ratio, r->diff_side_area_ratio, r->diff_side_area_ratio_pwl, root_data.diff_area);
          if (p.available)
            emit(r->layer_name, p.is_diff ? ViolationType::kAntennaDiffPsr : ViolationType::kAntennaPsr, par_side_val, p.threshold);

          p = pick(r->cum_side_area_ratio, r->cum_diff_side_area_ratio, r->cum_diff_side_area_ratio_pwl, root_data.diff_area);
          if (p.available)
            emit(r->layer_name, p.is_diff ? ViolationType::kAntennaDiffCsr : ViolationType::kAntennaCsr, car_side_val, p.threshold);
        }

        if (has_cut) {
          ThresholdPick p = pick(r->area_ratio, r->diff_area_ratio, r->diff_area_ratio_pwl, root_data.diff_area);
          if (p.available)
            emit(r->layer_name, ViolationType::kAntennaCutPar, par_cut_val, p.threshold);

          p = pick(r->cum_area_ratio, r->cum_diff_area_ratio, r->cum_diff_area_ratio_pwl, root_data.diff_area);
          if (p.available)
            emit(r->layer_name, ViolationType::kAntennaCutCar, car_cut_val, p.threshold);
        }
      }
    }
  }

  idb::IdbDesign* _design = nullptr;
  int _micron_dbu = 1000;
  std::vector<AntennaRule> _rules;
  std::vector<const AntennaRule*> _rule_map;
  std::vector<Violation> _violations;

  int _max_layer_order = 200;
  std::atomic<int64_t> _pins_missing_antenna_info{0};
  std::atomic<int64_t> _comps_without_gate{0};
  std::atomic<int64_t> _skipped_segments{0};
};

}  // anonymous namespace

// public

void AntennaChecker::initInst()
{
  if (_ac_instance == nullptr) {
    _ac_instance = new AntennaChecker();
  }
}

AntennaChecker& AntennaChecker::getInst()
{
  if (_ac_instance == nullptr) {
    ZHLOG.error(Loc::current(), "The instance not initialized!");
    abort();
  }
  return *_ac_instance;
}

void AntennaChecker::destroyInst()
{
  if (_ac_instance != nullptr) {
    delete _ac_instance;
    _ac_instance = nullptr;
  }
}

// function

void AntennaChecker::check(std::map<std::string, std::any> config_map)
{
  Monitor monitor;
  ZHLOG.info(Loc::current(), "Starting...");

  ACModel ac_model = initACModel(config_map);
  _violation_num = ac_model.get_violation_num();

  ZHLOG.info(Loc::current(), "ZH checkAntenna");
  ZHLOG.info(Loc::current(), "Found ", ac_model.get_violation_num(), " antenna violations");

  ZHLOG.info(Loc::current(), "Completed", monitor.getStatsInfo());
}

// private

ACModel AntennaChecker::initACModel(std::map<std::string, std::any>& config_map)
{
  std::string report_dir = "";
  if (config_map.find("report_dir") != config_map.end()) {
    report_dir = std::any_cast<std::string>(config_map["report_dir"]);
    config_map.erase("report_dir");
  }

  ACModel ac_model;
  if (!config_map.empty()) {
    ZHLOG.warn(Loc::current(), "The checkAntenna config has not been consumed yet!");
  }

  auto* idb_design = dmInst->get_idb_def_service()->get_design();
  AntennaCheckerImpl checker(idb_design);
  checker.run();

  ZHLOG.info(Loc::current(), "violation count: ", checker.get_violation_count());

  auto type_to_string = [](ViolationType t) {
    switch (t) {
      case ViolationType::kAntennaPar:
        return "PAR";
      case ViolationType::kAntennaDiffPar:
        return "DiffPAR";
      case ViolationType::kAntennaCar:
        return "CAR";
      case ViolationType::kAntennaDiffCar:
        return "DiffCAR";
      case ViolationType::kAntennaPsr:
        return "PSR";
      case ViolationType::kAntennaDiffPsr:
        return "DiffPSR";
      case ViolationType::kAntennaCsr:
        return "CSR";
      case ViolationType::kAntennaDiffCsr:
        return "DiffCSR";
      case ViolationType::kAntennaCutPar:
        return "CutPAR";
      case ViolationType::kAntennaCutCar:
        return "CutCAR";
      default:
        return "UNKNOWN";
    }
  };

  if (!report_dir.empty()) {
    std::ofstream rpt(report_dir + "/antenna_check.rpt");
    if (rpt.is_open()) {
      rpt << "========================================================================\n";
      rpt << "                      Antenna Violations Report                         \n";
      rpt << "========================================================================\n";
      rpt << "Total Violations: " << checker.get_violation_count() << "\n\n";
      if (checker.get_violation_count() > 0) {
        rpt << "Details:\n";
        rpt << "------------------------------------------------------------------------\n";
        for (const auto& v : checker.get_violations()) {
          rpt << "Net: " << v.net_name << " | Layer: " << v.layer_name << " | Type: " << type_to_string(v.type) << "\n  Ratio: " << v.ratio
              << " (Threshold: " << v.threshold << ")"
              << "\n  Location: (" << v.lx << ", " << v.ly << ") to (" << v.hx << ", " << v.hy << ")\n\n";
        }
      }
      rpt.close();
    }
  }

  ac_model.set_violation_num(checker.get_violation_count());

  return ac_model;
}

AntennaChecker* AntennaChecker::_ac_instance = nullptr;

}  // namespace izh
