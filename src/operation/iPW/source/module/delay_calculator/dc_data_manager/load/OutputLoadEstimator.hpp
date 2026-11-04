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

#include <string>

#include "AnalysisType.hpp"
#include "TransType.hpp"

namespace ipw {

class Arc;
class Net;
class ParasiticNet;
class ParasiticGraph;

// 负载估算：管脚电容、网表负载与寄生网络的输出负载
double getPinCapacitance(std::string& pin_name, AnalysisType analysis_type, TransType trans_type);
// 按 pin 索引取电容：用 PinStaticContext 的预解析结果，省掉 pin→instance→cell→port 的字符串查找链。
double getPinCapacitance(uint32_t pin_index, AnalysisType analysis_type, TransType trans_type);
// 节点负载：节点自身电容 + 该节点的 pin 电容（旧按名字版本的索引化等价物）。
double getParasiticNodeLoad(ParasiticNet& parasitic_net, uint32_t node_id, AnalysisType analysis_type, TransType trans_type);

double getArcOutputLoad(Arc& arc, AnalysisType analysis_type, TransType output_trans_type);
double getOutputPinLoad(std::string& output_pin, AnalysisType analysis_type, TransType output_trans_type);
double getNetOutputLoad(
    const std::string& net_name,
    const std::string& driver_pin,
    Net& net,
    AnalysisType analysis_type,
    TransType output_trans_type
);
double getParasiticNetOutputLoad(
    const std::string& driver_pin,
    ParasiticNet& parasitic_net,
    AnalysisType analysis_type,
    TransType trans_type
);
double getParasiticTotalLoad(ParasiticNet& parasitic_net, AnalysisType analysis_type, TransType trans_type);
// 同上，但复用调用方已取到的 RC 图，省掉一次按 net 名的图探测。
double getParasiticTotalLoad(
    const ParasiticGraph& graph,
    ParasiticNet& parasitic_net,
    AnalysisType analysis_type,
    TransType trans_type
);

}  // namespace ipw
