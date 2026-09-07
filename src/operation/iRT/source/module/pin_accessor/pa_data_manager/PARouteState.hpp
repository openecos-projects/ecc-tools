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
  std::vector<PANode*>& get_source_node_list() { return _source_node_list; }
  std::vector<PANode*>& get_target_node_list() { return _target_node_list; }
  std::vector<Segment<LayerCoord>>& get_routing_segment_list() { return _routing_segment_list; }
  std::map<PANode*, AccessPoint*>& get_source_node_access_point_map() { return _source_node_access_point_map; }
  void set_curr_route_task(PATask* curr_route_task) { _curr_route_task = curr_route_task; }
  // single path
  OpenQueue<PANode>& get_open_queue() { return _open_queue; }
  std::vector<PANode*>& get_single_path_visited_node_list() { return _single_path_visited_node_list; }
  PANode* get_path_head_node() { return _path_head_node; }
  void set_path_head_node(PANode* path_head_node) { _path_head_node = path_head_node; }

  void resetPath()
  {
    get_open_queue().clear();
    std::vector<PANode*>& single_path_visited_node_list = get_single_path_visited_node_list();
    for (PANode* visited_node : single_path_visited_node_list) {
      visited_node->set_state(PANodeState::kNone);
      visited_node->set_parent_node(nullptr);
      visited_node->set_parent_via_master_idx(ViaMasterIdx());
      visited_node->set_known_cost(0);
      visited_node->set_estimated_cost(0);
    }
    single_path_visited_node_list.clear();

    set_path_head_node(nullptr);
  }
  void resetTask()
  {
    set_curr_route_task(nullptr);
    get_source_node_list().clear();
    get_target_node_list().clear();
    get_routing_segment_list().clear();
    get_source_node_access_point_map().clear();
  }
  void release()
  {
    get_open_queue().release();
    *this = PARouteState();
  }

 private:
  // single task
  PATask* _curr_route_task = nullptr;
  // One multi-source search connects the AP group to the target group.
  std::vector<PANode*> _source_node_list;
  std::vector<PANode*> _target_node_list;
  std::vector<Segment<LayerCoord>> _routing_segment_list;
  std::map<PANode*, AccessPoint*> _source_node_access_point_map;
  // single path
  OpenQueue<PANode> _open_queue;
  std::vector<PANode*> _single_path_visited_node_list;
  PANode* _path_head_node = nullptr;
};

}  // namespace irt
