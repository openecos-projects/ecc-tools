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
#include "LVSChecker.hpp"

#include <algorithm>
#include <cstdint>
#include <iterator>
#include <limits>
#include <map>
#include <tuple>
#include <unordered_set>
#include <utility>
#include <vector>

namespace ilvs {

// public

void LVSChecker::initInst()
{
  if (_lc_instance == nullptr) {
    _lc_instance = new LVSChecker();
  }
}

LVSChecker& LVSChecker::getInst()
{
  if (_lc_instance == nullptr) {
    LVSLOG.error(Loc::current(), "The instance not initialized!");
  }
  return *_lc_instance;
}

void LVSChecker::destroyInst()
{
  if (_lc_instance != nullptr) {
    delete _lc_instance;
    _lc_instance = nullptr;
  }
}

// private

LVSChecker* LVSChecker::_lc_instance = nullptr;

namespace {

constexpr size_t kInvalidShapeIndex = std::numeric_limits<size_t>::max();
constexpr uint64_t kInvalidComponentId = std::numeric_limits<uint64_t>::max();

class DisjointSet
{
 public:
  explicit DisjointSet(size_t size) : _parent(size), _rank(size, 0)
  {
    for (size_t idx = 0; idx < size; idx++) {
      _parent[idx] = idx;
    }
  }

  size_t find(size_t node)
  {
    if (_parent[node] != node) {
      _parent[node] = find(_parent[node]);
    }
    return _parent[node];
  }

  void unite(size_t first, size_t second)
  {
    first = find(first);
    second = find(second);
    if (first == second) {
      return;
    }
    if (_rank[first] < _rank[second]) {
      std::swap(first, second);
    }
    _parent[second] = first;
    if (_rank[first] == _rank[second]) {
      _rank[first]++;
    }
  }

 private:
  std::vector<size_t> _parent;
  std::vector<uint8_t> _rank;
};

std::vector<std::string> getSortedUniqueStringList(std::vector<std::string> string_list)
{
  std::sort(string_list.begin(), string_list.end());
  string_list.erase(std::unique(string_list.begin(), string_list.end()), string_list.end());
  return string_list;
}

template <typename TValue>
std::vector<std::string> getSortedKeyList(const std::unordered_map<std::string, TValue>& value_map)
{
  std::vector<std::string> key_list;
  key_list.reserve(value_map.size());
  for (const auto& [key, value] : value_map) {
    (void) value;
    key_list.push_back(key);
  }
  return getSortedUniqueStringList(std::move(key_list));
}

std::vector<std::string> getDifference(const std::vector<std::string>& first_list, const std::vector<std::string>& second_list)
{
  std::vector<std::string> difference_list;
  std::set_difference(first_list.begin(), first_list.end(), second_list.begin(), second_list.end(), std::back_inserter(difference_list));
  return difference_list;
}

int32_t getMidpoint(int32_t first_coordinate, int32_t second_coordinate)
{
  return static_cast<int32_t>((static_cast<int64_t>(first_coordinate) + second_coordinate) / 2);
}

struct SupplyTrack
{
  bool is_power = false;
  std::string net_name;
  uint64_t component_id = 0;
  int32_t layer_id = -1;
  int32_t layer_order = -1;
  int32_t position = 0;
  int32_t span_low = 0;
  int32_t span_high = 0;
};

SupplyPoint makeSupplyPoint(const SupplyTrack& track, bool is_horizontal)
{
  SupplyPoint supply_point;
  supply_point.net_name = track.net_name;
  supply_point.component_id = track.component_id;
  supply_point.layer_id = track.layer_id;
  supply_point.layer_order = track.layer_order;
  supply_point.is_power = track.is_power;
  if (is_horizontal) {
    supply_point.x = getMidpoint(track.span_low, track.span_high);
    supply_point.y = track.position;
  } else {
    supply_point.x = track.position;
    supply_point.y = getMidpoint(track.span_low, track.span_high);
  }
  return supply_point;
}

std::vector<SupplyPoint> getSupplyPointList(const PhysicalGraph& physical_graph)
{
  int32_t highest_layer_order = -1;
  for (const SupplyRouteShape& route_shape : physical_graph.supply_route_shape_list) {
    if (physical_graph.power_net_set.contains(route_shape.net_name) || physical_graph.ground_net_set.contains(route_shape.net_name)) {
      highest_layer_order = std::max(highest_layer_order, route_shape.layer_order);
    }
  }
  if (highest_layer_order < 0) {
    return {};
  }

  int64_t horizontal_span = 0;
  int64_t vertical_span = 0;
  for (const SupplyRouteShape& route_shape : physical_graph.supply_route_shape_list) {
    if (route_shape.layer_order != highest_layer_order) {
      continue;
    }
    const int64_t span_x = static_cast<int64_t>(route_shape.shape.ur_x) - route_shape.shape.ll_x;
    const int64_t span_y = static_cast<int64_t>(route_shape.shape.ur_y) - route_shape.shape.ll_y;
    if (span_x > span_y) {
      horizontal_span += span_x;
    } else if (span_y > span_x) {
      vertical_span += span_y;
    }
  }
  if (horizontal_span == 0 && vertical_span == 0) {
    return {};
  }
  const bool is_horizontal = horizontal_span >= vertical_span;

  using SupplyTrackKey = std::tuple<bool, std::string, uint64_t, int32_t, int32_t, int32_t>;
  std::map<SupplyTrackKey, SupplyTrack> supply_track_map;
  for (const SupplyRouteShape& route_shape : physical_graph.supply_route_shape_list) {
    if (route_shape.layer_order != highest_layer_order) {
      continue;
    }
    const bool is_power = physical_graph.power_net_set.contains(route_shape.net_name);
    const bool is_ground = physical_graph.ground_net_set.contains(route_shape.net_name);
    if (!is_power && !is_ground) {
      continue;
    }
    const int64_t span_x = static_cast<int64_t>(route_shape.shape.ur_x) - route_shape.shape.ll_x;
    const int64_t span_y = static_cast<int64_t>(route_shape.shape.ur_y) - route_shape.shape.ll_y;
    if ((is_horizontal && span_x <= span_y) || (!is_horizontal && span_y <= span_x)) {
      continue;
    }
    const int32_t position = is_horizontal ? getMidpoint(route_shape.shape.ll_y, route_shape.shape.ur_y)
                                           : getMidpoint(route_shape.shape.ll_x, route_shape.shape.ur_x);
    const int32_t span_low = is_horizontal ? route_shape.shape.ll_x : route_shape.shape.ll_y;
    const int32_t span_high = is_horizontal ? route_shape.shape.ur_x : route_shape.shape.ur_y;
    const SupplyTrackKey key = {is_power, route_shape.net_name, route_shape.component_id, route_shape.shape.layer_id,
                                route_shape.layer_order, position};
    auto [track_iter, inserted] = supply_track_map.emplace(
        key, SupplyTrack{is_power, route_shape.net_name, route_shape.component_id, route_shape.shape.layer_id, route_shape.layer_order,
                         position, span_low, span_high});
    if (!inserted) {
      track_iter->second.span_low = std::min(track_iter->second.span_low, span_low);
      track_iter->second.span_high = std::max(track_iter->second.span_high, span_high);
    }
  }

  std::vector<SupplyTrack> supply_track_list;
  supply_track_list.reserve(supply_track_map.size());
  for (const auto& [key, track] : supply_track_map) {
    (void) key;
    supply_track_list.push_back(track);
  }
  std::sort(supply_track_list.begin(), supply_track_list.end(), [](const SupplyTrack& first, const SupplyTrack& second) {
    return std::tie(first.position, first.is_power, first.net_name, first.component_id) <
           std::tie(second.position, second.is_power, second.net_name, second.component_id);
  });
  if (supply_track_list.empty()) {
    return {};
  }

  std::vector<SupplyPoint> supply_point_list;
  const SupplyTrack& first_track = supply_track_list.front();
  supply_point_list.push_back(makeSupplyPoint(first_track, is_horizontal));
  for (auto track_iter = supply_track_list.rbegin(); track_iter != supply_track_list.rend(); ++track_iter) {
    if (track_iter->is_power != first_track.is_power) {
      supply_point_list.push_back(makeSupplyPoint(*track_iter, is_horizontal));
      break;
    }
  }
  std::sort(supply_point_list.begin(), supply_point_list.end(), [](const SupplyPoint& first, const SupplyPoint& second) {
    if (first.is_power != second.is_power) {
      return first.is_power > second.is_power;
    }
    return std::tie(first.net_name, first.component_id) < std::tie(second.net_name, second.component_id);
  });
  return supply_point_list;
}

void checkSupplyConnectivity(CheckResult& result, const PhysicalGraph& physical_graph,
                             const std::unordered_map<std::string, std::string>& instance_pin_net_map, bool is_power)
{
  uint64_t& instance_pin_num = is_power ? result.power_instance_pin_num : result.ground_instance_pin_num;
  uint64_t& connected_instance_pin_num =
      is_power ? result.connected_power_instance_pin_num : result.connected_ground_instance_pin_num;
  uint64_t& disconnected_instance_pin_num =
      is_power ? result.disconnected_power_instance_pin_num : result.disconnected_ground_instance_pin_num;
  instance_pin_num = instance_pin_net_map.size();
  if (instance_pin_net_map.empty()) {
    return;
  }

  const auto supply_point_iter = std::find_if(result.supply_point_list.begin(), result.supply_point_list.end(), [is_power](const SupplyPoint& point) {
    return point.is_power == is_power;
  });
  if (supply_point_iter == result.supply_point_list.end()) {
    disconnected_instance_pin_num = instance_pin_net_map.size();
    Violation violation;
    violation.type = is_power ? "PowerSupplyPointMissing" : "GroundSupplyPointMissing";
    violation.terminal_list = getSortedKeyList(instance_pin_net_map);
    for (const auto& [terminal_name, net_name] : instance_pin_net_map) {
      (void) terminal_name;
      violation.related_net_name_list.push_back(net_name);
    }
    violation.related_net_name_list = getSortedUniqueStringList(std::move(violation.related_net_name_list));
    result.violation_list.push_back(std::move(violation));
    return;
  }

  std::map<std::pair<std::string, uint64_t>, std::vector<std::string>> disconnected_terminal_map;
  for (const auto& [terminal_name, net_name] : instance_pin_net_map) {
    const auto component_iter = physical_graph.terminal_component_map.find(terminal_name);
    if (component_iter != physical_graph.terminal_component_map.end() && component_iter->second == supply_point_iter->component_id) {
      connected_instance_pin_num++;
      continue;
    }
    disconnected_instance_pin_num++;
    const uint64_t component_id = component_iter == physical_graph.terminal_component_map.end() ? kInvalidComponentId : component_iter->second;
    disconnected_terminal_map[{net_name, component_id}].push_back(terminal_name);
  }
  for (auto& [key, terminal_list] : disconnected_terminal_map) {
    std::sort(terminal_list.begin(), terminal_list.end());
    Violation violation;
    violation.type = is_power ? "PowerDisconnected" : "GroundDisconnected";
    violation.net_name = key.first;
    violation.terminal_list = std::move(terminal_list);
    if (key.second != kInvalidComponentId) {
      violation.component_id_list.push_back(key.second);
    }
    result.violation_list.push_back(std::move(violation));
  }
}

bool isPowerGroundIO(const std::string& io_pin_name, const PhysicalGraph& physical_graph)
{
  constexpr char kPinPrefix[] = "PIN/";
  const std::string net_name = io_pin_name.rfind(kPinPrefix, 0) == 0 ? io_pin_name.substr(sizeof(kPinPrefix) - 1) : io_pin_name;
  return physical_graph.power_net_set.contains(net_name) || physical_graph.ground_net_set.contains(net_name);
}

std::vector<std::string> getComparedIOList(const std::vector<std::string>& io_pin_list, const PhysicalGraph& physical_graph,
                                           uint64_t& power_ground_io_num)
{
  power_ground_io_num = 0;
  std::vector<std::string> compared_io_list;
  for (const std::string& io_pin_name : getSortedUniqueStringList(io_pin_list)) {
    if (isPowerGroundIO(io_pin_name, physical_graph)) {
      power_ground_io_num++;
    } else {
      compared_io_list.push_back(io_pin_name);
    }
  }
  return compared_io_list;
}

bool isIntersected(const ShapeLocation& first_shape, const ShapeLocation& second_shape)
{
  return first_shape.ll_x <= second_shape.ur_x && second_shape.ll_x <= first_shape.ur_x && first_shape.ll_y <= second_shape.ur_y
         && second_shape.ll_y <= first_shape.ur_y;
}

struct RoutingCheck
{
  std::string net_name;
  std::string driver_terminal_name;
  std::vector<std::string> disconnected_terminal_list;
  std::vector<ShapeLocation> disconnected_shape_list;
  bool missing_driver = false;
  bool connected = false;
};

size_t getTerminalRoot(const NetRoutingGraph& routing_graph, const std::string& terminal_name, DisjointSet& graph)
{
  auto terminal_iter = routing_graph.terminal_shape_map.find(terminal_name);
  if (terminal_iter == routing_graph.terminal_shape_map.end()) {
    return kInvalidShapeIndex;
  }
  for (uint64_t shape_idx : terminal_iter->second) {
    if (shape_idx < routing_graph.shape_list.size()) {
      return graph.find(static_cast<size_t>(shape_idx));
    }
  }
  return kInvalidShapeIndex;
}

RoutingCheck checkNetRoutingConnectivity(const Net& net, const NetRoutingGraph* routing_graph)
{
  RoutingCheck routing_check;
  routing_check.net_name = net.name;
  if (net.terminal_list.size() <= 1) {
    routing_check.connected = true;
    return routing_check;
  }
  if (routing_graph == nullptr) {
    routing_check.disconnected_terminal_list = net.terminal_list;
    return routing_check;
  }

  routing_check.driver_terminal_name = routing_graph->driver_terminal_name;
  DisjointSet graph(routing_graph->shape_list.size());
  std::unordered_map<int32_t, std::vector<size_t>> layer_shape_map;
  for (size_t shape_idx = 0; shape_idx < routing_graph->shape_list.size(); shape_idx++) {
    layer_shape_map[routing_graph->shape_list[shape_idx].layer_id].push_back(shape_idx);
  }
  for (auto& [layer_id, shape_index_list] : layer_shape_map) {
    (void) layer_id;
    std::sort(shape_index_list.begin(), shape_index_list.end(), [&routing_graph](size_t first_idx, size_t second_idx) {
      const ShapeLocation& first_shape = routing_graph->shape_list[first_idx];
      const ShapeLocation& second_shape = routing_graph->shape_list[second_idx];
      if (first_shape.ll_x != second_shape.ll_x) {
        return first_shape.ll_x < second_shape.ll_x;
      }
      if (first_shape.ll_y != second_shape.ll_y) {
        return first_shape.ll_y < second_shape.ll_y;
      }
      return first_idx < second_idx;
    });
    std::vector<size_t> active_shape_index_list;
    for (size_t shape_idx : shape_index_list) {
      const ShapeLocation& shape = routing_graph->shape_list[shape_idx];
      active_shape_index_list.erase(
          std::remove_if(active_shape_index_list.begin(), active_shape_index_list.end(), [&routing_graph, &shape](size_t active_shape_idx) {
            return routing_graph->shape_list[active_shape_idx].ur_x < shape.ll_x;
          }),
          active_shape_index_list.end());
      for (size_t active_shape_idx : active_shape_index_list) {
        if (isIntersected(routing_graph->shape_list[active_shape_idx], shape)) {
          graph.unite(active_shape_idx, shape_idx);
        }
      }
      active_shape_index_list.push_back(shape_idx);
    }
  }
  for (const auto& [bottom_shape_idx, top_shape_idx] : routing_graph->via_shape_pair_list) {
    if (bottom_shape_idx < routing_graph->shape_list.size() && top_shape_idx < routing_graph->shape_list.size()) {
      graph.unite(static_cast<size_t>(bottom_shape_idx), static_cast<size_t>(top_shape_idx));
    }
  }
  for (const auto& [terminal_name, shape_index_list] : routing_graph->terminal_shape_map) {
    (void) terminal_name;
    size_t first_shape_idx = kInvalidShapeIndex;
    for (uint64_t shape_idx : shape_index_list) {
      if (shape_idx >= routing_graph->shape_list.size()) {
        continue;
      }
      if (first_shape_idx == kInvalidShapeIndex) {
        first_shape_idx = static_cast<size_t>(shape_idx);
      } else {
        graph.unite(first_shape_idx, static_cast<size_t>(shape_idx));
      }
    }
  }

  const size_t driver_root = getTerminalRoot(*routing_graph, routing_check.driver_terminal_name, graph);
  if (routing_check.driver_terminal_name.empty() || driver_root == kInvalidShapeIndex) {
    routing_check.missing_driver = true;
    routing_check.disconnected_terminal_list = net.terminal_list;
    routing_check.disconnected_shape_list = routing_graph->shape_list;
    return routing_check;
  }

  std::unordered_set<size_t> disconnected_root_set;
  for (const std::string& terminal_name : net.terminal_list) {
    if (terminal_name == routing_check.driver_terminal_name) {
      continue;
    }
    const size_t terminal_root = getTerminalRoot(*routing_graph, terminal_name, graph);
    if (terminal_root == kInvalidShapeIndex || terminal_root != driver_root) {
      routing_check.disconnected_terminal_list.push_back(terminal_name);
      if (terminal_root != kInvalidShapeIndex) {
        disconnected_root_set.insert(terminal_root);
      }
    }
  }
  if (routing_check.disconnected_terminal_list.empty()) {
    routing_check.connected = true;
    return routing_check;
  }
  for (size_t shape_idx = 0; shape_idx < routing_graph->shape_list.size(); shape_idx++) {
    if (disconnected_root_set.contains(graph.find(shape_idx))) {
      routing_check.disconnected_shape_list.push_back(routing_graph->shape_list[shape_idx]);
    }
  }
  return routing_check;
}

}  // namespace

#if 1  // check

CheckResult LVSChecker::check(const Netlist& netlist, const Netlist& def)
{
  Monitor monitor;
  LVSLOG.info(Loc::current(), "Starting...");

  CheckResult result;
  const std::vector<std::string> netlist_io_list =
      getComparedIOList(netlist.logical_graph.io_pin_list, def.physical_graph, result.netlist_power_ground_io_num);
  const std::vector<std::string> def_io_list = getComparedIOList(def.physical_graph.io_pin_list, def.physical_graph, result.def_power_ground_io_num);
  const std::vector<std::string> netlist_instance_name_list = getSortedKeyList(netlist.logical_graph.instance_map);
  const std::vector<std::string> def_instance_name_list = getSortedKeyList(def.physical_graph.instance_map);
  const std::vector<std::string> netlist_net_name_list = getSortedKeyList(netlist.net_map);
  const std::vector<std::string> def_net_name_list = getSortedKeyList(def.net_map);

  result.netlist_io_num = netlist_io_list.size();
  result.def_io_num = def_io_list.size();
  result.netlist_instance_num = netlist_instance_name_list.size();
  result.def_instance_num = def_instance_name_list.size();
  result.netlist_net_num = netlist_net_name_list.size();
  result.def_net_num = def_net_name_list.size();

  const std::vector<std::string> missing_io_list = getDifference(netlist_io_list, def_io_list);
  const std::vector<std::string> unexpected_io_list = getDifference(def_io_list, netlist_io_list);
  result.missing_io_num = missing_io_list.size();
  result.unexpected_io_num = unexpected_io_list.size();
  for (const std::string& io_pin_name : missing_io_list) {
    result.violation_list.push_back({"MissingIO", "", {io_pin_name}, {}});
  }
  for (const std::string& io_pin_name : unexpected_io_list) {
    result.violation_list.push_back({"UnexpectedIO", "", {io_pin_name}, {}});
  }

  const std::vector<std::string> missing_instance_name_list = getDifference(netlist_instance_name_list, def_instance_name_list);
  const std::vector<std::string> unexpected_instance_name_list = getDifference(def_instance_name_list, netlist_instance_name_list);
  result.missing_instance_num = missing_instance_name_list.size();
  result.unexpected_instance_num = unexpected_instance_name_list.size();
  for (const std::string& instance_name : missing_instance_name_list) {
    Violation violation;
    violation.type = "MissingInstance";
    violation.instance_name = instance_name;
    result.violation_list.push_back(std::move(violation));
  }
  for (const std::string& instance_name : unexpected_instance_name_list) {
    Violation violation;
    violation.type = "UnexpectedInstance";
    violation.instance_name = instance_name;
    result.violation_list.push_back(std::move(violation));
  }

  const std::vector<std::string> missing_net_name_list = getDifference(netlist_net_name_list, def_net_name_list);
  const std::vector<std::string> unexpected_net_name_list = getDifference(def_net_name_list, netlist_net_name_list);
  result.missing_net_num = missing_net_name_list.size();
  result.unexpected_net_num = unexpected_net_name_list.size();
  for (const std::string& net_name : missing_net_name_list) {
    Violation violation;
    violation.type = "MissingNet";
    violation.net_name = net_name;
    violation.terminal_list = netlist.net_map.at(net_name).terminal_list;
    result.violation_list.push_back(std::move(violation));
  }
  for (const std::string& net_name : unexpected_net_name_list) {
    Violation violation;
    violation.type = "UnexpectedNet";
    violation.net_name = net_name;
    violation.terminal_list = def.net_map.at(net_name).terminal_list;
    result.violation_list.push_back(std::move(violation));
  }
  for (const std::string& net_name : netlist_net_name_list) {
    auto def_net_iter = def.net_map.find(net_name);
    if (def_net_iter == def.net_map.end()) {
      continue;
    }
    const std::vector<std::string> netlist_pin_list = getSortedUniqueStringList(netlist.net_map.at(net_name).terminal_list);
    const std::vector<std::string> def_pin_list = getSortedUniqueStringList(def_net_iter->second.terminal_list);
    if (netlist_pin_list == def_pin_list) {
      continue;
    }
    result.net_pin_mismatch_num++;
    Violation violation;
    violation.type = "NetPinMismatch";
    violation.net_name = net_name;
    for (const std::string& pin_name : getDifference(netlist_pin_list, def_pin_list)) {
      violation.terminal_list.push_back("NETLIST/" + pin_name);
    }
    for (const std::string& pin_name : getDifference(def_pin_list, netlist_pin_list)) {
      violation.terminal_list.push_back("DEF/" + pin_name);
    }
    result.violation_list.push_back(std::move(violation));
  }

  std::vector<RoutingCheck> routing_check_list(def_net_name_list.size());
#pragma omp parallel for schedule(dynamic)
  for (int64_t net_idx = 0; net_idx < static_cast<int64_t>(def_net_name_list.size()); net_idx++) {
    const std::string& net_name = def_net_name_list[static_cast<size_t>(net_idx)];
    const Net& net = def.net_map.at(net_name);
    auto routing_graph_iter = def.physical_graph.net_routing_graph_map.find(net_name);
    const NetRoutingGraph* routing_graph = routing_graph_iter == def.physical_graph.net_routing_graph_map.end()
                                                  ? nullptr
                                                  : &routing_graph_iter->second;
    routing_check_list[static_cast<size_t>(net_idx)] = checkNetRoutingConnectivity(net, routing_graph);
  }
  result.routing_checked_net_num = routing_check_list.size();
  for (RoutingCheck& routing_check : routing_check_list) {
    if (routing_check.connected) {
      result.routing_connected_net_num++;
      continue;
    }
    if (!routing_check.missing_driver) {
      result.routing_open_net_num++;
      result.routing_open_load_pin_num += routing_check.disconnected_terminal_list.size();
    }
    result.routing_missing_driver_num += routing_check.missing_driver;
    Violation violation;
    violation.type = routing_check.missing_driver ? "RoutingDriverMissing" : "RoutingOpen";
    violation.net_name = std::move(routing_check.net_name);
    violation.driver_terminal_name = std::move(routing_check.driver_terminal_name);
    violation.terminal_list = std::move(routing_check.disconnected_terminal_list);
    violation.shape_list = std::move(routing_check.disconnected_shape_list);
    result.violation_list.push_back(std::move(violation));
  }

  std::vector<uint64_t> short_component_id_list;
  short_component_id_list.reserve(def.physical_graph.component_net_map.size());
  for (const auto& [component_id, net_name_list] : def.physical_graph.component_net_map) {
    if (getSortedUniqueStringList(net_name_list).size() > 1) {
      short_component_id_list.push_back(component_id);
    }
  }
  std::sort(short_component_id_list.begin(), short_component_id_list.end());
  for (uint64_t component_id : short_component_id_list) {
    Violation violation;
    violation.type = "RoutingShort";
    violation.component_id_list.push_back(component_id);
    violation.related_net_name_list = getSortedUniqueStringList(def.physical_graph.component_net_map.at(component_id));
    result.routing_short_component_num++;
    result.violation_list.push_back(std::move(violation));
  }

  result.supply_point_list = getSupplyPointList(def.physical_graph);
  for (const SupplyPoint& supply_point : result.supply_point_list) {
    if (supply_point.is_power) {
      result.power_supply_point_num++;
    } else {
      result.ground_supply_point_num++;
    }
  }
  checkSupplyConnectivity(result, def.physical_graph, def.physical_graph.power_instance_pin_net_map, true);
  checkSupplyConnectivity(result, def.physical_graph, def.physical_graph.ground_instance_pin_net_map, false);
  LVSLOG.info(Loc::current(), "Completed", monitor.getStatsInfo());
  return result;
}

#endif

}  // namespace ilvs
