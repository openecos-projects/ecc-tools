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
/**
 * @file StaDelayPropagation.h
 * @author simin tao (taosm@pcl.ac.cn)
 * @brief The class of delay propagation.
 * @version 0.1
 * @date 2021-04-10
 */
#pragma once

#include "StaFunc.hh"
// DEBUG
#include <mutex>

struct ArcDebugInfo {
    std::string inst_name;
    std::string from_pin;
    std::string to_pin;
    std::string analysis_mode;
    std::string transition;
    int timing_sense;
    double in_slew_ns;
    double load_cap;
    double delay_ns;
};
class ArcDebugDataManager {
public:
    // 获取单例
    static ArcDebugDataManager& getInstance() {
        static ArcDebugDataManager instance;
        return instance;
    }

    // 添加信息（线程安全）
    void addArcInfo(const ArcDebugInfo& info) {
        // std::lock_guard 会在构造时自动加锁，在析构时（离开作用域）自动解锁
        std::lock_guard<std::mutex> lock(mtx_);
        arc_data_.push_back(info);
    }

    // 获取所有信息
    const std::vector<ArcDebugInfo>& getArcInfo() const {
        return arc_data_;
    }

    // 清空数据，为下一次运行做准备
    void clear() {
        std::lock_guard<std::mutex> lock(mtx_);
        arc_data_.clear();
    }

private:
    ArcDebugDataManager() {} // 私有构造函数
    std::vector<ArcDebugInfo> arc_data_;
    std::mutex mtx_; // 关键：成员互斥锁
};
// DEBUG
namespace ista {

class StaArc;
class StaVertex;
class StaGraph;

class StaDelayPropagation : public StaFunc {
 public:
  unsigned operator()(StaArc* the_arc);
  unsigned operator()(StaVertex* the_vertex);
  unsigned operator()(StaGraph* the_graph);

  AnalysisMode get_analysis_mode() override { return AnalysisMode::kMaxMin; }
};

}  // namespace ista
