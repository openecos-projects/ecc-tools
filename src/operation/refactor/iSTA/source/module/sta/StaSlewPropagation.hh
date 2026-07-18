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
 * @file StaSlewPropagation.h
 * @author simin tao (taosm@pcl.ac.cn)
 * @brief The slew propagation from input port.
 * @version 0.1
 * @date 2021-04-08
 */
#pragma once

#include "StaFunc.hh"
// DEBUG
#include <mutex>

struct SlewKey {
    std::string pin_full_name;
    ista::AnalysisMode mode;
    ista::TransType trans;

    bool operator==(const SlewKey& other) const {
        return pin_full_name == other.pin_full_name && mode == other.mode && trans == other.trans;
    }
};

// 为SlewKey提供哈希函数，以便用于unordered_map
namespace std {
    template <>
    struct hash<SlewKey> {
        size_t operator()(const SlewKey& k) const {
            return hash<string>()(k.pin_full_name) ^ (hash<int>()(static_cast<int>(k.mode)) << 1) ^ (hash<int>()(static_cast<int>(k.trans)) << 2);
        }
    };
}

// 线程安全的“Slew银行”
class SlewDebugDataManager {
public:
    static SlewDebugDataManager& getInstance() {
        static SlewDebugDataManager instance;
        return instance;
    }

    void addSlew(const SlewKey& key, double slew_ns) {
        std::lock_guard<std::mutex> lock(mtx_);
        slew_data_[key] = slew_ns;
    }

    // 这个函数将用于在C++中直接获取整个map
    const std::unordered_map<SlewKey, double>& getData() const {
        return slew_data_;
    }

    void clear() {
        std::lock_guard<std::mutex> lock(mtx_);
        slew_data_.clear();
    }
private:
    SlewDebugDataManager() {}
    std::unordered_map<SlewKey, double> slew_data_;
    std::mutex mtx_;
};
// DEBUG
namespace ista {

/**
 * @brief The slew propagation for calculating the gate delay.
 *
 */
class StaSlewPropagation : public StaFunc {
 public:
  unsigned operator()(StaArc* the_arc) override;
  unsigned operator()(StaVertex* the_vertex) override;
  unsigned operator()(StaGraph* the_graph) override;

  AnalysisMode get_analysis_mode() override { return AnalysisMode::kMaxMin; }

  void set_propagate_output_port() { _propagate_output_port = true; }
  bool isPropagateOutputPort() { return _propagate_output_port; }

 private:
  bool _propagate_output_port = false;
};

}  // namespace ista
