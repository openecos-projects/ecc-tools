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
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <limits>
#include <map>
#include <optional>
#include <string>
#include <thread>
#include <tuple>
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

constexpr double kEps = 1e-12;

double evalPwl(const std::vector<std::pair<double, double>>& pwl, double x, double default_value = 0.0)
{
  if (pwl.empty()) {
    return default_value;
  }

  if (x <= pwl.front().first) {
    return pwl.front().second;
  }

  if (x >= pwl.back().first) {
    return pwl.back().second;
  }

  for (size_t i = 0; i + 1 < pwl.size(); ++i) {
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

struct ThresholdPick
{
  bool available = false;
  double threshold = 0.0;
  bool is_diff = false;
};

ThresholdPick pickThreshold(double plain_ratio, double diff_ratio, const std::vector<std::pair<double, double>>& diff_pwl, double diff_area,
                            bool diff_connected)
{
  const bool has_diff = (diff_ratio >= 0.0) || (!diff_pwl.empty());
  const bool has_plain = (plain_ratio >= 0.0);

  const bool diff_active = diff_connected || (diff_area > kEps);
  const bool use_diff = has_diff && (diff_active || !has_plain);

  if (use_diff) {
    double threshold = !diff_pwl.empty() ? evalPwl(diff_pwl, diff_area, diff_ratio) : diff_ratio;
    return {true, threshold, true};
  }

  if (has_plain) {
    return {true, plain_ratio, false};
  }

  return {};
}

struct Shape
{
  int layer_order = -1;
  idb::IdbRect rect;
};

struct GraphNode
{
  int id = -1;
  int time = 0;

  bool is_conductor = false;
  bool is_routing = false;
  bool is_cut = false;

  double gate_area = 0.0;
  double diff_area = 0.0;
  bool provides_diff = false;

  std::vector<Shape> shapes;
};

struct UFNode
{
  int parent = -1;
  int rank = 0;

  double gate_area = 0.0;
  double diff_area = 0.0;
  bool diff_connected = false;

  // Worst-case cumulative ratios (CAR/CSR).
  double worst_car_area = 0.0;
  double worst_car_side = 0.0;
  double worst_car_cut = 0.0;
};

struct UnionFind
{
  std::vector<UFNode> d;

  UnionFind(int n, const std::vector<GraphNode>& nodes)
  {
    d.resize(n);

    for (int i = 0; i < n; ++i) {
      d[i].parent = i;
      d[i].rank = 0;

      d[i].gate_area = nodes[i].gate_area;
      d[i].diff_area = nodes[i].diff_area;
      d[i].diff_connected = nodes[i].provides_diff || (nodes[i].diff_area > kEps);

      d[i].worst_car_area = 0.0;
      d[i].worst_car_side = 0.0;
      d[i].worst_car_cut = 0.0;
    }
  }

  int find(int i)
  {
    if (d[i].parent == i) {
      return i;
    }

    d[i].parent = find(d[i].parent);
    return d[i].parent;
  }

  void merge(int i, int j)
  {
    int ri = find(i);
    int rj = find(j);

    if (ri == rj) {
      return;
    }

    if (d[ri].rank < d[rj].rank) {
      std::swap(ri, rj);
    }

    d[rj].parent = ri;

    if (d[ri].rank == d[rj].rank) {
      ++d[ri].rank;
    }

    d[ri].gate_area += d[rj].gate_area;
    d[ri].diff_area += d[rj].diff_area;
    d[ri].diff_connected = d[ri].diff_connected || d[rj].diff_connected;

    d[ri].worst_car_area = std::max(d[ri].worst_car_area, d[rj].worst_car_area);
    d[ri].worst_car_side = std::max(d[ri].worst_car_side, d[rj].worst_car_side);
    d[ri].worst_car_cut = std::max(d[ri].worst_car_cut, d[rj].worst_car_cut);
  }
};

struct Edge
{
  int a = -1;
  int b = -1;
  int time = 0;
};

struct AntennaRule
{
  std::string layer_name;
  int layer_order = -1;
  bool is_routing = true;

  double thickness_um = 0.0;

  double area_ratio = -1.0;
  double cum_area_ratio = -1.0;

  double area_factor = -1.0;
  bool area_factor_diffuse_only = false;

  double side_area_ratio = -1.0;
  double cum_side_area_ratio = -1.0;

  double side_area_factor = -1.0;
  bool side_area_factor_diffuse_only = false;

  double gate_plus_diff = -1.0;
  double area_minus_diff = -1.0;

  double diff_area_ratio = -1.0;
  double cum_diff_area_ratio = -1.0;

  double diff_side_area_ratio = -1.0;
  double cum_diff_side_area_ratio = -1.0;

  std::vector<std::pair<double, double>> diff_area_ratio_pwl;
  std::vector<std::pair<double, double>> cum_diff_area_ratio_pwl;

  std::vector<std::pair<double, double>> diff_side_area_ratio_pwl;
  std::vector<std::pair<double, double>> cum_diff_side_area_ratio_pwl;

  std::vector<std::pair<double, double>> area_diff_reduce_pwl;
  std::vector<std::pair<double, double>> side_area_diff_reduce_pwl;

  bool cum_routing_plus_cut = false;

  double cut_area_factor = -1.0;
  bool cut_area_factor_diffuse_only = false;
};

class SegmentTree
{
 public:
  void init(const std::vector<int64_t>& y_coords)
  {
    ys = y_coords;
    n = static_cast<int>(ys.size()) - 1;
    st.assign(static_cast<size_t>(4) * std::max(1, n), Node());
  }

  long double coveredLength() const
  {
    if (n <= 0) {
      return 0.0L;
    }
    return st[1].len;
  }

  int intervalCount() const
  {
    if (n <= 0) {
      return 0;
    }
    return st[1].num;
  }

  void update(int l, int r, int delta)
  {
    if (n <= 0 || l > r) {
      return;
    }
    updateImpl(1, 0, n - 1, l, r, delta);
  }

  long double queryCovered(int l, int r) const
  {
    if (n <= 0 || l > r) {
      return 0.0L;
    }
    return queryCoveredImpl(1, 0, n - 1, l, r);
  }

 private:
  struct Node
  {
    int cover = 0;
    long double len = 0.0L;
    int num = 0;
    bool lcov = false;
    bool rcov = false;
  };

  std::vector<int64_t> ys;
  int n = 0;
  std::vector<Node> st;

  long double segLen(int l, int r) const { return static_cast<long double>(ys[r + 1] - ys[l]); }

  void pull(int p, int l, int r)
  {
    if (st[p].cover > 0) {
      st[p].len = segLen(l, r);
      st[p].num = 1;
      st[p].lcov = true;
      st[p].rcov = true;
      return;
    }

    if (l == r) {
      st[p].len = 0.0L;
      st[p].num = 0;
      st[p].lcov = false;
      st[p].rcov = false;
      return;
    }

    int lc = p * 2;
    int rc = lc + 1;

    st[p].len = st[lc].len + st[rc].len;
    st[p].num = st[lc].num + st[rc].num - ((st[lc].rcov && st[rc].lcov) ? 1 : 0);
    st[p].lcov = st[lc].lcov;
    st[p].rcov = st[rc].rcov;
  }

  void updateImpl(int p, int l, int r, int ql, int qr, int delta)
  {
    if (qr < l || r < ql) {
      return;
    }

    if (ql <= l && r <= qr) {
      st[p].cover += delta;
      if (st[p].cover < 0) {
        st[p].cover = 0;
      }
      pull(p, l, r);
      return;
    }

    int m = (l + r) / 2;
    updateImpl(p * 2, l, m, ql, qr, delta);
    updateImpl(p * 2 + 1, m + 1, r, ql, qr, delta);
    pull(p, l, r);
  }

  long double queryCoveredImpl(int p, int l, int r, int ql, int qr) const
  {
    if (qr < l || r < ql) {
      return 0.0L;
    }

    if (ql <= l && r <= qr) {
      return st[p].len;
    }

    if (st[p].cover > 0) {
      int L = std::max(l, ql);
      int R = std::min(r, qr);

      if (L > R) {
        return 0.0L;
      }

      return static_cast<long double>(ys[R + 1] - ys[L]);
    }

    if (l == r) {
      return 0.0L;
    }

    int m = (l + r) / 2;
    return queryCoveredImpl(p * 2, l, m, ql, qr) + queryCoveredImpl(p * 2 + 1, m + 1, r, ql, qr);
  }
};

void unionAreaPerimeter(const std::vector<idb::IdbRect>& rects, int micron_dbu, double& area_um, double& perimeter_um)
{
  area_um = 0.0;
  perimeter_um = 0.0;

  if (rects.empty() || micron_dbu <= 0) {
    return;
  }

  struct NormRect
  {
    int64_t x1 = 0;
    int64_t x2 = 0;
    int64_t y1 = 0;
    int64_t y2 = 0;
  };

  std::vector<NormRect> rs;
  std::vector<int64_t> ys;

  rs.reserve(rects.size());
  ys.reserve(rects.size() * 2);

  for (const auto& r : rects) {
    int64_t x1 = std::min(r.get_low_x(), r.get_high_x());
    int64_t x2 = std::max(r.get_low_x(), r.get_high_x());
    int64_t y1 = std::min(r.get_low_y(), r.get_high_y());
    int64_t y2 = std::max(r.get_low_y(), r.get_high_y());

    if (x2 <= x1 || y2 <= y1) {
      continue;
    }

    rs.push_back({x1, x2, y1, y2});
    ys.push_back(y1);
    ys.push_back(y2);
  }

  if (rs.empty()) {
    return;
  }

  const long double dbu = static_cast<long double>(micron_dbu);

  auto finalize = [&](long double area_dbu, long double perimeter_dbu) {
    if (area_dbu < 0.0L) {
      area_dbu = 0.0L;
    }
    if (perimeter_dbu < 0.0L) {
      perimeter_dbu = 0.0L;
    }
    area_um = static_cast<double>(area_dbu / (dbu * dbu));
    perimeter_um = static_cast<double>(perimeter_dbu / dbu);
  };

  if (rs.size() == 1) {
    long double w = static_cast<long double>(rs[0].x2 - rs[0].x1);
    long double h = static_cast<long double>(rs[0].y2 - rs[0].y1);
    finalize(w * h, 2.0L * (w + h));
    return;
  }

  if (rs.size() <= 64) {
    bool need_union = false;

    for (size_t i = 0; i < rs.size() && !need_union; ++i) {
      for (size_t j = i + 1; j < rs.size(); ++j) {
        if (!(rs[i].x2 < rs[j].x1 || rs[j].x2 < rs[i].x1 || rs[i].y2 < rs[j].y1 || rs[j].y2 < rs[i].y1)) {
          need_union = true;
          break;
        }
      }
    }

    if (!need_union) {
      long double area = 0.0L;
      long double per = 0.0L;

      for (const auto& r : rs) {
        long double w = static_cast<long double>(r.x2 - r.x1);
        long double h = static_cast<long double>(r.y2 - r.y1);
        area += w * h;
        per += 2.0L * (w + h);
      }

      finalize(area, per);
      return;
    }
  }

  std::sort(ys.begin(), ys.end());
  ys.erase(std::unique(ys.begin(), ys.end()), ys.end());

  const int y_interval_count = static_cast<int>(ys.size()) - 1;
  if (y_interval_count <= 0) {
    return;
  }

  struct Event
  {
    int64_t x = 0;
    int y1 = 0;
    int y2 = 0;
    int delta = 0;
  };

  std::vector<Event> events;
  events.reserve(rs.size() * 2);

  auto yIndex = [&](int64_t v) -> int {
    return static_cast<int>(std::lower_bound(ys.begin(), ys.end(), v) - ys.begin());
  };

  for (const auto& r : rs) {
    int y1 = yIndex(r.y1);
    int y2 = yIndex(r.y2) - 1;

    if (y1 > y2) {
      continue;
    }

    events.push_back({r.x1, y1, y2, +1});
    events.push_back({r.x2, y1, y2, -1});
  }

  if (events.empty()) {
    return;
  }

  std::sort(events.begin(), events.end(), [](const Event& a, const Event& b) {
    return a.x < b.x;
  });

  SegmentTree tree;
  tree.init(ys);

  long double area = 0.0L;
  long double perimeter = 0.0L;

  bool has_prev_x = false;
  int64_t prev_x = 0;

  std::vector<std::pair<int, int>> left_ranges;
  std::vector<std::pair<int, int>> right_ranges;

  auto addVerticalContribution = [&](std::vector<std::pair<int, int>>& ranges) {
    if (ranges.empty()) {
      return;
    }

    std::sort(ranges.begin(), ranges.end());

    size_t i = 0;
    while (i < ranges.size()) {
      int a = ranges[i].first;
      int b = ranges[i].second;

      size_t j = i + 1;
      while (j < ranges.size() && ranges[j].first <= b + 1) {
        b = std::max(b, ranges[j].second);
        ++j;
      }

      long double range_len = static_cast<long double>(ys[b + 1] - ys[a]);
      long double covered = tree.queryCovered(a, b);
      long double uncovered = range_len - covered;

      if (uncovered > 0.0L) {
        perimeter += uncovered;
      }

      i = j;
    }
  };

  size_t i = 0;
  while (i < events.size()) {
    int64_t x = events[i].x;

    if (has_prev_x) {
      long double dx = static_cast<long double>(x - prev_x);
      if (dx > 0.0L) {
        area += tree.coveredLength() * dx;
        perimeter += 2.0L * static_cast<long double>(tree.intervalCount()) * dx;
      }
    }

    size_t j = i;
    left_ranges.clear();
    right_ranges.clear();

    while (j < events.size() && events[j].x == x) {
      if (events[j].delta > 0) {
        left_ranges.emplace_back(events[j].y1, events[j].y2);
      } else {
        right_ranges.emplace_back(events[j].y1, events[j].y2);
      }
      ++j;
    }

    addVerticalContribution(left_ranges);

    for (size_t k = i; k < j; ++k) {
      tree.update(events[k].y1, events[k].y2, events[k].delta);
    }

    addVerticalContribution(right_ranges);

    has_prev_x = true;
    prev_x = x;
    i = j;
  }

  finalize(area, perimeter);
}

class AntennaCheckerImpl
{
 public:
  explicit AntennaCheckerImpl(idb::IdbDesign* design) : _design(design)
  {
    if (_design && _design->get_layout() && _design->get_layout()->get_units()) {
      int dbu = _design->get_layout()->get_units()->get_micron_dbu();
      if (dbu > 0) {
        _micron_dbu = dbu;
      }
    }
  }

  void run()
  {
    _violations.clear();

    if (!_design || !_design->get_layout()) {
      return;
    }

    initLayers();

    if (_rules.empty()) {
      ZHLOG.info(Loc::current(), "No antenna rules defined in technology; skipping antenna check");
      return;
    }

    idb::IdbNetList* net_list = _design->get_net_list();
    if (!net_list) {
      return;
    }

    std::vector<idb::IdbNet*> signal_nets;
    for (idb::IdbNet* net : net_list->get_net_list()) {
      if (net && net->is_signal()) {
        signal_nets.push_back(net);
      }
    }

    const int nthreads = std::max(1u, std::thread::hardware_concurrency());
    std::vector<std::vector<Violation>> local_violations(nthreads);

#pragma omp parallel for schedule(dynamic, 16) num_threads(nthreads)
    for (long long i = 0; i < static_cast<long long>(signal_nets.size()); ++i) {
      int tid = omp_get_thread_num();
      checkNet(signal_nets[static_cast<size_t>(i)], local_violations[tid]);
    }

    for (auto& v : local_violations) {
      _violations.insert(_violations.end(), std::make_move_iterator(v.begin()), std::make_move_iterator(v.end()));
    }

    std::sort(_violations.begin(), _violations.end(), [](const Violation& a, const Violation& b) {
      return std::tie(a.net_name, a.layer_name, a.type, a.ratio, a.threshold, a.lx, a.ly, a.hx, a.hy) <
             std::tie(b.net_name, b.layer_name, b.type, b.ratio, b.threshold, b.lx, b.ly, b.hx, b.hy);
    });

    if (_pins_missing_antenna_info.load() > 0) {
      ZHLOG.warn(Loc::current(), _pins_missing_antenna_info.load(),
                 " instance pins have no antenna gate/diffusion area annotation; affected nets may be under-checked");
    }

    if (_comps_without_gate.load() > 0) {
      ZHLOG.info(Loc::current(), _comps_without_gate.load(),
                 " conductor components had no connected gate state at their layer event (ignored for damage accumulation)");
    }

    if (_skipped_segments.load() > 0) {
      ZHLOG.warn(Loc::current(), _skipped_segments.load(), " segments were neither wire nor via and were skipped");
    }
  }

  int get_violation_count() const { return static_cast<int>(_violations.size()); }
  const std::vector<Violation>& get_violations() const { return _violations; }

 private:
  double getMicrons(int64_t dbu_value) const
  {
    if (_micron_dbu <= 0) {
      return 0.0;
    }
    return static_cast<double>(dbu_value) / static_cast<double>(_micron_dbu);
  }

  const AntennaRule* pickRule(int layer_order, bool routing) const
  {
    if (layer_order < 0 || static_cast<size_t>(layer_order) >= _rule_map.size()) {
      return nullptr;
    }

    for (const auto* rule : _rule_map[layer_order]) {
      if (rule && rule->is_routing == routing) {
        return rule;
      }
    }

    return nullptr;
  }

  void initLayers()
  {
    if (!_design || !_design->get_layout()) {
      return;
    }

    idb::IdbLayers* layers = _design->get_layout()->get_layers();
    if (!layers) {
      return;
    }

    _rules.clear();
    _rule_map.clear();
    _thickness_by_order.clear();

    _max_layer_order = 0;

    for (idb::IdbLayer* layer : layers->get_routing_layers()) {
      if (layer) {
        _max_layer_order = std::max(_max_layer_order, static_cast<int>(layer->get_order()));
      }
    }

    for (idb::IdbLayer* layer : layers->get_cut_layers()) {
      if (layer) {
        _max_layer_order = std::max(_max_layer_order, static_cast<int>(layer->get_order()));
      }
    }

    // Routing layers
    for (idb::IdbLayer* layer : layers->get_routing_layers()) {
      idb::IdbLayerRouting* routing = dynamic_cast<idb::IdbLayerRouting*>(layer);
      if (routing == nullptr) {
        continue;
      }

      bool has_any_rule = false;

      has_any_rule |= routing->has_antenna_area_ratio();
      has_any_rule |= routing->has_antenna_cum_area_ratio();
      has_any_rule |= routing->has_antenna_area_factor();
      has_any_rule |= routing->has_antenna_side_area_ratio();
      has_any_rule |= routing->has_antenna_cum_side_area_ratio();
      has_any_rule |= routing->has_antenna_side_area_factor();
      has_any_rule |= routing->has_antenna_gate_plus_diff();
      has_any_rule |= routing->has_antenna_area_minus_diff();
      has_any_rule |= routing->has_antenna_diff_area_ratio();
      has_any_rule |= routing->has_antenna_cum_diff_area_ratio();
      has_any_rule |= routing->has_antenna_diff_side_area_ratio();
      has_any_rule |= routing->has_antenna_cum_diff_side_area_ratio();
      has_any_rule |= routing->has_antenna_diff_area_ratio_pwl();
      has_any_rule |= routing->has_antenna_cum_diff_area_ratio_pwl();
      has_any_rule |= routing->has_antenna_diff_side_area_ratio_pwl();
      has_any_rule |= routing->has_antenna_cum_diff_side_area_ratio_pwl();
      has_any_rule |= routing->get_antenna_cum_routing_plus_cut();

      if (!has_any_rule) {
        continue;
      }

      AntennaRule rule;
      rule.layer_name = routing->get_name();
      rule.layer_order = static_cast<int>(routing->get_order());
      rule.is_routing = true;
      rule.thickness_um = getMicrons(static_cast<int64_t>(routing->get_thickness()));

      if (routing->has_antenna_area_ratio()) {
        rule.area_ratio = routing->get_antenna_area_ratio();
      }
      if (routing->has_antenna_cum_area_ratio()) {
        rule.cum_area_ratio = routing->get_antenna_cum_area_ratio();
      }
      if (routing->has_antenna_area_factor()) {
        rule.area_factor = routing->get_antenna_area_factor();
      }

      rule.area_factor_diffuse_only = routing->get_antenna_area_factor_diffuse_only();

      if (routing->has_antenna_side_area_ratio()) {
        rule.side_area_ratio = routing->get_antenna_side_area_ratio();
      }
      if (routing->has_antenna_cum_side_area_ratio()) {
        rule.cum_side_area_ratio = routing->get_antenna_cum_side_area_ratio();
      }
      if (routing->has_antenna_side_area_factor()) {
        rule.side_area_factor = routing->get_antenna_side_area_factor();
      }

      rule.side_area_factor_diffuse_only = routing->get_antenna_side_area_factor_diffuse_only();

      if (routing->has_antenna_gate_plus_diff()) {
        rule.gate_plus_diff = routing->get_antenna_gate_plus_diff();
      }
      if (routing->has_antenna_area_minus_diff()) {
        rule.area_minus_diff = routing->get_antenna_area_minus_diff();
      }
      if (routing->has_antenna_diff_area_ratio()) {
        rule.diff_area_ratio = routing->get_antenna_diff_area_ratio();
      }
      if (routing->has_antenna_cum_diff_area_ratio()) {
        rule.cum_diff_area_ratio = routing->get_antenna_cum_diff_area_ratio();
      }
      if (routing->has_antenna_diff_side_area_ratio()) {
        rule.diff_side_area_ratio = routing->get_antenna_diff_side_area_ratio();
      }
      if (routing->has_antenna_cum_diff_side_area_ratio()) {
        rule.cum_diff_side_area_ratio = routing->get_antenna_cum_diff_side_area_ratio();
      }

      rule.cum_routing_plus_cut = routing->get_antenna_cum_routing_plus_cut();

      if (routing->has_antenna_diff_area_ratio_pwl()) {
        rule.diff_area_ratio_pwl = routing->get_antenna_diff_area_ratio_pwl();
      }
      if (routing->has_antenna_cum_diff_area_ratio_pwl()) {
        rule.cum_diff_area_ratio_pwl = routing->get_antenna_cum_diff_area_ratio_pwl();
      }
      if (routing->has_antenna_diff_side_area_ratio_pwl()) {
        rule.diff_side_area_ratio_pwl = routing->get_antenna_diff_side_area_ratio_pwl();
      }
      if (routing->has_antenna_cum_diff_side_area_ratio_pwl()) {
        rule.cum_diff_side_area_ratio_pwl = routing->get_antenna_cum_diff_side_area_ratio_pwl();
      }

      _rules.push_back(rule);
    }

    // Cut layers
    for (idb::IdbLayer* layer : layers->get_cut_layers()) {
      idb::IdbLayerCut* cut = dynamic_cast<idb::IdbLayerCut*>(layer);
      if (cut == nullptr) {
        continue;
      }

      bool has_any_rule = false;

      has_any_rule |= cut->has_antenna_cut_area_factor();
      has_any_rule |= cut->has_antenna_area_ratio();
      has_any_rule |= cut->has_antenna_cum_area_ratio();
      has_any_rule |= cut->has_antenna_diff_area_ratio();
      has_any_rule |= cut->has_antenna_cum_diff_area_ratio();
      has_any_rule |= cut->has_antenna_gate_plus_diff();
      has_any_rule |= cut->has_antenna_area_minus_diff();
      has_any_rule |= cut->has_antenna_diff_area_ratio_pwl();
      has_any_rule |= cut->has_antenna_cum_diff_area_ratio_pwl();
      has_any_rule |= cut->get_antenna_cum_routing_plus_cut();

      if (!has_any_rule) {
        continue;
      }

      AntennaRule rule;
      rule.layer_name = cut->get_name();
      rule.layer_order = static_cast<int>(cut->get_order());
      rule.is_routing = false;

      if (cut->has_antenna_cut_area_factor()) {
        rule.cut_area_factor = cut->get_antenna_cut_area_factor();
      } else {
        rule.cut_area_factor = 1.0;
      }

      rule.cut_area_factor_diffuse_only = cut->get_antenna_cut_area_factor_diffuse_only();

      if (cut->has_antenna_area_ratio()) {
        rule.area_ratio = cut->get_antenna_area_ratio();
      }
      if (cut->has_antenna_cum_area_ratio()) {
        rule.cum_area_ratio = cut->get_antenna_cum_area_ratio();
      }
      if (cut->has_antenna_diff_area_ratio()) {
        rule.diff_area_ratio = cut->get_antenna_diff_area_ratio();
      }
      if (cut->has_antenna_cum_diff_area_ratio()) {
        rule.cum_diff_area_ratio = cut->get_antenna_cum_diff_area_ratio();
      }
      if (cut->has_antenna_gate_plus_diff()) {
        rule.gate_plus_diff = cut->get_antenna_gate_plus_diff();
      }
      if (cut->has_antenna_area_minus_diff()) {
        rule.area_minus_diff = cut->get_antenna_area_minus_diff();
      }

      rule.cum_routing_plus_cut = cut->get_antenna_cum_routing_plus_cut();

      if (cut->has_antenna_diff_area_ratio_pwl()) {
        rule.diff_area_ratio_pwl = cut->get_antenna_diff_area_ratio_pwl();
      }
      if (cut->has_antenna_cum_diff_area_ratio_pwl()) {
        rule.cum_diff_area_ratio_pwl = cut->get_antenna_cum_diff_area_ratio_pwl();
      }

      _rules.push_back(rule);
    }

    if (_max_layer_order < 0) {
      _max_layer_order = 0;
    }

    _rule_map.resize(static_cast<size_t>(_max_layer_order) + 1);
    _thickness_by_order.assign(static_cast<size_t>(_max_layer_order) + 1, 0.0);

    for (const auto& rule : _rules) {
      if (rule.layer_order >= 0 && static_cast<size_t>(rule.layer_order) < _rule_map.size()) {
        _rule_map[rule.layer_order].push_back(&rule);

        if (rule.is_routing && rule.thickness_um > 0.0 && _thickness_by_order[rule.layer_order] <= 0.0) {
          _thickness_by_order[rule.layer_order] = rule.thickness_um;
        }
      }
    }
  }

  void readPinAntennaInfo(idb::IdbPin* pin, bool instance_pin, double& gate_area, double& diff_area, bool& provides_diff)
  {
    gate_area = 0.0;
    diff_area = 0.0;
    provides_diff = false;

    if (pin == nullptr) {
      return;
    }

    idb::IdbTerm* term = pin->get_term();
    if (term == nullptr) {
      if (instance_pin) {
        _pins_missing_antenna_info.fetch_add(1, std::memory_order_relaxed);
      }
      return;
    }

    if (term->has_antenna_gate_area()) {
      gate_area = term->get_antenna_gate_area();
    }

    if (term->has_antenna_diff_area()) {
      diff_area = term->get_antenna_diff_area();
      provides_diff = true;
    }

    if (diff_area > kEps) {
      provides_diff = true;
    }

    idb::IdbConnectDirection dir = term->get_direction();
    if (dir == idb::IdbConnectDirection::kOutput || dir == idb::IdbConnectDirection::kOutputTriState ||
        dir == idb::IdbConnectDirection::kInOut) {
      provides_diff = true;
    }

    if (instance_pin && !term->has_antenna_gate_area() && !term->has_antenna_diff_area()) {
      _pins_missing_antenna_info.fetch_add(1, std::memory_order_relaxed);
    }
  }

  void checkNet(idb::IdbNet* net, std::vector<Violation>& out_violations)
  {
    if (net == nullptr) {
      return;
    }

    const std::string& net_name = net->get_net_name();

    std::vector<GraphNode> nodes;
    std::vector<Edge> all_edges;

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
      if (t > _max_layer_order) {
        return;
      }

      max_time = std::max(max_time, t);
      all_edges.push_back({a, b, t});
    };

    auto createGateNode = [&](double gate_area, double diff_area, bool provides_diff) -> int {
      if (gate_area <= kEps && diff_area <= kEps && !provides_diff) {
        return -1;
      }

      GraphNode n;
      n.id = static_cast<int>(nodes.size());
      n.time = 0;
      n.is_conductor = false;

      n.gate_area = gate_area;
      n.diff_area = diff_area;
      n.provides_diff = provides_diff || (diff_area > kEps);

      nodes.push_back(std::move(n));
      return static_cast<int>(nodes.size()) - 1;
    };

    auto createConductorNode = [&](int order, bool is_routing, bool is_cut, const std::vector<idb::IdbRect>& rects) -> int {
      if (order < 0 || order > _max_layer_order || rects.empty()) {
        return -1;
      }

      GraphNode n;
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

    // 1. Instance pins
    if (net->get_instance_pin_list() != nullptr) {
      for (idb::IdbPin* pin : net->get_instance_pin_list()->get_pin_list()) {
        if (pin == nullptr) {
          continue;
        }

        double gate_area = 0.0;
        double diff_area = 0.0;
        bool provides_diff = false;

        readPinAntennaInfo(pin, true, gate_area, diff_area, provides_diff);
        int gate_node = createGateNode(gate_area, diff_area, provides_diff);

        struct PinShape
        {
          int order = -1;
          bool routing = false;
          bool cut = false;
          idb::IdbRect rect;
        };

        std::vector<PinShape> pin_shapes;

        for (idb::IdbLayerShape* shape : pin->get_port_box_list()) {
          if (shape == nullptr || shape->get_layer() == nullptr) {
            continue;
          }

          idb::IdbLayer* layer = shape->get_layer();
          int order = static_cast<int>(layer->get_order());

          if (order < 0 || order > _max_layer_order) {
            ZHLOG.warn(Loc::current(), "Skipping instance pin shape with suspicious layer_order=", order, " net=", net_name);
            continue;
          }

          for (idb::IdbRect* r : shape->get_rect_list()) {
            if (r != nullptr) {
              pin_shapes.push_back({order, layer->is_routing(), layer->is_cut(), *r});
            }
          }
        }

        if (!pin_shapes.empty()) {
          std::sort(pin_shapes.begin(), pin_shapes.end(), [](const PinShape& a, const PinShape& b) {
            return a.order < b.order;
          });

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

            if (gate_node >= 0 && shape_node >= 0) {
              addEdge(gate_node, shape_node, order);
            }
          }
        }
      }
    }

    // 2. IO pins
    if (net->get_io_pins() != nullptr) {
      for (idb::IdbPin* pin : net->get_io_pins()->get_pin_list()) {
        if (pin == nullptr) {
          continue;
        }

        double gate_area = 0.0;
        double diff_area = 0.0;
        bool provides_diff = false;

        readPinAntennaInfo(pin, false, gate_area, diff_area, provides_diff);
        int gate_node = createGateNode(gate_area, diff_area, provides_diff);

        struct PinShape
        {
          int order = -1;
          bool routing = false;
          bool cut = false;
          idb::IdbRect rect;
        };

        std::vector<PinShape> pin_shapes;

        for (idb::IdbLayerShape* shape : pin->get_port_box_list()) {
          if (shape == nullptr || shape->get_layer() == nullptr) {
            continue;
          }

          idb::IdbLayer* layer = shape->get_layer();
          int order = static_cast<int>(layer->get_order());

          if (order < 0 || order > _max_layer_order) {
            ZHLOG.warn(Loc::current(), "Skipping IO pin shape with suspicious layer_order=", order, " net=", net_name);
            continue;
          }

          for (idb::IdbRect* r : shape->get_rect_list()) {
            if (r != nullptr) {
              pin_shapes.push_back({order, layer->is_routing(), layer->is_cut(), *r});
            }
          }
        }

        if (!pin_shapes.empty()) {
          std::sort(pin_shapes.begin(), pin_shapes.end(), [](const PinShape& a, const PinShape& b) {
            return a.order < b.order;
          });

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

            if (gate_node >= 0 && shape_node >= 0) {
              addEdge(gate_node, shape_node, order);
            }
          }
        }
      }
    }

    // 3. Routing wires and vias
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

            if (layer != nullptr && layer->is_routing()) {
              int order = static_cast<int>(layer->get_order());

              if (order < 0 || order > _max_layer_order) {
                ZHLOG.warn(Loc::current(), "Skipping wire segment with suspicious layer_order=", order, " net=", net_name);
              } else {
                std::vector<idb::IdbRect> rects;
                rects.push_back(seg->get_segment_rect());
                createConductorNode(order, true, false, rects);
              }
            } else {
              _skipped_segments.fetch_add(1, std::memory_order_relaxed);
            }
          }

          if (seg->is_via()) {
            for (idb::IdbVia* via : seg->get_via_list()) {
              if (via == nullptr) {
                continue;
              }

              auto collectShapeRects = [&](idb::IdbLayerShape& shape, int& order, bool& is_routing,
                                           bool& is_cut) -> std::vector<idb::IdbRect> {
                std::vector<idb::IdbRect> rects;
                idb::IdbLayer* layer = shape.get_layer();
                if (layer == nullptr) {
                  return rects;
                }

                order = static_cast<int>(layer->get_order());
                is_routing = layer->is_routing();
                is_cut = layer->is_cut();

                if (order < 0 || order > _max_layer_order) {
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
            _skipped_segments.fetch_add(1, std::memory_order_relaxed);
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

    // 4. Same-layer geometric overlap edges
    std::vector<std::vector<std::pair<ecc_solver::BgRect, int>>> items_by_layer(static_cast<size_t>(max_layer_idx) + 1);

    for (size_t i = 0; i < nodes.size(); ++i) {
      if (!nodes[i].is_conductor) {
        continue;
      }

      for (const auto& s : nodes[i].shapes) {
        if (s.layer_order < 0 || s.layer_order > max_layer_idx) {
          continue;
        }

        ecc_solver::BgPoint p_min(s.rect.get_low_x(), s.rect.get_low_y());
        ecc_solver::BgPoint p_max(s.rect.get_high_x(), s.rect.get_high_y());
        ecc_solver::BgRect b_rect(p_min, p_max);

        items_by_layer[s.layer_order].emplace_back(b_rect, static_cast<int>(i));
      }
    }

    constexpr size_t kBruteForceThreshold = 8;
    using RTree = ecc_solver::bg::index::rtree<std::pair<ecc_solver::BgRect, int>, ecc_solver::bg::index::quadratic<16>>;

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

            if (ecc_solver::bg::intersects(items[a].first, items[b].first)) {
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

        ecc_solver::BgPoint p_min(s.rect.get_low_x(), s.rect.get_low_y());
        ecc_solver::BgPoint p_max(s.rect.get_high_x(), s.rect.get_high_y());
        ecc_solver::BgRect b_rect(p_min, p_max);

        std::vector<std::pair<ecc_solver::BgRect, int>> results;
        if (rtrees[s.layer_order].has_value()) {
          rtrees[s.layer_order]->query(ecc_solver::bg::index::intersects(b_rect), std::back_inserter(results));
        }

        for (const auto& res : results) {
          if (res.second > static_cast<int>(i)) {
            addEdge(static_cast<int>(i), res.second, s.layer_order);
          }
        }
      }
    }

    // 5. Bucket edges by time
    std::vector<std::vector<Edge>> edges_by_time(static_cast<size_t>(max_time) + 1);
    for (auto& e : all_edges) {
      if (e.time >= 0 && e.time <= max_time) {
        edges_by_time[e.time].push_back(std::move(e));
      }
    }
    std::vector<Edge>().swap(all_edges);

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

    UnionFind uf(static_cast<int>(nodes.size()), nodes);

    std::vector<std::vector<int>> comps_by_root(nodes.size());
    std::vector<int> active_roots;

    std::vector<idb::IdbRect> routing_rects;
    std::vector<idb::IdbRect> cut_rects;

    auto edgeCmp = [](const Edge& a, const Edge& b) {
      return std::tie(a.time, a.a, a.b) < std::tie(b.time, b.a, b.b);
    };

    auto edgeEq = [](const Edge& a, const Edge& b) {
      return a.time == b.time && a.a == b.a && a.b == b.b;
    };

    // 6. Layer-by-layer antenna calculation
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

      const AntennaRule* routing_rule = pickRule(T, true);
      const AntennaRule* cut_rule = pickRule(T, false);

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
          comps_by_root[root].clear();
        }
        continue;
      }

      for (int root : active_roots) {
        auto& comp_nodes = comps_by_root[root];
        UFNode& root_data = uf.d[root];

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

        for (int u : comp_nodes) {
          if (!nodes[u].is_conductor) {
            continue;
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

        const bool has_routing = !routing_rects.empty();
        const bool has_cut = !cut_rects.empty();

        if (!has_routing && !has_cut) {
          comps_by_root[root].clear();
          continue;
        }

        if (root_data.gate_area <= kEps && root_data.diff_area <= kEps && !root_data.diff_connected) {
          _comps_without_gate.fetch_add(1, std::memory_order_relaxed);
          comps_by_root[root].clear();
          continue;
        }

        const bool diff_active = root_data.diff_connected || (root_data.diff_area > kEps);

        auto emit = [&](const std::string& layer_name, ViolationType type, double ratio, double threshold) {
          if (ratio > threshold) {
            Violation v;
            v.net_name = net_name;
            v.layer_name = layer_name;
            v.type = type;
            v.ratio = ratio;
            v.threshold = threshold;

            if (has_bbox) {
              v.lx = getMicrons(bbox_lx);
              v.ly = getMicrons(bbox_ly);
              v.hx = getMicrons(bbox_hx);
              v.hy = getMicrons(bbox_hy);
            }

            out_violations.push_back(v);
          }
        };

        // Cut layer PAR/CAR
        if (has_cut && cut_rule != nullptr) {
          double cut_area_um = 0.0;
          double cut_perimeter_um = 0.0;

          unionAreaPerimeter(cut_rects, _micron_dbu, cut_area_um, cut_perimeter_um);

          if (cut_area_um > kEps) {
            const double gate_plus_diff_factor = (cut_rule->gate_plus_diff >= 0.0) ? cut_rule->gate_plus_diff : 0.0;
            const double eff_gate = root_data.gate_area + gate_plus_diff_factor * root_data.diff_area;

            if (eff_gate > kEps) {
              auto factor = [&](double f, bool diffuse_only) {
                if (f >= 0.0 && (!diffuse_only || diff_active)) {
                  return f;
                }
                return 1.0;
              };

              const double cut_scale = factor(cut_rule->cut_area_factor, cut_rule->cut_area_factor_diffuse_only);
              const double reduce_factor = evalPwl(cut_rule->area_diff_reduce_pwl, root_data.diff_area, 1.0);
              const double minus_diff = (cut_rule->area_minus_diff >= 0.0) ? cut_rule->area_minus_diff * root_data.diff_area : 0.0;

              const double par_cut_raw = ((cut_scale * cut_area_um) * reduce_factor - minus_diff) / eff_gate;
              const double par_cut_check = std::max(0.0, par_cut_raw);

              const double prev_cut = cut_rule->cum_routing_plus_cut ? root_data.worst_car_area : root_data.worst_car_cut;
              root_data.worst_car_cut = std::max(0.0, prev_cut + par_cut_raw);

              ThresholdPick p = pickThreshold(cut_rule->area_ratio, cut_rule->diff_area_ratio, cut_rule->diff_area_ratio_pwl,
                                              root_data.diff_area, root_data.diff_connected);

              if (p.available) {
                emit(cut_rule->layer_name, p.is_diff ? ViolationType::kAntennaDiffCutPar : ViolationType::kAntennaCutPar, par_cut_check,
                     p.threshold);
              }

              p = pickThreshold(cut_rule->cum_area_ratio, cut_rule->cum_diff_area_ratio, cut_rule->cum_diff_area_ratio_pwl,
                                root_data.diff_area, root_data.diff_connected);

              if (p.available) {
                emit(cut_rule->layer_name, p.is_diff ? ViolationType::kAntennaDiffCutCar : ViolationType::kAntennaCutCar,
                     root_data.worst_car_cut, p.threshold);
              }
            } else {
              _comps_without_gate.fetch_add(1, std::memory_order_relaxed);
            }
          }
        }

        // Routing layer PAR/CAR/PSR/CSR
        if (has_routing && routing_rule != nullptr) {
          double metal_area_um = 0.0;
          double metal_perimeter_um = 0.0;

          unionAreaPerimeter(routing_rects, _micron_dbu, metal_area_um, metal_perimeter_um);

          double thickness_um = routing_rule->thickness_um;
          if (thickness_um <= 0.0 && static_cast<size_t>(T) < _thickness_by_order.size()) {
            thickness_um = _thickness_by_order[T];
          }

          const double side_area_um = metal_perimeter_um * thickness_um;

          if (metal_area_um > kEps || side_area_um > kEps) {
            const double gate_plus_diff_factor = (routing_rule->gate_plus_diff >= 0.0) ? routing_rule->gate_plus_diff : 0.0;
            const double eff_gate = root_data.gate_area + gate_plus_diff_factor * root_data.diff_area;

            if (eff_gate > kEps) {
              auto factor = [&](double f, bool diffuse_only) {
                if (f >= 0.0 && (!diffuse_only || diff_active)) {
                  return f;
                }
                return 1.0;
              };

              const double area_scale = factor(routing_rule->area_factor, routing_rule->area_factor_diffuse_only);
              const double side_scale = factor(routing_rule->side_area_factor, routing_rule->side_area_factor_diffuse_only);

              const double area_reduce = evalPwl(routing_rule->area_diff_reduce_pwl, root_data.diff_area, 1.0);
              const double side_reduce = evalPwl(routing_rule->side_area_diff_reduce_pwl, root_data.diff_area, 1.0);

              const double minus_diff = (routing_rule->area_minus_diff >= 0.0) ? routing_rule->area_minus_diff * root_data.diff_area : 0.0;

              const double par_area_raw = ((area_scale * metal_area_um) * area_reduce - minus_diff) / eff_gate;
              const double par_side_raw = ((side_scale * side_area_um) * side_reduce) / eff_gate;

              const double par_area_check = std::max(0.0, par_area_raw);
              const double par_side_check = std::max(0.0, par_side_raw);

              const double prev_area = routing_rule->cum_routing_plus_cut ? root_data.worst_car_cut : root_data.worst_car_area;
              root_data.worst_car_area = std::max(0.0, prev_area + par_area_raw);
              root_data.worst_car_side = std::max(0.0, root_data.worst_car_side + par_side_raw);

              ThresholdPick p = pickThreshold(routing_rule->area_ratio, routing_rule->diff_area_ratio, routing_rule->diff_area_ratio_pwl,
                                              root_data.diff_area, root_data.diff_connected);

              if (p.available) {
                emit(routing_rule->layer_name, p.is_diff ? ViolationType::kAntennaDiffPar : ViolationType::kAntennaPar, par_area_check,
                     p.threshold);
              }

              p = pickThreshold(routing_rule->cum_area_ratio, routing_rule->cum_diff_area_ratio, routing_rule->cum_diff_area_ratio_pwl,
                                root_data.diff_area, root_data.diff_connected);

              if (p.available) {
                emit(routing_rule->layer_name, p.is_diff ? ViolationType::kAntennaDiffCar : ViolationType::kAntennaCar,
                     root_data.worst_car_area, p.threshold);
              }

              p = pickThreshold(routing_rule->side_area_ratio, routing_rule->diff_side_area_ratio, routing_rule->diff_side_area_ratio_pwl,
                                root_data.diff_area, root_data.diff_connected);

              if (p.available) {
                emit(routing_rule->layer_name, p.is_diff ? ViolationType::kAntennaDiffPsr : ViolationType::kAntennaPsr, par_side_check,
                     p.threshold);
              }

              p = pickThreshold(routing_rule->cum_side_area_ratio, routing_rule->cum_diff_side_area_ratio,
                                routing_rule->cum_diff_side_area_ratio_pwl, root_data.diff_area, root_data.diff_connected);

              if (p.available) {
                emit(routing_rule->layer_name, p.is_diff ? ViolationType::kAntennaDiffCsr : ViolationType::kAntennaCsr,
                     root_data.worst_car_side, p.threshold);
              }
            } else {
              _comps_without_gate.fetch_add(1, std::memory_order_relaxed);
            }
          }
        }

        comps_by_root[root].clear();
      }
    }
  }

  idb::IdbDesign* _design = nullptr;
  int _micron_dbu = 1000;

  std::vector<AntennaRule> _rules;
  std::vector<std::vector<const AntennaRule*>> _rule_map;
  std::vector<double> _thickness_by_order;

  std::vector<Violation> _violations;

  int _max_layer_order = 200;

  std::atomic<int64_t> _pins_missing_antenna_info{0};
  std::atomic<int64_t> _comps_without_gate{0};
  std::atomic<int64_t> _skipped_segments{0};
};

}  // namespace

// Public

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

ACModel AntennaChecker::initACModel(std::map<std::string, std::any>& config_map)
{
  std::string report_dir;

  auto it = config_map.find("report_dir");
  if (it != config_map.end()) {
    if (const std::string* dir = std::any_cast<std::string>(&it->second)) {
      report_dir = *dir;
    } else {
      ZHLOG.warn(Loc::current(), "config_map[\"report_dir\"] is not a string");
    }

    config_map.erase(it);
  }

  ACModel ac_model;

  if (!config_map.empty()) {
    ZHLOG.warn(Loc::current(), "The checkAntenna config has not been consumed yet!");
  }

  idb::IdbDesign* idb_design = nullptr;

  if (dmInst != nullptr) {
    auto* def_service = dmInst->get_idb_def_service();
    if (def_service != nullptr) {
      idb_design = def_service->get_design();
    }
  }

  AntennaCheckerImpl checker(idb_design);
  checker.run();

  this->set_violations(checker.get_violations());

  ZHLOG.info(Loc::current(), "violation count: ", checker.get_violation_count());

  auto type_to_string = [](ViolationType t) -> const char* {
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
      case ViolationType::kAntennaDiffCutPar:
        return "DiffCutPAR";
      case ViolationType::kAntennaDiffCutCar:
        return "DiffCutCAR";
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
          rpt << "Net: " << v.net_name << " | Layer: " << v.layer_name << " | Type: " << type_to_string(v.type) << "\n"
              << "  Ratio: " << v.ratio << " (Threshold: " << v.threshold << ")\n"
              << "  Location: (" << v.lx << ", " << v.ly << ") to (" << v.hx << ", " << v.hy << ")\n\n";
        }
      }

      rpt.close();
    } else {
      ZHLOG.warn(Loc::current(), "Cannot open antenna report file: ", report_dir + "/antenna_check.rpt");
    }
  }

  ac_model.set_violation_num(checker.get_violation_count());
  return ac_model;
}

AntennaChecker* AntennaChecker::_ac_instance = nullptr;

}  // namespace izh
