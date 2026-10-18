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
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "AnalysisType.hpp"
#include "ParasiticCornerState.hpp"
#include "PWHeader.hpp"
#include "TransType.hpp"

namespace ipw {

class ParasiticNet;

// 无效的寄生节点 id。
inline constexpr uint32_t kInvalidParasiticNode = 0xFFFFFFFFu;

// RC 图的一条边：邻接表按 buildParasiticResistorMap 的原插入序建立
// （每条电阻 source→sink、再 sink→source），保证四个递归遍历的求和顺序不变。
struct ParasiticGraphEdge
{
  uint32_t node = kInvalidParasiticNode;
  // 原始电阻值（未乘 1E-3），与旧 resistor_map 的存放口径一致；
  // 调用方保留各自原来的 ×1E-3 位置，避免改变浮点舍入。
  double resistance = 0.0;
};

// 一条 net 的寄生 RC 图：按 net 建一次、之后只读。
// 节点 id 的分配顺序 = ParasiticNet::node_capacitance_map 的迭代序，再追加只出现在
// resistor_list 里的节点；因此"按 id 求和"与旧代码"按 map 迭代求和"顺序一致。
struct ParasiticGraph
{
  std::vector<const std::string*> node_name;   // id -> 节点名（指向 ParasiticNet 内的字符串，零拷贝）
  std::vector<std::vector<ParasiticGraphEdge>> adjacency;
  std::vector<double> node_capacitance;        // id -> 节点自身电容（无 = 0.0）
  std::vector<uint32_t> node_pin_index;        // id -> pin 索引；非 pin 节点 = kInvalidParasiticNode
  std::vector<const std::string*> node_pin_name;  // id -> 命中的 pin 名（非 pin = nullptr）
  uint32_t capacitance_node_num = 0;           // 前多少个 id 来自 node_capacitance_map

  uint32_t size() const { return static_cast<uint32_t>(node_name.size()); }

  uint32_t find(const std::string& name) const
  {
    auto iter = node_id_map.find(std::string_view(name));
    return iter == node_id_map.end() ? kInvalidParasiticNode : iter->second;
  }

  bool is_pin(const uint32_t node_id) const
  {
    return node_id < node_pin_index.size() && node_pin_index[node_id] != kInvalidParasiticNode;
  }

  // 等价于旧的 getPinNameByParasiticNodeName()：命中 pin 时返回 pin 名，否则返回节点名。
  const std::string& pin_name_of(const uint32_t node_id) const
  {
    if (node_id < node_pin_name.size() && node_pin_name[node_id] != nullptr) {
      return *node_pin_name[node_id];
    }
    return *node_name[node_id];
  }

  std::unordered_map<std::string_view, uint32_t> node_id_map;
};

// 取（必要时构建）net 的 RC 图；图按 net 缓存在 thread_local DelayCache 里。
const ParasiticGraph& getParasiticGraph(ParasiticNet& parasitic_net);

// pin 名 -> 节点 id；同时接受 "inst:PORT" 与 SPEF 的 "inst/PORT" 两种写法。
// 找不到返回 kInvalidParasiticNode（对应旧 getParasiticNodeName() 返回空串）。
uint32_t getParasiticNodeId(ParasiticNet& parasitic_net, const std::string& pin_name);

// 调用方已持有 graph 时用这个重载：省掉 getParasiticNodeId 内部重复的图探测。
uint32_t getParasiticNodeId(const ParasiticGraph& graph, const std::string& pin_name);

// 该 corner 下每个节点的 pin 电容表（按节点 id；非 pin 节点为 0.0）。
// 每 (net, analysis, trans) 只计算一次，替代原来每节点重建的五级字符串查找。
const std::vector<double>& getParasiticPinCapList(ParasiticNet& parasitic_net, AnalysisType analysis_type, TransType trans_type);

// 同上，但用调用方已经取到的 graph / corner state，避免重复探测 net 名。
const std::vector<double>& getParasiticPinCapList(
    const ParasiticGraph& graph,
    ParasiticCornerState& state,
    AnalysisType analysis_type,
    TransType trans_type
);

// 节点负载 = 节点自身电容 + 该节点的 pin 电容（等价于旧 getParasiticNodeLoad()）。
inline double parasiticNodeLoad(
    const ParasiticGraph& graph,
    const std::vector<double>& pin_cap_list,
    const uint32_t node_id
)
{
  return graph.node_capacitance[node_id] + pin_cap_list[node_id];
}

}  // namespace ipw
