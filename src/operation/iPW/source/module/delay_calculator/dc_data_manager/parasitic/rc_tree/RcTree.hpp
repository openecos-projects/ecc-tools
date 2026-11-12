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
// See the Mulan PSL v2 for more details.
// ***************************************************************************************
#pragma once

#include <cstdint>
#include <map>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "AnalysisType.hpp"
#include "TransType.hpp"

namespace ipw {

class ParasiticNet;
class ParasiticDmpModel;

// RC 树的通用处理：树展平、Pi 降阶（DMP 与 Arnoldi 共用）。
// 图结构（邻接表 / 节点电容 / pin 归属）由 ParasiticGraph 按 net 维护，见 ParasiticGraph.hpp。
void flattenRcTree(
    ParasiticNet& parasitic_net,
    uint32_t source_node_id,
    AnalysisType analysis_type,
    TransType trans_type,
    std::vector<uint32_t>& node_id_list,
    std::vector<int32_t>& parent_idx_list,
    std::vector<double>& resistance_list,
    std::vector<double>& capacitance_list
);
void buildPiModel(
    ParasiticDmpModel& dmp_model,
    std::vector<int32_t>& parent_idx_list,
    std::vector<double>& resistance_list,
    std::vector<double>& capacitance_list
);

}  // namespace ipw
