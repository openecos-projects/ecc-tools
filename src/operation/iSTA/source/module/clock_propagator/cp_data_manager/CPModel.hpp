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

#include "STAHeader.hpp"

namespace ista {

class CPClock
{
 public:
  CPClock() = default;
  ~CPClock() = default;
  CPClock(std::string_view clock_name, const std::vector<std::string>& source_list, bool is_propagated)
      : _clock_name(clock_name), _source_list(source_list), _is_propagated(is_propagated)
  {
  }

  [[nodiscard]] std::string_view get_clock_name() const { return _clock_name; }
  [[nodiscard]] const std::vector<std::string>& get_source_list() const { return _source_list; }
  [[nodiscard]] bool get_is_propagated() const { return _is_propagated; }
  [[nodiscard]] const std::vector<std::string>& get_clock_point_list() const { return _clock_point_list; }

  void set_clock_name(const std::string& clock_name) { _clock_name = clock_name; }
  void set_is_propagated(bool is_propagated) { _is_propagated = is_propagated; }
  void add_clock_pin(std::string_view clock_name) { _clock_point_list.emplace_back(clock_name); }

 private:
  std::string _clock_name;
  const std::vector<std::string>& _source_list;
  std::vector<std::string> _clock_point_list;
  bool _is_propagated = false;
};

struct CPModel
{
  std::vector<CPClock> clock_list;
};

}  // namespace ista
