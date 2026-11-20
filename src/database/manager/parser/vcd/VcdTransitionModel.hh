// ***************************************************************************************
// Copyright (c) 2023-2025 Peng Cheng Laboratory
// Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences
// Copyright (c) 2023-2025 Beijing Institute of Open Source Chip
//
// iEDA is licensed under Mulan PSL v2.
// You can use this software according to the terms and conditions of the Mulan PSL v2.
// You may obtain a copy of the Mulan PSL v2 at:
// http://license.coscl.org.cn/MulanPSL2
//
// THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
// EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
// MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
//
// See the Mulan PSL v2 for more details.
// ***************************************************************************************
#pragma once

namespace vcd {

// VCD 活动口径：一次"值变化"折算成多少次翻转，以及 x/z 怎么参与折算。
//
// 平均功耗和逐窗峰值功耗采用不同的活动统计规则：
//   kAveraged  - 平均功耗：将 VCD 折算成翻转率和静态概率。
//                x/z 只在"回到已知值"时折算，回到原值打
//                x_transition_derate_factor 折，回到不同值算一次完整翻转。
//   kTimeBased - 逐窗/峰值功耗：
//                · X：每个"进入或离开 X"的事件都记 x_transition_derate_factor 次（默认 0.5）；
//                · Z：经过 Z 不耗功耗，0->Z->1 算 1 次完整跳变，1->Z->1 不算跳变；
//                · 初始化阶段的 unknown->known 默认忽略（include_initial_x_transitions=false）。
enum class ActivityModel
{
  kAveraged,
  kTimeBased,
};

// 活动统计参数：选择统计模式、X 翻转折算系数和初始化处理方式。
struct ActivityModelOptions
{
  ActivityModel model = ActivityModel::kAveraged;
  double x_transition_derate_factor = 0.5;
  bool include_initial_x_transitions = false;
};

inline bool isUnknownValue(const char value)
{
  return value == 'x' || value == 'z';
}

// 本次变化（last_value -> value）折算的翻转次数；两个 reader 共用这一处规则。
// 调用方只负责过滤"没有发生变化"和"同一时刻的多次变化"，是否折算由这里决定。
inline double evaluateTransition(const ActivityModelOptions& options, const char last_value, const char value,
                                 const char last_defined_value)
{
  const bool last_is_x = last_value == 'x';
  const bool last_is_z = last_value == 'z';
  const bool value_is_x = value == 'x';
  const bool value_is_z = value == 'z';
  const bool last_unknown = last_is_x || last_is_z;
  const bool value_unknown = value_is_x || value_is_z;
  const double derate = options.x_transition_derate_factor;

  if (options.model == ActivityModel::kTimeBased) {
    if (last_is_x && value_is_x) {
      return 0.0;
    }
    if (last_is_x || value_is_x) {
      // include_initial_x_transitions 控制是否计入初始化阶段的 unknown -> known。
      if (last_is_x && !value_unknown && last_defined_value == '\0' && !options.include_initial_x_transitions) {
        return 0.0;
      }
      return derate;
    }
    if (last_is_z) {
      // 经过 Z：回到进入 Z 之前的值不算跳变，回到不同值算一次完整跳变。
      return value == last_defined_value ? 0.0 : 1.0;
    }
    if (value_is_z) {
      return 0.0;  // 进入 Z 不耗功耗
    }
    return 1.0;
  }

  // kAveraged：只在回到已知值时计数，并按上一次已知值判断是否折算。
  if (value_unknown) {
    return 0.0;
  }
  if (!last_unknown) {
    return 1.0;
  }
  if (last_defined_value == '\0') {
    return 0.0;
  }
  return value == last_defined_value ? derate : 1.0;
}

}  // namespace vcd
