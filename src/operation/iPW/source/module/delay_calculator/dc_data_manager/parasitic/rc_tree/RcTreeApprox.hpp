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
// MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE.
// See the Mulan PSL v2 for more details.
// ***************************************************************************************
#pragma once

#include <string>

#include <cstdint>

#include "AnalysisType.hpp"
#include "ParasiticCornerState.hpp"
#include "ParasiticGraph.hpp"
#include "TransType.hpp"

namespace ipw {

class ParasiticNet;

// 老 RC 树近似：逐节点求负载/延时/二阶矩冲量（当没有可用降阶模型时的兜底口径）。
// 按 ParasiticGraph 的节点 id 操作稠密数组；四个遍历的顺序与旧字符串 map 版本逐位一致。
// graph / corner_state 由调用方解析后传入，避免每次调用重新探测 net 名。
void buildParasiticDelayMap(
    const ParasiticGraph& graph,
    ParasiticCornerState& corner_state,
    uint32_t source_node_id,
    AnalysisType analysis_type,
    TransType trans_type
);

}  // namespace ipw
