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

#include "ProcessTable1DEntry.hpp"

namespace ircx {

class ProcessTable1D
{
 public:
  ProcessTable1D() = default;
  ~ProcessTable1D() = default;
  // getter
  std::vector<ProcessTable1DEntry>& get_entry_list() { return _entry_list; }
  const std::vector<ProcessTable1DEntry>& get_entry_list() const { return _entry_list; }
  // setter
  void set_entry_list(const std::vector<ProcessTable1DEntry>& entry_list)
  {
    _entry_list = entry_list;
    sort_entry_list();
  }
  // function
  void add_entry(double key, double value)
  {
    _entry_list.emplace_back(key, value);
    sort_entry_list();
  }
  bool get_is_empty() const { return _entry_list.empty(); }
  std::optional<double> query(double key) const
  {
    if (_entry_list.empty()) {
      return std::nullopt;
    }
    if (_entry_list.size() == 1 || key <= _entry_list.front().get_key()) {
      return _entry_list.front().get_value();
    }
    if (key >= _entry_list.back().get_key()) {
      return _entry_list.back().get_value();
    }

    std::vector<ProcessTable1DEntry>::const_iterator high_iter
        = std::lower_bound(_entry_list.begin(), _entry_list.end(), key,
                           [this](const ProcessTable1DEntry& entry, double table_key) { return is_entry_less_than_key(entry, table_key); });
    if (high_iter == _entry_list.end()) {
      return _entry_list.back().get_value();
    }
    if (high_iter->get_key() == key) {
      return high_iter->get_value();
    }

    std::vector<ProcessTable1DEntry>::const_iterator low_iter = std::prev(high_iter);
    double key_delta = high_iter->get_key() - low_iter->get_key();
    if (key_delta == 0.0) {
      return high_iter->get_value();
    }
    double ratio = (key - low_iter->get_key()) / key_delta;
    return std::lerp(low_iter->get_value(), high_iter->get_value(), ratio);
  }

 private:
  bool is_entry_less_than_key(const ProcessTable1DEntry& entry, double key) const { return entry.get_key() < key; }
  bool is_entry_less(const ProcessTable1DEntry& first_entry, const ProcessTable1DEntry& second_entry) const
  {
    return first_entry.get_key() < second_entry.get_key();
  }
  void sort_entry_list()
  {
    std::sort(_entry_list.begin(), _entry_list.end(),
              [this](const ProcessTable1DEntry& first_entry, const ProcessTable1DEntry& second_entry) { return is_entry_less(first_entry, second_entry); });
  }

  std::vector<ProcessTable1DEntry> _entry_list;
};

}  // namespace ircx
