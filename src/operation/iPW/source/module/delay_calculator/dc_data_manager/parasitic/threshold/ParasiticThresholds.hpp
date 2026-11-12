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

#include <cstdint>

#include "TransType.hpp"

namespace ipw {

class TimingArc;
class TimingCell;

// 阈值对齐：把 liberty 表里的阈值语义应用到寄生延时结果（DM​P 与 Arnoldi 共用）
double getNormalizedThreshold(double threshold);
void adjustParasiticLoadThreshold(
    TimingArc& timing_arc,
    const std::string& load_pin,
    TransType trans_type,
    double& wire_delay,
    double& load_slew
);
bool isParasiticLoadSlewCompatible(TimingArc& timing_arc, const std::string& load_pin, TransType trans_type);
// 接收端的 library cell：按 pin 索引取（PinStaticContext 已解析），或按名字取。
TimingCell* getThresholdTimingCell(uint32_t pin_index);
TimingCell* getThresholdTimingCell(const std::string& pin_name);
double getTimingCellSlewLowerThreshold(TimingCell& timing_cell, TransType trans_type);
double getTimingCellSlewUpperThreshold(TimingCell& timing_cell, TransType trans_type);
double getTimingCellInputThreshold(TimingCell& timing_cell, TransType trans_type);

}  // namespace ipw
