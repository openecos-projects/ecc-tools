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
// See the Mulan PSL v2 for more details.
// ***************************************************************************************
#pragma once

#include "AnalysisType.hpp"
#include "STAHeader.hpp"
#include "TransType.hpp"

namespace ista {

class SDFDelay
{
 public:
  SDFDelay() = default;
  ~SDFDelay() = default;
  // getter
  std::optional<double>& get_rise_min_delay() { return _rise_min_delay; }
  std::optional<double>& get_rise_max_delay() { return _rise_max_delay; }
  std::optional<double>& get_fall_min_delay() { return _fall_min_delay; }
  std::optional<double>& get_fall_max_delay() { return _fall_max_delay; }
  // setter
  void set_rise_min_delay(const std::optional<double>& rise_min_delay) { _rise_min_delay = rise_min_delay; }
  void set_rise_max_delay(const std::optional<double>& rise_max_delay) { _rise_max_delay = rise_max_delay; }
  void set_fall_min_delay(const std::optional<double>& fall_min_delay) { _fall_min_delay = fall_min_delay; }
  void set_fall_max_delay(const std::optional<double>& fall_max_delay) { _fall_max_delay = fall_max_delay; }
  // function
  void update(AnalysisType analysis_type, TransType trans_type, double delay)
  {
    std::optional<double>* target_delay = nullptr;
    if (analysis_type == AnalysisType::kMin && trans_type == TransType::kRise) {
      target_delay = &_rise_min_delay;
    } else if (analysis_type == AnalysisType::kMax && trans_type == TransType::kRise) {
      target_delay = &_rise_max_delay;
    } else if (analysis_type == AnalysisType::kMin && trans_type == TransType::kFall) {
      target_delay = &_fall_min_delay;
    } else if (analysis_type == AnalysisType::kMax && trans_type == TransType::kFall) {
      target_delay = &_fall_max_delay;
    }
    if (target_delay == nullptr || !(*target_delay)) {
      if (target_delay != nullptr) {
        *target_delay = delay;
      }
      return;
    }
    if ((analysis_type == AnalysisType::kMin && delay < **target_delay)
        || (analysis_type == AnalysisType::kMax && delay > **target_delay)) {
      *target_delay = delay;
    }
  }

 private:
  std::optional<double> _rise_min_delay;
  std::optional<double> _rise_max_delay;
  std::optional<double> _fall_min_delay;
  std::optional<double> _fall_max_delay;
};

}  // namespace ista
