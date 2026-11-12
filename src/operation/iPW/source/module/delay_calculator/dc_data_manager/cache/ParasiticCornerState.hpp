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

#include <cstdint>
#include <vector>

namespace ipw {

// 一条 net 在一个 (analysis, trans) corner 下的逐节点数值：node_load / node_delay /
// node_impulse 是旧的四层嵌套 map 的等价物，但按节点 id 存放（id 由 ParasiticGraph
// 分配），并附带写标记位以复刻 map::count 语义。
//
// 单独放一个头文件：持有 graph + corner state 的调用方（RcTreeApprox 等）需要在
// 不引入 DelayCache 的前提下按引用传递它。
struct ParasiticCornerState
{
  std::vector<double> node_load;
  std::vector<double> node_delay;
  std::vector<double> node_impulse;
  std::vector<double> load_delay;       // 中间量（每轮 build 前清零）
  std::vector<double> beta;             // 中间量（每轮 build 前清零）
  std::vector<double> node_pin_cap;     // 该 corner 下节点的 pin 电容
  std::vector<uint8_t> node_delay_valid;
  std::vector<uint8_t> node_impulse_valid;
  std::vector<uint8_t> visited_scratch;  // 四个遍历共用，避免每次调用分配
  uint32_t source_node_idx = UINT32_MAX;
  bool pin_cap_ready = false;
};

}  // namespace ipw
