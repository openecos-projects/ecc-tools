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
 * @file Net.hh
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-01-07
 * @brief The connection structure of clock driver and loads
 */

#pragma once

#include <algorithm>
#include <string>
#include <vector>

namespace icts {
class Pin;

class Net
{
 public:
  Net(const std::string& name) : _name(name) {}
  ~Net() = default;

  // Getter
  auto get_name() const -> const std::string& { return _name; }
  auto get_driver() const -> Pin* { return _driver; }
  auto get_loads() const -> const std::vector<Pin*>& { return _loads; }
  auto get_ignore_loads() const -> const std::vector<Pin*>& { return _ignore_loads; }

  // Setter
  auto set_name(const std::string& net_name) -> void { _name = net_name; }
  auto set_driver(Pin* driver) -> void { _driver = driver; }
  auto set_loads(const std::vector<Pin*>& loads) -> void { _loads = loads; }
  auto set_ignore_loads(const std::vector<Pin*>& ignore_loads) -> void { _ignore_loads = ignore_loads; }

  // Adder
  auto add_load(Pin* load) -> void
  {
    if (load == nullptr || std::ranges::find(_loads, load) != _loads.end()) {
      return;
    }
    _loads.push_back(load);
  }
  auto add_ignore_load(Pin* ignore_load) -> void
  {
    if (ignore_load == nullptr || std::ranges::find(_ignore_loads, ignore_load) != _ignore_loads.end()) {
      return;
    }
    _ignore_loads.push_back(ignore_load);
  }

 private:
  std::string _name = "";
  Pin* _driver = nullptr;
  std::vector<Pin*> _loads;
  // Top-level IO pins that stay connected to the net but are not CTS sinks:
  // they are never balanced, clustered, or counted as clock loads.
  std::vector<Pin*> _ignore_loads;
};
}  // namespace icts
