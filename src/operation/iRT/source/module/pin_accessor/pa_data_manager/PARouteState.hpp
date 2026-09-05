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
#pragma once

#include "OpenQueue.hpp"
#include "PANode.hpp"
#include "PATask.hpp"

namespace irt {

class PARouteState
{
 public:
  PARouteState() = default;
  PARouteState(const PARouteState&) = default;
  PARouteState(PARouteState&&) = default;
  PARouteState& operator=(const PARouteState&) = default;
  PARouteState& operator=(PARouteState&&) = default;
  ~PARouteState() = default;
  // single task
  PATask* get_curr_route_task() { return _curr_route_task; }
  std::vector<std::vector<PANode*>>& get_start_node_list_list() { return _start_node_list_list; }
  std::vector<std::vector<PANode*>>& get_end_node_list_list() { return _end_node_list_list; }
  std::vector<PANode*>& get_path_node_list() { return _path_node_list; }
  std::vector<PANode*>& get_single_task_visited_node_list() { return _single_task_visited_node_list; }
  std::vector<Segment<LayerCoord>>& get_routing_segment_list() { return _routing_segment_list; }
  std::map<PANode*, AccessPoint*>& get_source_node_access_point_map() { return _source_node_access_point_map; }
  void set_curr_route_task(PATask* curr_route_task) { _curr_route_task = curr_route_task; }
  void set_start_node_list_list(const std::vector<std::vector<PANode*>>& start_node_list_list) { _start_node_list_list = start_node_list_list; }
  void set_end_node_list_list(const std::vector<std::vector<PANode*>>& end_node_list_list) { _end_node_list_list = end_node_list_list; }
  void set_path_node_list(const std::vector<PANode*>& path_node_list) { _path_node_list = path_node_list; }
  void set_single_task_visited_node_list(const std::vector<PANode*>& single_task_visited_node_list)
  {
    _single_task_visited_node_list = single_task_visited_node_list;
  }
  void set_routing_segment_list(const std::vector<Segment<LayerCoord>>& routing_segment_list) { _routing_segment_list = routing_segment_list; }
  // single path
  OpenQueue<PANode>& get_open_queue() { return _open_queue; }
  std::vector<PANode*>& get_single_path_visited_node_list() { return _single_path_visited_node_list; }
  PANode* get_path_head_node() { return _path_head_node; }
  int32_t get_end_node_list_idx() const { return _end_node_list_idx; }
  void set_open_queue(const OpenQueue<PANode>& open_queue) { _open_queue = open_queue; }
  void set_single_path_visited_node_list(const std::vector<PANode*>& single_path_visited_node_list)
  {
    _single_path_visited_node_list = single_path_visited_node_list;
  }
  void set_path_head_node(PANode* path_head_node) { _path_head_node = path_head_node; }
  void set_end_node_list_idx(const int32_t end_node_list_idx) { _end_node_list_idx = end_node_list_idx; }

 private:
  // single task
  PATask* _curr_route_task = nullptr;
  std::vector<std::vector<PANode*>> _start_node_list_list;
  std::vector<std::vector<PANode*>> _end_node_list_list;
  std::vector<PANode*> _path_node_list;
  std::vector<PANode*> _single_task_visited_node_list;
  std::vector<Segment<LayerCoord>> _routing_segment_list;
  std::map<PANode*, AccessPoint*> _source_node_access_point_map;
  // single path
  OpenQueue<PANode> _open_queue;
  std::vector<PANode*> _single_path_visited_node_list;
  PANode* _path_head_node = nullptr;
  int32_t _end_node_list_idx = -1;
};

}  // namespace irt
