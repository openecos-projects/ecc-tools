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

#include "PeakPowerValue.hpp"
#include <algorithm>

#include "PWHeader.hpp"

namespace ipw {

struct PeakPowerWindow
{
  std::size_t window_index = 0;
  double begin_time = 0.0;
  double end_time = 0.0;
  double power = 0.0;
};

class PeakPowerSummary
{
 public:
  PeakPowerSummary() = default;
  ~PeakPowerSummary() = default;
  // getter
  bool get_is_enabled() const { return _is_enabled; }
  PeakPowerValue& get_total_peak_value() { return _total_peak_value; }
  std::vector<PeakPowerWindow>& get_window_list() { return _window_list; }
  const PeakPowerValue& get_total_peak_value() const { return _total_peak_value; }
  const std::vector<PeakPowerWindow>& get_window_list() const { return _window_list; }
  // 每实例峰值：按 instance id 索引；written 标记等价于原来的 map "存在"。
  std::size_t get_instance_num() const { return _instance_peak_written.size(); }
  PeakPowerValue& getOrCreateInstancePeakValue(const uint32_t instance_id)
  {
    _instance_peak_written[instance_id] = 1;
    return _instance_peak_value_list[instance_id];
  }
  PeakPowerValue* findInstancePeakValue(const uint32_t instance_id)
  {
    if (instance_id >= _instance_peak_written.size() || _instance_peak_written[instance_id] == 0) {
      return nullptr;
    }
    return &_instance_peak_value_list[instance_id];
  }
  const PeakPowerValue* findInstancePeakValue(const uint32_t instance_id) const
  {
    if (instance_id >= _instance_peak_written.size() || _instance_peak_written[instance_id] == 0) {
      return nullptr;
    }
    return &_instance_peak_value_list[instance_id];
  }
  void resizeInstancePeakValueList(const std::size_t instance_num)
  {
    _instance_peak_value_list.assign(instance_num, PeakPowerValue());
    _instance_peak_written.assign(instance_num, 0);
  }
  // 跨 worker 合并：逐 id 取两边都写过的做 merge。
  void mergeInstancePeakValues(const PeakPowerSummary& other)
  {
    const std::size_t merge_num = std::min(_instance_peak_written.size(), other._instance_peak_written.size());
    for (std::size_t instance_id = 0; instance_id < merge_num; instance_id++) {
      if (other._instance_peak_written[instance_id] == 0) {
        continue;
      }
      getOrCreateInstancePeakValue(static_cast<uint32_t>(instance_id))
          .merge(other._instance_peak_value_list[instance_id]);
    }
  }
  // setter
  void set_is_enabled(const bool is_enabled) { _is_enabled = is_enabled; }
  // function
  // 复位峰值结果，重复运行前调用；峰值与窗口列表必须同时清，否则导出会对不上。
  void reset()
  {
    _is_enabled = false;
    _total_peak_value = PeakPowerValue();
    std::fill(_instance_peak_written.begin(), _instance_peak_written.end(), 0);
    _window_list.clear();
  }

 private:
  bool _is_enabled = false;
  PeakPowerValue _total_peak_value;
  std::vector<PeakPowerValue> _instance_peak_value_list;
  std::vector<uint8_t> _instance_peak_written;
  std::vector<PeakPowerWindow> _window_list;
};

}  // namespace ipw
