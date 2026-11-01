// ***************************************************************************************
// SPDX-License-Identifier: MulanPSL-2.0
// Copyright (c) 2026 Beijing Institute of Open Source Chip
// ***************************************************************************************

#include "AntennaKernel.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <limits>
#include <optional>
#include <tuple>
#include <utility>
#include <vector>

#include <boost/geometry.hpp>
#include <boost/geometry/geometries/box.hpp>
#include <boost/geometry/geometries/point.hpp>
#include <boost/geometry/index/predicates.hpp>
#include <boost/geometry/index/rtree.hpp>
#include "ACViolationType.hpp"
#include "MJHeader.hpp"
#include "Utility.hpp"

namespace imj {
namespace {

namespace bg = boost::geometry;
namespace bgi = boost::geometry::index;

using BgPoint = bg::model::point<int64_t, 2, bg::cs::cartesian>;
using BgBox = bg::model::box<BgPoint>;
using OverlapValue = std::pair<BgBox, int>;
using OverlapTree = bgi::rtree<OverlapValue, bgi::quadratic<16>>;

[[nodiscard]] BgBox asBox(const AntennaRect& rectangle)
{
  return BgBox{BgPoint{std::min(rectangle.ll_x, rectangle.ur_x), std::min(rectangle.ll_y, rectangle.ur_y)},
               BgPoint{std::max(rectangle.ll_x, rectangle.ur_x), std::max(rectangle.ll_y, rectangle.ur_y)}};
}

[[nodiscard]] const ACAntennaRule* pickRule(const ACModel& model, int layer_order, bool routing)
{
  const auto& map = model.get_rule_map();
  if (layer_order < 0 || static_cast<size_t>(layer_order) >= map.size()) {
    return nullptr;
  }
  for (const auto* rule : map[static_cast<size_t>(layer_order)]) {
    if (rule != nullptr && rule->is_routing == routing) {
      return rule;
    }
  }
  return nullptr;
}

[[nodiscard]] bool hasValue(double value)
{
  return !Utility::equalDoubleByError(value, 0.0, MJ_ERROR);
}

struct SweepNode
{
  int coverage = 0;
  long double length = 0.0L;
  int intervals = 0;
  bool left_covered = false;
  bool right_covered = false;
};

class SweepTree
{
 public:
  explicit SweepTree(std::vector<int64_t> coordinates) : _coordinates(std::move(coordinates))
  {
    _interval_count = static_cast<int>(_coordinates.size()) - 1;
    _nodes.resize(static_cast<size_t>(std::max(1, _interval_count)) * 4u);
  }

  [[nodiscard]] long double coveredLength() const { return _interval_count > 0 ? _nodes[1].length : 0.0L; }
  [[nodiscard]] int intervalCount() const { return _interval_count > 0 ? _nodes[1].intervals : 0; }

  void update(int left, int right, int delta)
  {
    if (_interval_count <= 0 || left > right) {
      return;
    }
    update(1, 0, _interval_count - 1, left, right, delta);
  }

  [[nodiscard]] long double coveredLength(int left, int right) const
  {
    if (_interval_count <= 0 || left > right) {
      return 0.0L;
    }
    return coveredLength(1, 0, _interval_count - 1, left, right);
  }

 private:
  void pull(int index, int left, int right)
  {
    auto& node = _nodes[static_cast<size_t>(index)];
    if (node.coverage > 0) {
      node.length = static_cast<long double>(_coordinates[static_cast<size_t>(right + 1)] - _coordinates[static_cast<size_t>(left)]);
      node.intervals = 1;
      node.left_covered = true;
      node.right_covered = true;
      return;
    }
    if (left == right) {
      node.length = 0.0L;
      node.intervals = 0;
      node.left_covered = false;
      node.right_covered = false;
      return;
    }
    const auto& lhs = _nodes[static_cast<size_t>(index * 2)];
    const auto& rhs = _nodes[static_cast<size_t>(index * 2 + 1)];
    node.length = lhs.length + rhs.length;
    node.intervals = lhs.intervals + rhs.intervals - ((lhs.right_covered && rhs.left_covered) ? 1 : 0);
    node.left_covered = lhs.left_covered;
    node.right_covered = rhs.right_covered;
  }

  void update(int index, int left, int right, int query_left, int query_right, int delta)
  {
    if (query_right < left || right < query_left) {
      return;
    }
    if (query_left <= left && right <= query_right) {
      _nodes[static_cast<size_t>(index)].coverage += delta;
      pull(index, left, right);
      return;
    }
    const int middle = left + (right - left) / 2;
    update(index * 2, left, middle, query_left, query_right, delta);
    update(index * 2 + 1, middle + 1, right, query_left, query_right, delta);
    pull(index, left, right);
  }

  [[nodiscard]] long double coveredLength(int index, int left, int right, int query_left, int query_right) const
  {
    if (query_right < left || right < query_left) {
      return 0.0L;
    }
    const auto& node = _nodes[static_cast<size_t>(index)];
    if (query_left <= left && right <= query_right) {
      return node.length;
    }
    if (node.coverage > 0) {
      const int clipped_left = std::max(left, query_left);
      const int clipped_right = std::min(right, query_right);
      return clipped_left <= clipped_right
                 ? static_cast<long double>(_coordinates[static_cast<size_t>(clipped_right + 1)]
                                             - _coordinates[static_cast<size_t>(clipped_left)])
                 : 0.0L;
    }
    if (left == right) {
      return 0.0L;
    }
    const int middle = left + (right - left) / 2;
    return coveredLength(index * 2, left, middle, query_left, query_right)
           + coveredLength(index * 2 + 1, middle + 1, right, query_left, query_right);
  }

  std::vector<int64_t> _coordinates;
  int _interval_count = 0;
  std::vector<SweepNode> _nodes;
};

struct SweepEvent
{
  int64_t x = 0;
  int first_y = 0;
  int last_y = -1;
  int delta = 0;
};

[[nodiscard]] double factor(double value, bool diffuse_only, bool diff_active)
{
  return value >= 0.0 && (!diffuse_only || diff_active) ? value : 1.0;
}

}  // namespace

void AntennaKernel::initializeRuleIndex(ACModel& model)
{
  auto& rules = model.get_rule_list();
  auto& rule_map = model.get_rule_map();
  auto& thickness = model.get_thickness_by_order();
  auto& conductive = model.get_conductive_order();

  int maximum = 0;
  for (const auto& rule : rules) {
    maximum = std::max(maximum, rule.layer_order);
  }
  model.get_max_layer_order() = maximum;
  rule_map.assign(static_cast<size_t>(maximum) + 1u, {});
  thickness.assign(static_cast<size_t>(maximum) + 1u, 0.0);
  conductive.assign(static_cast<size_t>(maximum) + 1u, false);

  for (const auto& rule : rules) {
    if (rule.layer_order < 0 || rule.layer_order > maximum) {
      continue;
    }
    auto& entries = rule_map[static_cast<size_t>(rule.layer_order)];
    entries.push_back(&rule);
    conductive[static_cast<size_t>(rule.layer_order)] = true;
    if (rule.is_routing && rule.thickness_um > 0.0 && thickness[static_cast<size_t>(rule.layer_order)] <= 0.0) {
      thickness[static_cast<size_t>(rule.layer_order)] = rule.thickness_um;
    }
  }
}

AntennaRunSummary AntennaKernel::summary(const ACModel& model)
{
  return {.signal_net_count = static_cast<uint64_t>(std::max<int64_t>(0, model.get_signal_net_cnt())),
          .violation_count = static_cast<uint64_t>(std::max(0, model.get_violation_num())),
          .rule_layer_count = static_cast<uint64_t>(model.get_rule_list().size()),
          .pins_missing_antenna_info = static_cast<uint64_t>(std::max<int64_t>(0, model.get_pins_missing_antenna_info().load())),
          .pins_with_gate_area = static_cast<uint64_t>(std::max<int64_t>(0, model.get_pins_with_gate_area().load())),
          .components_without_gate = static_cast<uint64_t>(std::max<int64_t>(0, model.get_comps_without_gate().load())),
          .skipped_segments = static_cast<uint64_t>(std::max<int64_t>(0, model.get_skipped_segments().load())),
          .conductors_out_of_range = static_cast<uint64_t>(std::max<int64_t>(0, model.get_conductors_out_of_range().load())),
          .partial_areas_dropped = static_cast<uint64_t>(std::max<int64_t>(0, model.get_partial_areas_dropped().load()))};
}

void AntennaKernel::unionArea(const std::vector<AntennaRect>& rectangles, int micron_dbu, double& area_um)
{
  double ignored_perimeter = 0.0;
  unionAreaPerimeter(rectangles, micron_dbu, area_um, ignored_perimeter);
}

void AntennaKernel::unionAreaPerimeter(const std::vector<AntennaRect>& rectangles,
                                       int micron_dbu,
                                       double& area_um,
                                       double& perimeter_um)
{
  area_um = 0.0;
  perimeter_um = 0.0;
  if (rectangles.empty() || micron_dbu <= 0) {
    return;
  }

  std::vector<AntennaRect> normalized;
  std::vector<int64_t> ys;
  normalized.reserve(rectangles.size());
  ys.reserve(rectangles.size() * 2u);
  for (auto rectangle : rectangles) {
    if (rectangle.ll_x > rectangle.ur_x) {
      std::swap(rectangle.ll_x, rectangle.ur_x);
    }
    if (rectangle.ll_y > rectangle.ur_y) {
      std::swap(rectangle.ll_y, rectangle.ur_y);
    }
    if (rectangle.ll_x >= rectangle.ur_x || rectangle.ll_y >= rectangle.ur_y) {
      continue;
    }
    normalized.push_back(rectangle);
    ys.push_back(rectangle.ll_y);
    ys.push_back(rectangle.ur_y);
  }
  if (normalized.empty()) {
    return;
  }

  std::sort(ys.begin(), ys.end());
  ys.erase(std::unique(ys.begin(), ys.end()), ys.end());
  if (ys.size() < 2u) {
    return;
  }

  std::vector<SweepEvent> events;
  events.reserve(normalized.size() * 2u);
  const auto y_index = [&ys](int64_t value) {
    return static_cast<int>(std::lower_bound(ys.begin(), ys.end(), value) - ys.begin());
  };
  for (const auto& rectangle : normalized) {
    const int first = y_index(rectangle.ll_y);
    const int last = y_index(rectangle.ur_y) - 1;
    if (first <= last) {
      events.push_back({.x = rectangle.ll_x, .first_y = first, .last_y = last, .delta = 1});
      events.push_back({.x = rectangle.ur_x, .first_y = first, .last_y = last, .delta = -1});
    }
  }
  std::sort(events.begin(), events.end(), [](const SweepEvent& lhs, const SweepEvent& rhs) { return lhs.x < rhs.x; });

  SweepTree tree(std::move(ys));
  long double area = 0.0L;
  long double perimeter = 0.0L;
  int64_t previous_x = events.front().x;
  size_t index = 0;
  while (index < events.size()) {
    const int64_t x = events[index].x;
    const long double dx = static_cast<long double>(x - previous_x);
    if (dx > 0.0L) {
      area += tree.coveredLength() * dx;
      perimeter += 2.0L * static_cast<long double>(tree.intervalCount()) * dx;
    }

    size_t end = index;
    while (end < events.size() && events[end].x == x) {
      ++end;
    }
    const long double before = tree.coveredLength();
    for (size_t event = index; event < end; ++event) {
      if (events[event].delta > 0) {
        tree.update(events[event].first_y, events[event].last_y, events[event].delta);
      }
    }
    const long double after_insert = tree.coveredLength();
    perimeter += std::abs(after_insert - before);
    for (size_t event = index; event < end; ++event) {
      if (events[event].delta < 0) {
        tree.update(events[event].first_y, events[event].last_y, events[event].delta);
      }
    }
    perimeter += std::abs(tree.coveredLength() - after_insert);
    previous_x = x;
    index = end;
  }

  const long double dbu = static_cast<long double>(micron_dbu);
  area_um = static_cast<double>(std::max(area, 0.0L) / (dbu * dbu));
  perimeter_um = static_cast<double>(std::max(perimeter, 0.0L) / dbu);
}

ACThresholdPick AntennaKernel::pickThreshold(double plain_ratio,
                                              double diff_ratio,
                                              const std::vector<std::pair<double, double>>& diff_pwl,
                                              double diff_area,
                                              bool diff_connected)
{
  const bool has_diff = diff_ratio >= 0.0 || !diff_pwl.empty();
  const bool has_plain = plain_ratio >= 0.0;
  bool use_diff = has_diff && (diff_connected || hasValue(diff_area) || !has_plain);
  if (use_diff && !diff_pwl.empty() && !hasValue(diff_area) && has_plain) {
    use_diff = false;
  }
  if (use_diff) {
    return {true, diff_pwl.empty() ? diff_ratio : Utility::getPWLValue(diff_pwl, diff_area, diff_ratio), true};
  }
  return has_plain ? ACThresholdPick{true, plain_ratio, false} : ACThresholdPick{};
}

void AntennaKernel::checkNet(ACModel& model, const AntennaGraphInput& input, std::vector<ACViolation>& violations)
{
  if (input.nodes.empty()) {
    return;
  }

  std::vector<AntennaGraphNode> nodes = input.nodes;
  std::vector<AntennaGraphEdge> edges = input.edges;
  int maximum_time = 0;
  for (const auto& node : nodes) {
    maximum_time = std::max(maximum_time, node.time);
    for (const auto& shape : node.shapes) {
      maximum_time = std::max(maximum_time, shape.layer_order);
    }
  }
  for (const auto& edge : edges) {
    maximum_time = std::max(maximum_time, edge.time);
  }
  if (maximum_time < 0) {
    return;
  }

  const auto append_edge = [&](int first, int second, int time) {
    if (first < 0 || second < 0 || first == second || time < 0 || time > maximum_time) {
      return;
    }
    if (first > second) {
      std::swap(first, second);
    }
    edges.push_back({.first = first, .second = second, .time = time});
  };

  std::vector<std::vector<OverlapValue>> shapes_by_layer(static_cast<size_t>(maximum_time) + 1u);
  for (size_t index = 0; index < nodes.size(); ++index) {
    if (!nodes[index].is_conductor) {
      continue;
    }
    for (const auto& shape : nodes[index].shapes) {
      if (shape.layer_order >= 0 && shape.layer_order <= maximum_time) {
        shapes_by_layer[static_cast<size_t>(shape.layer_order)].emplace_back(asBox(shape.rectangle), static_cast<int>(index));
      }
    }
  }
  for (int layer = 0; layer <= maximum_time; ++layer) {
    auto& shapes = shapes_by_layer[static_cast<size_t>(layer)];
    if (shapes.size() < 2u) {
      continue;
    }
    OverlapTree tree(shapes.begin(), shapes.end());
    for (const auto& value : shapes) {
      std::vector<OverlapValue> overlaps;
      tree.query(bgi::intersects(value.first), std::back_inserter(overlaps));
      for (const auto& overlap : overlaps) {
        if (value.second < overlap.second) {
          append_edge(value.second, overlap.second, layer);
        }
      }
    }
  }

  std::vector<std::vector<AntennaGraphEdge>> edges_by_time(static_cast<size_t>(maximum_time) + 1u);
  for (auto edge : edges) {
    if (edge.time >= 0 && edge.time <= maximum_time) {
      if (edge.first > edge.second) {
        std::swap(edge.first, edge.second);
      }
      edges_by_time[static_cast<size_t>(edge.time)].push_back(edge);
    }
  }
  std::vector<std::vector<int>> nodes_by_time(static_cast<size_t>(maximum_time) + 1u);
  for (size_t index = 0; index < nodes.size(); ++index) {
    if (nodes[index].is_conductor && nodes[index].time >= 0 && nodes[index].time <= maximum_time) {
      nodes_by_time[static_cast<size_t>(nodes[index].time)].push_back(static_cast<int>(index));
    }
  }

  // ACUnionFind is legacy-data-model specific. Build its equivalent state
  // locally so this kernel remains independent from iDB geometry classes.
  struct UnionData {
    int parent = -1;
    uint32_t rank = 0;
    double gate_area = 0.0;
    double diff_area = 0.0;
    bool diff_connected = false;
    double cumulative_area = 0.0;
    double cumulative_side = 0.0;
    double cumulative_cut = 0.0;
  };
  std::vector<UnionData> union_data(nodes.size());
  for (size_t index = 0; index < nodes.size(); ++index) {
    union_data[index] = {.parent = static_cast<int>(index),
                         .gate_area = nodes[index].gate_area,
                         .diff_area = nodes[index].diff_area,
                         .diff_connected = nodes[index].provides_diff || hasValue(nodes[index].diff_area)};
  }
  const auto find = [&](auto&& self, int index) -> int {
    if (union_data[static_cast<size_t>(index)].parent == index) {
      return index;
    }
    union_data[static_cast<size_t>(index)].parent = self(self, union_data[static_cast<size_t>(index)].parent);
    return union_data[static_cast<size_t>(index)].parent;
  };
  const auto merge = [&](int first, int second) {
    int left = find(find, first);
    int right = find(find, second);
    if (left == right) {
      return;
    }
    if (union_data[static_cast<size_t>(left)].rank < union_data[static_cast<size_t>(right)].rank) {
      std::swap(left, right);
    }
    auto& lhs = union_data[static_cast<size_t>(left)];
    auto& rhs = union_data[static_cast<size_t>(right)];
    rhs.parent = left;
    if (lhs.rank == rhs.rank) {
      ++lhs.rank;
    }
    lhs.gate_area += rhs.gate_area;
    lhs.diff_area += rhs.diff_area;
    lhs.diff_connected = lhs.diff_connected || rhs.diff_connected;
    lhs.cumulative_area += rhs.cumulative_area;
    lhs.cumulative_side += rhs.cumulative_side;
    lhs.cumulative_cut += rhs.cumulative_cut;
  };

  const int dbu = model.get_micron_dbu();
  if (dbu <= 0) {
    return;
  }
  std::vector<std::vector<int>> component_nodes(nodes.size());
  std::vector<int> active_roots;
  for (int time = 0; time <= maximum_time; ++time) {
    auto& time_edges = edges_by_time[static_cast<size_t>(time)];
    std::sort(time_edges.begin(), time_edges.end(), [](const auto& lhs, const auto& rhs) {
      return std::tie(lhs.first, lhs.second) < std::tie(rhs.first, rhs.second);
    });
    time_edges.erase(std::unique(time_edges.begin(), time_edges.end(), [](const auto& lhs, const auto& rhs) {
                       return lhs.first == rhs.first && lhs.second == rhs.second;
                     }),
                     time_edges.end());
    for (const auto& edge : time_edges) {
      merge(edge.first, edge.second);
    }
    if (nodes_by_time[static_cast<size_t>(time)].empty()) {
      continue;
    }

    active_roots.clear();
    for (const int node : nodes_by_time[static_cast<size_t>(time)]) {
      const int root = find(find, node);
      if (component_nodes[static_cast<size_t>(root)].empty()) {
        active_roots.push_back(root);
      }
      component_nodes[static_cast<size_t>(root)].push_back(node);
    }
    const auto* routing_rule = pickRule(model, time, true);
    const auto* cut_rule = pickRule(model, time, false);
    for (const int root : active_roots) {
      auto& members = component_nodes[static_cast<size_t>(root)];
      auto& state = union_data[static_cast<size_t>(root)];
      std::vector<AntennaRect> routing_rectangles;
      std::vector<AntennaRect> cut_rectangles;
      double declared_routing = 0.0;
      double declared_side = 0.0;
      double declared_cut = 0.0;
      bool has_bbox = false;
      AntennaRect bbox{.ll_x = std::numeric_limits<int64_t>::max(), .ll_y = std::numeric_limits<int64_t>::max(),
                       .ur_x = std::numeric_limits<int64_t>::min(), .ur_y = std::numeric_limits<int64_t>::min()};
      for (const int member : members) {
        const auto& node = nodes[static_cast<size_t>(member)];
        if (node.declared_area > 0.0) {
          if (node.is_cut) {
            declared_cut += node.declared_area;
          } else if (node.is_side) {
            declared_side += node.declared_area;
          } else if (node.is_routing) {
            declared_routing += node.declared_area;
          }
        }
        for (const auto& shape : node.shapes) {
          if (shape.layer_order != time) {
            continue;
          }
          const auto rectangle = AntennaRect{.ll_x = std::min(shape.rectangle.ll_x, shape.rectangle.ur_x),
                                             .ll_y = std::min(shape.rectangle.ll_y, shape.rectangle.ur_y),
                                             .ur_x = std::max(shape.rectangle.ll_x, shape.rectangle.ur_x),
                                             .ur_y = std::max(shape.rectangle.ll_y, shape.rectangle.ur_y)};
          if (node.is_routing) {
            routing_rectangles.push_back(rectangle);
          } else if (node.is_cut) {
            cut_rectangles.push_back(rectangle);
          }
          bbox.ll_x = std::min(bbox.ll_x, rectangle.ll_x);
          bbox.ll_y = std::min(bbox.ll_y, rectangle.ll_y);
          bbox.ur_x = std::max(bbox.ur_x, rectangle.ur_x);
          bbox.ur_y = std::max(bbox.ur_y, rectangle.ur_y);
          has_bbox = true;
        }
      }

      double physical_metal_area = 0.0;
      double physical_metal_perimeter = 0.0;
      unionAreaPerimeter(routing_rectangles, dbu, physical_metal_area, physical_metal_perimeter);
      double physical_cut_area = 0.0;
      unionArea(cut_rectangles, dbu, physical_cut_area);
      const double metal_area = declared_routing + physical_metal_area;
      const double cut_area = declared_cut + physical_cut_area;
      const double thickness = routing_rule != nullptr ? routing_rule->thickness_um
                                                       : (static_cast<size_t>(time) < model.get_thickness_by_order().size()
                                                              ? model.get_thickness_by_order()[static_cast<size_t>(time)]
                                                              : 0.0);
      const double side_area = declared_side + physical_metal_perimeter * thickness;

      if (!hasValue(metal_area) && !hasValue(cut_area) && !hasValue(side_area)) {
        members.clear();
        continue;
      }
      if (!hasValue(state.gate_area) && !hasValue(state.diff_area) && !state.diff_connected) {
        model.get_comps_without_gate().fetch_add(1, std::memory_order_relaxed);
        members.clear();
        continue;
      }
      const bool diff_active = state.diff_connected || hasValue(state.diff_area);
      const auto emit = [&](const std::string& layer, ACViolationType type, double ratio, double threshold) {
        if (ratio <= threshold) {
          return;
        }
        ACViolation violation;
        violation.net_name = input.net_name;
        violation.layer_name = layer;
        violation.type = type;
        violation.ratio = ratio;
        violation.threshold = threshold;
        if (has_bbox) {
          violation.lx = Utility::getRatio(bbox.ll_x, dbu);
          violation.ly = Utility::getRatio(bbox.ll_y, dbu);
          violation.hx = Utility::getRatio(bbox.ur_x, dbu);
          violation.hy = Utility::getRatio(bbox.ur_y, dbu);
        }
        violations.push_back(std::move(violation));
      };

      const auto evaluate = [&](const ACAntennaRule& rule, bool cut) {
        const double effective_gate = state.gate_area + (rule.gate_plus_diff >= 0.0 ? rule.gate_plus_diff * state.diff_area : 0.0);
        if (!hasValue(effective_gate)) {
          model.get_comps_without_gate().fetch_add(1, std::memory_order_relaxed);
          return;
        }
        const double raw_area = cut ? cut_area : metal_area;
        const double raw_side = cut ? 0.0 : side_area;
        const double scaled_area = factor(rule.area_factor, rule.area_factor_diffuse_only, diff_active) * raw_area;
        const double reduced_area = scaled_area * Utility::getPWLValue(rule.area_diff_reduce_pwl, state.diff_area, 1.0)
                                    - (rule.area_minus_diff >= 0.0 ? rule.area_minus_diff * state.diff_area : 0.0);
        const double area_numerator = std::max(0.0, reduced_area);
        const double side_numerator = std::max(0.0, factor(rule.side_area_factor, rule.side_area_factor_diffuse_only, diff_active) * raw_side);
        const double par = Utility::getRatio(area_numerator, effective_gate);
        const double psr = Utility::getRatio(side_numerator, effective_gate);
        if (cut) {
          const double prior = rule.cum_routing_plus_cut ? state.cumulative_area : state.cumulative_cut;
          state.cumulative_cut = std::max(0.0, prior + reduced_area);
          const double car = Utility::getRatio(state.cumulative_cut, effective_gate);
          auto threshold = pickThreshold(rule.area_ratio, rule.diff_area_ratio, rule.diff_area_ratio_pwl, state.diff_area, state.diff_connected);
          if (threshold.available) {
            emit(rule.layer_name, threshold.is_diff ? ACViolationType::kAntennaDiffCutPar : ACViolationType::kAntennaCutPar, par,
                 threshold.threshold);
          }
          threshold = pickThreshold(rule.cum_area_ratio, rule.cum_diff_area_ratio, rule.cum_diff_area_ratio_pwl, state.diff_area,
                                    state.diff_connected);
          if (threshold.available) {
            emit(rule.layer_name, threshold.is_diff ? ACViolationType::kAntennaDiffCutCar : ACViolationType::kAntennaCutCar, car,
                 threshold.threshold);
          }
          return;
        }
        const double prior = rule.cum_routing_plus_cut ? state.cumulative_cut : state.cumulative_area;
        state.cumulative_area = std::max(0.0, prior + reduced_area);
        state.cumulative_side = std::max(0.0, state.cumulative_side + side_numerator);
        const double car = Utility::getRatio(state.cumulative_area, effective_gate);
        const double csr = Utility::getRatio(state.cumulative_side, effective_gate);
        auto threshold = pickThreshold(rule.area_ratio, rule.diff_area_ratio, rule.diff_area_ratio_pwl, state.diff_area, state.diff_connected);
        if (threshold.available) {
          emit(rule.layer_name, threshold.is_diff ? ACViolationType::kAntennaDiffPar : ACViolationType::kAntennaPar, par,
               threshold.threshold);
        }
        threshold = pickThreshold(rule.cum_area_ratio, rule.cum_diff_area_ratio, rule.cum_diff_area_ratio_pwl, state.diff_area,
                                  state.diff_connected);
        if (threshold.available) {
          emit(rule.layer_name, threshold.is_diff ? ACViolationType::kAntennaDiffCar : ACViolationType::kAntennaCar, car,
               threshold.threshold);
        }
        threshold = pickThreshold(rule.side_area_ratio, rule.diff_side_area_ratio, rule.diff_side_area_ratio_pwl, state.diff_area,
                                  state.diff_connected);
        if (threshold.available) {
          emit(rule.layer_name, threshold.is_diff ? ACViolationType::kAntennaDiffPsr : ACViolationType::kAntennaPsr, psr,
               threshold.threshold);
        }
        threshold = pickThreshold(rule.cum_side_area_ratio, rule.cum_diff_side_area_ratio, rule.cum_diff_side_area_ratio_pwl,
                                  state.diff_area, state.diff_connected);
        if (threshold.available) {
          emit(rule.layer_name, threshold.is_diff ? ACViolationType::kAntennaDiffCsr : ACViolationType::kAntennaCsr, csr,
               threshold.threshold);
        }
      };

      if (routing_rule != nullptr) {
        evaluate(*routing_rule, false);
      } else {
        state.cumulative_area += declared_routing;
        state.cumulative_side += declared_side;
      }
      if (cut_rule != nullptr) {
        evaluate(*cut_rule, true);
      } else {
        state.cumulative_cut += declared_cut;
      }
      members.clear();
    }
  }
}

}  // namespace imj
