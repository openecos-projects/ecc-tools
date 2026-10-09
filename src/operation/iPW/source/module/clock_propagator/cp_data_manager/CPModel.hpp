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
// MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE.
// See the Mulan PSL v2 for more details.
// ***************************************************************************************
#pragma once

#include "ClockSlewState.hpp"
#include "PWHeader.hpp"

namespace ipw {

class CPModel
{
 public:
  CPModel() = default;
  ~CPModel() = default;
  // getter
  ClockSlewState& get_clock_state(std::string_view pin_name, std::string_view clock_name)
  {
    return _clock_state_map[std::string(pin_name)][std::string(clock_name)];
  }
  std::string& get_clock_name(std::string_view pin_name) { return _clock_name_map[std::string(pin_name)]; }
  const ClockSlewState* find_clock_state(std::string_view pin_name, std::string_view clock_name) const
  {
    const auto clock_state_map = _clock_state_map.find(std::string(pin_name));
    if (clock_state_map == _clock_state_map.end()) {
      return nullptr;
    }
    const auto clock_state = clock_state_map->second.find(std::string(clock_name));
    return clock_state == clock_state_map->second.end() ? nullptr : &clock_state->second;
  }
  bool has_clock_state(std::string_view pin_name, std::string_view clock_name) const
  {
    return find_clock_state(pin_name, clock_name) != nullptr;
  }
  // setter
  void set_clock_name(std::string_view pin_name, std::string_view clock_name) { _clock_name_map[std::string(pin_name)] = clock_name; }

 private:
  std::map<std::string, std::string> _clock_name_map;
  std::map<std::string, std::map<std::string, ClockSlewState>> _clock_state_map;
};

}  // namespace ipw
