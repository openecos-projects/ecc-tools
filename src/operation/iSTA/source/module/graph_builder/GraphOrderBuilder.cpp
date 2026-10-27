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
// WHETHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
// MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
//
// See the Mulan PSL v2 for more details.
// ***************************************************************************************
#include "GraphOrderBuilder.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <queue>
#include <utility>
#include <vector>

#include "Logger.hpp"
namespace ista {
namespace {
enum class GraphColor
{
  kNone,
  kGray,
  kBlack
};
bool isDisabled(const Arc& arc)
{
  return arc.get_is_disable_arc() || arc.get_is_loop_disable();
}
}  // namespace
void GraphOrderBuilder::build()
{
  cutLoops();
  sortVertices();
  checkComplete();
}
void GraphOrderBuilder::cutLoops()
{
  std::vector<GraphVertex>& vertices = _graph.vertices();
  std::vector<GraphColor> colors(vertices.size(), GraphColor::kNone);
  int32_t disabled_loop_num = 0;
  // Use graph vertices, not physical pin names: PAD receive and drive are independent.
  for (int32_t root = 0; root < static_cast<int32_t>(vertices.size()); ++root) {
    if (colors[root] != GraphColor::kNone) {
      continue;
    }
    std::vector<std::pair<int32_t, std::size_t>> stack{{root, 0}};
    colors[root] = GraphColor::kGray;
    while (!stack.empty()) {
      auto& [vertex_idx, next_arc_idx] = stack.back();
      std::vector<std::size_t>& outgoing = vertices[vertex_idx].get_outgoing_arc_list();
      if (next_arc_idx == outgoing.size()) {
        colors[vertex_idx] = GraphColor::kBlack;
        stack.pop_back();
        continue;
      }
      Arc& arc = _graph.arcs()[outgoing[next_arc_idx++]];
      if (isDisabled(arc)) {
        continue;
      }
      int32_t sink_idx = arc.get_sink_vertex_idx();
      if (colors[sink_idx] == GraphColor::kGray) {
        arc.set_is_loop_disable(true);
        ++disabled_loop_num;
        STALOG.warn(Loc::current(), "Combinational loop cut: ", arc.get_source_pin(), " -> ", arc.get_sink_pin());
      } else if (colors[sink_idx] == GraphColor::kNone) {
        colors[sink_idx] = GraphColor::kGray;
        stack.emplace_back(sink_idx, 0);
      }
    }
  }
  if (disabled_loop_num > 0) {
    STALOG.info(Loc::current(), "Break iSTA loop arcs: disabled_arcs=", disabled_loop_num);
  }
}

void GraphOrderBuilder::sortVertices()
{
  std::vector<GraphVertex>& vertices = _graph.vertices();
  std::vector<int32_t> indegree_list(vertices.size(), 0);
  std::queue<int32_t> vertex_queue;
  for (int32_t idx = 0; idx < static_cast<int32_t>(vertices.size()); ++idx) {
    for (std::size_t arc_idx : vertices[idx].get_incoming_arc_list()) {
      if (!isDisabled(_graph.arcs()[arc_idx])) {
        ++indegree_list[idx];
      }
    }
    if (indegree_list[idx] == 0) {
      vertices[idx].get_timing_point().set_level(1);
      vertex_queue.push(idx);
    }
  }
  _graph.vertexOrder().clear();
  _graph.timingOrder().clear();
  while (!vertex_queue.empty()) {
    int32_t vertex_idx = vertex_queue.front();
    vertex_queue.pop();
    GraphVertex& vertex = vertices[vertex_idx];
    _graph.vertexOrder().push_back(vertex_idx);
    _graph.timingOrder().push_back(vertex.get_pin_name());
    for (std::size_t arc_idx : vertex.get_outgoing_arc_list()) {
      Arc& arc = _graph.arcs()[arc_idx];
      if (isDisabled(arc)) {
        continue;
      }
      int32_t sink_idx = arc.get_sink_vertex_idx();
      TimingPoint& sink = vertices[sink_idx].get_timing_point();
      sink.set_level(std::max(sink.get_level(), vertex.get_timing_point().get_level() + 1));
      if (--indegree_list[sink_idx] == 0) {
        vertex_queue.push(sink_idx);
      }
    }
  }
}

void GraphOrderBuilder::checkComplete()
{
  if (_graph.vertexOrder().size() != _graph.vertices().size()) {
    STALOG.error(Loc::current(), "Incomplete graph order after loop cleanup.");
  }
}

}  // namespace ista
