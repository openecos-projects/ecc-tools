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

#include "PRComParam.hpp"
#include "PRNet.hpp"
#include "RTHeader.hpp"

namespace irt {

class PRModel
{
 public:
  PRModel() = default;
  ~PRModel() = default;
  // getter
  std::vector<PRNet>& get_pr_net_list() { return _pr_net_list; }
  PRComParam& get_pr_com_param() { return _pr_com_param; }
  std::vector<PRNet*>& get_pr_task_list() { return _pr_task_list; }
  std::map<int32_t, std::vector<Segment<LayerCoord>>>& get_net_global_result_map() { return _net_global_result_map; }
  // setter
  void set_pr_net_list(const std::vector<PRNet>& pr_net_list) { _pr_net_list = pr_net_list; }
  void set_pr_com_param(const PRComParam& pr_com_param) { _pr_com_param = pr_com_param; }

  // single task
  PRNet* get_curr_pr_task() { return _curr_pr_task; }
  void set_curr_pr_task(PRNet* curr_pr_task) { _curr_pr_task = curr_pr_task; }

 private:
  std::vector<PRNet> _pr_net_list;
  PRComParam _pr_com_param;
  std::vector<PRNet*> _pr_task_list;
  std::map<int32_t, std::vector<Segment<LayerCoord>>> _net_global_result_map;
  PRNet* _curr_pr_task = nullptr;
};

}  // namespace irt
