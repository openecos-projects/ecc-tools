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
#include <algorithm>
#include <array>
#include <cassert>
#include <cstdlib>
#include <string>
#include <tuple>
#include <vector>

#include "DataManager.hpp"
#include "CmpEnvTopoEdgeByCoordASC.hpp"
#include "EnvTrackOverlapMerger.hpp"
#include "Logger.hpp"
#include "TopoBuilder.hpp"
#include "Utility.hpp"

namespace {

struct NodeSnapshot
{
  int32_t layer = -1;
  int32_t x = 0;
  int32_t y = 0;
  std::array<int32_t, 4> shape{};
  std::string pin_name;

  bool operator==(const NodeSnapshot&) const = default;
};

struct EdgeSnapshot
{
  int32_t layer = -1;
  std::array<int32_t, 3> first{};
  std::array<int32_t, 3> second{};
  std::array<int32_t, 4> shape{};
  std::string via_name;

  bool operator==(const EdgeSnapshot&) const = default;
};

struct TopologySnapshot
{
  std::vector<NodeSnapshot> nodes;
  std::vector<EdgeSnapshot> edges;

  bool operator==(const TopologySnapshot&) const = default;
};

std::array<int32_t, 4> rectSnapshot(const GTLRectInt& rectangle)
{
  return {RCXUTIL.minX(rectangle), RCXUTIL.minY(rectangle), RCXUTIL.maxX(rectangle), RCXUTIL.maxY(rectangle)};
}

std::array<int32_t, 3> nodeKey(ircx::TopoNode& node)
{
  return {node.get_layer_idx(), RCXUTIL.x(node.get_point()), RCXUTIL.y(node.get_point())};
}

ircx::Segment makeSegment(const int32_t start_x, const int32_t end_x)
{
  ircx::Segment segment;
  segment.set_layer_idx(1);
  segment.set_start_point(GTLPointInt(start_x, 5));
  segment.set_end_point(GTLPointInt(end_x, 5));
  segment.set_shape(GTLRectInt(std::min(start_x, end_x), 0, std::max(start_x, end_x), 10));
  return segment;
}

ircx::Net makeEquivalentNet(const bool reverse_and_reorder)
{
  ircx::Net net;
  net.set_net_idx(0);
  net.set_net_name("order_invariant");

  ircx::Pin pin;
  pin.set_pin_name("PORT");
  pin.set_is_driver(true);
  pin.set_direction(ircx::Direction::kOutput);
  pin.get_layer_shape_list().emplace_back(1, GTLRectInt(0, 0, 100, 10));
  net.get_pin_list().push_back(std::move(pin));

  if (reverse_and_reorder) {
    net.get_segment_list().push_back(makeSegment(150, 90));
    net.get_segment_list().push_back(makeSegment(90, 10));
  } else {
    net.get_segment_list().push_back(makeSegment(10, 90));
    net.get_segment_list().push_back(makeSegment(90, 150));
  }
  return net;
}

TopologySnapshot buildTopology(ircx::Net net)
{
  auto& database = RCXDM.getDatabase();
  database = ircx::Database{};
  database.get_layout_data().get_net_list().push_back(std::move(net));
  RCXDM.getConfig().thread_number = 1;

  ircx::TopoBuilder::initInst();
  ircx::TopoBuilder::getInst().build();
  ircx::TopoBuilder::destroyInst();

  TopologySnapshot snapshot;
  auto& topo_pool = database.get_topo_pool();
  const auto nodes = topo_pool.get_net_node_list(0);
  const auto edges = topo_pool.get_net_edge_list(0);
  snapshot.nodes.reserve(nodes.size());
  snapshot.edges.reserve(edges.size());

  for (auto& node : nodes) {
    snapshot.nodes.push_back(NodeSnapshot{.layer = node.get_layer_idx(),
                                          .x = RCXUTIL.x(node.get_point()),
                                          .y = RCXUTIL.y(node.get_point()),
                                          .shape = rectSnapshot(node.get_shape()),
                                          .pin_name = node.get_pin_name()});
  }
  for (auto& edge : edges) {
    auto& first = topo_pool.get_node(edge.get_start_node_idx());
    auto& second = topo_pool.get_node(edge.get_end_node_idx());
    auto first_key = nodeKey(first);
    auto second_key = nodeKey(second);
    if (second_key < first_key) {
      std::swap(first_key, second_key);
    }
    snapshot.edges.push_back(EdgeSnapshot{.layer = edge.get_layer_idx(),
                                          .first = first_key,
                                          .second = second_key,
                                          .shape = rectSnapshot(edge.get_shape()),
                                          .via_name = edge.get_via_name()});
  }

  std::sort(snapshot.nodes.begin(), snapshot.nodes.end(), [](const NodeSnapshot& lhs, const NodeSnapshot& rhs) {
    return std::tie(lhs.layer, lhs.x, lhs.y, lhs.pin_name, lhs.shape) < std::tie(rhs.layer, rhs.x, rhs.y, rhs.pin_name, rhs.shape);
  });
  std::sort(snapshot.edges.begin(), snapshot.edges.end(), [](const EdgeSnapshot& lhs, const EdgeSnapshot& rhs) {
    return std::tie(lhs.layer, lhs.first, lhs.second, lhs.shape, lhs.via_name)
           < std::tie(rhs.layer, rhs.first, rhs.second, rhs.shape, rhs.via_name);
  });
  return snapshot;
}

void testEquivalentSegmentPermutationBuildsSameTopology()
{
  const TopologySnapshot forward = buildTopology(makeEquivalentNet(false));
  const TopologySnapshot reversed = buildTopology(makeEquivalentNet(true));

  assert(forward == reversed);
  const auto port = std::find_if(forward.nodes.begin(), forward.nodes.end(), [](const NodeSnapshot& node) { return node.pin_name == "PORT"; });
  assert(port != forward.nodes.end());
  assert(port->x == 10);
  assert(port->y == 5);
  assert(forward.edges.size() == 2);
}

void testCoincidentEdgesUseStableNetRank()
{
  ircx::TopoEdge alphabetically_first(99);
  ircx::TopoEdge alphabetically_last(0);
  alphabetically_first.set_shape(GTLRectInt(0, 0, 100, 10));
  alphabetically_last.set_shape(GTLRectInt(0, 0, 100, 10));
  alphabetically_first.set_stable_net_rank(0);
  alphabetically_last.set_stable_net_rank(1);

  ircx::CmpEnvTopoEdgeByCoordASC comparator;
  assert(comparator(&alphabetically_first, &alphabetically_last));
  assert(!comparator(&alphabetically_last, &alphabetically_first));

  ircx::EnvTrackOverlapMerger merger;
  std::vector<ircx::EdgeEnvInterval> intervals;
  merger.merge(0,
               100,
               {{0, 100, 10, &alphabetically_last}, {0, 100, 10, &alphabetically_first}},
               {{0, 100, 10, nullptr}},
               intervals);
  assert(intervals.size() == 1);
  assert(intervals.front().get_lower_adjacent_edge() == &alphabetically_first);
}

}  // namespace

int main()
{
  ircx::Logger::initInst();
  ircx::DataManager::initInst();

  testEquivalentSegmentPermutationBuildsSameTopology();
  testCoincidentEdgesUseStableNetRank();

  ircx::TopoBuilder::destroyInst();
  ircx::DataManager::destroyInst();
  ircx::Logger::destroyInst();
  return EXIT_SUCCESS;
}
