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

#include "TableIdxRange.hpp"

namespace ircx {

class ProcessTable2D
{
 public:
  ProcessTable2D() = default;
  ~ProcessTable2D() = default;
  // getter
  std::vector<double>& get_row_list() { return _row_list; }
  std::vector<double>& get_column_list() { return _column_list; }
  std::vector<double>& get_value_list() { return _value_list; }
  const std::vector<double>& get_row_list() const { return _row_list; }
  const std::vector<double>& get_column_list() const { return _column_list; }
  const std::vector<double>& get_value_list() const { return _value_list; }
  // setter
  void set_row_list(const std::vector<double>& row_list) { _row_list = row_list; }
  void set_column_list(const std::vector<double>& column_list) { _column_list = column_list; }
  void set_value_list(const std::vector<double>& value_list) { _value_list = value_list; }
  // function
  bool get_is_empty() const { return _row_list.empty() || _column_list.empty() || _value_list.empty(); }
  std::optional<double> query(double row, double column) const
  {
    if (get_is_empty()) {
      return std::nullopt;
    }

    TableIdxRange row_idx_range = get_bounding_idx_range(_row_list, row);
    TableIdxRange column_idx_range = get_bounding_idx_range(_column_list, column);
    std::optional<double> low_low_value = get_value(row_idx_range.get_lower_idx(), column_idx_range.get_lower_idx());
    if (!low_low_value.has_value()) {
      return std::nullopt;
    }
    if (row_idx_range.get_lower_idx() == row_idx_range.get_upper_idx() && column_idx_range.get_lower_idx() == column_idx_range.get_upper_idx()) {
      return low_low_value;
    }

    double row_ratio = 0.0;
    if (row_idx_range.get_lower_idx() != row_idx_range.get_upper_idx()) {
      double row_delta = _row_list[row_idx_range.get_upper_idx()] - _row_list[row_idx_range.get_lower_idx()];
      if (row_delta != 0.0) {
        row_ratio = (row - _row_list[row_idx_range.get_lower_idx()]) / row_delta;
      }
    }
    double column_ratio = 0.0;
    if (column_idx_range.get_lower_idx() != column_idx_range.get_upper_idx()) {
      double column_delta = _column_list[column_idx_range.get_upper_idx()] - _column_list[column_idx_range.get_lower_idx()];
      if (column_delta != 0.0) {
        column_ratio = (column - _column_list[column_idx_range.get_lower_idx()]) / column_delta;
      }
    }

    if (row_idx_range.get_lower_idx() == row_idx_range.get_upper_idx()) {
      std::optional<double> low_high_value = get_value(row_idx_range.get_lower_idx(), column_idx_range.get_upper_idx());
      if (!low_high_value.has_value()) {
        return std::nullopt;
      }
      return std::lerp(low_low_value.value(), low_high_value.value(), column_ratio);
    }
    if (column_idx_range.get_lower_idx() == column_idx_range.get_upper_idx()) {
      std::optional<double> high_low_value = get_value(row_idx_range.get_upper_idx(), column_idx_range.get_lower_idx());
      if (!high_low_value.has_value()) {
        return std::nullopt;
      }
      return std::lerp(low_low_value.value(), high_low_value.value(), row_ratio);
    }

    std::optional<double> low_high_value = get_value(row_idx_range.get_lower_idx(), column_idx_range.get_upper_idx());
    std::optional<double> high_low_value = get_value(row_idx_range.get_upper_idx(), column_idx_range.get_lower_idx());
    std::optional<double> high_high_value = get_value(row_idx_range.get_upper_idx(), column_idx_range.get_upper_idx());
    if (!low_high_value.has_value() || !high_low_value.has_value() || !high_high_value.has_value()) {
      return std::nullopt;
    }
    double low_value = std::lerp(low_low_value.value(), low_high_value.value(), column_ratio);
    double high_value = std::lerp(high_low_value.value(), high_high_value.value(), column_ratio);
    return std::lerp(low_value, high_value, row_ratio);
  }

 private:
  TableIdxRange get_bounding_idx_range(const std::vector<double>& axis, double value) const
  {
    if (value <= axis.front()) {
      return TableIdxRange(0, 0);
    }
    if (value >= axis.back()) {
      int32_t last_idx = static_cast<int32_t>(axis.size()) - 1;
      return TableIdxRange(last_idx, last_idx);
    }

    std::vector<double>::const_iterator high_iter = std::lower_bound(axis.begin(), axis.end(), value);
    int32_t high_idx = static_cast<int32_t>(std::distance(axis.begin(), high_iter));
    if (*high_iter == value) {
      return TableIdxRange(high_idx, high_idx);
    }
    return TableIdxRange(high_idx - 1, high_idx);
  }
  std::optional<double> get_value(int32_t row_idx, int32_t column_idx) const
  {
    if (row_idx < 0 || row_idx >= static_cast<int32_t>(_row_list.size()) || column_idx < 0 || column_idx >= static_cast<int32_t>(_column_list.size())) {
      return std::nullopt;
    }
    int32_t value_idx = row_idx * static_cast<int32_t>(_column_list.size()) + column_idx;
    if (value_idx >= static_cast<int32_t>(_value_list.size())) {
      return std::nullopt;
    }
    return _value_list[value_idx];
  }

  std::vector<double> _row_list;
  std::vector<double> _column_list;
  std::vector<double> _value_list;
};

}  // namespace ircx
