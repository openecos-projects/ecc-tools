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

#include "AccessPoint.hpp"
#include "DRNode.hpp"
#include "DRTask.hpp"
#include "OpenQueue.hpp"

namespace irt {

class DRRouteState
{
 public:
  DRRouteState() = default;
  DRRouteState(const DRRouteState&) = default;
  DRRouteState(DRRouteState&&) = default;
  DRRouteState& operator=(const DRRouteState&) = default;
  DRRouteState& operator=(DRRouteState&&) = default;
  ~DRRouteState() = default;
  // single task
  DRTask* get_curr_route_task() { return _curr_route_task; }
  std::vector<std::vector<DRNode*>>& get_start_node_list_list() { return _start_node_list_list; }
  std::vector<std::vector<DRNode*>>& get_end_node_list_list() { return _end_node_list_list; }
  std::vector<DRNode*>& get_path_node_list() { return _path_node_list; }
  std::vector<DRNode*>& get_single_task_visited_node_list() { return _single_task_visited_node_list; }
  std::vector<Segment<LayerCoord>>& get_routing_segment_list() { return _routing_segment_list; }
  std::map<DRNode*, AccessPoint*>& get_source_node_access_point_map() { return _source_node_access_point_map; }
  void set_curr_route_task(DRTask* curr_route_task) { _curr_route_task = curr_route_task; }
  // single path
  OpenQueue<DRNode>& get_open_queue() { return _open_queue; }
  std::vector<DRNode*>& get_single_path_visited_node_list() { return _single_path_visited_node_list; }
  DRNode* get_path_head_node() { return _path_head_node; }
  int32_t get_end_node_list_idx() const { return _end_node_list_idx; }
  void set_path_head_node(DRNode* path_head_node) { _path_head_node = path_head_node; }
  void set_end_node_list_idx(const int32_t end_node_list_idx) { _end_node_list_idx = end_node_list_idx; }

  void resetPath()
  {
    get_open_queue().clear();
    std::vector<DRNode*>& single_path_visited_node_list = get_single_path_visited_node_list();
    for (DRNode* visited_node : single_path_visited_node_list) {
      visited_node->set_state(DRNodeState::kNone);
      visited_node->set_parent_node(nullptr);
      visited_node->set_parent_via_master_idx(ViaMasterIdx());
      visited_node->set_known_cost(0);
      visited_node->set_estimated_cost(0);
    }
    single_path_visited_node_list.clear();

    set_path_head_node(nullptr);
    set_end_node_list_idx(-1);
  }
  void resetTask()
  {
    set_curr_route_task(nullptr);
    get_start_node_list_list().clear();
    get_end_node_list_list().clear();
    get_path_node_list().clear();
    for (DRNode* single_task_visited_node : get_single_task_visited_node_list()) {
      single_task_visited_node->clearDirection();
    }
    get_single_task_visited_node_list().clear();
    get_routing_segment_list().clear();
    get_source_node_access_point_map().clear();
  }
  void release()
  {
    _open_queue.release();
    *this = DRRouteState();
  }

 private:
  // single task
  DRTask* _curr_route_task = nullptr;
  std::vector<std::vector<DRNode*>> _start_node_list_list;
  std::vector<std::vector<DRNode*>> _end_node_list_list;
  std::vector<DRNode*> _path_node_list;
  std::vector<DRNode*> _single_task_visited_node_list;
  std::vector<Segment<LayerCoord>> _routing_segment_list;
  std::map<DRNode*, AccessPoint*> _source_node_access_point_map;
  // single path
  OpenQueue<DRNode> _open_queue;
  std::vector<DRNode*> _single_path_visited_node_list;
  DRNode* _path_head_node = nullptr;
  int32_t _end_node_list_idx = -1;
};

}  // namespace irt
