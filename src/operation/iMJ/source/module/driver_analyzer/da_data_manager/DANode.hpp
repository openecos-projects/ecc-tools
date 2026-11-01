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

#include "DAPin.hpp"

namespace imj {

class DANode
{
 public:
  DANode() = default;
  ~DANode() = default;
  // getter
  DAPin& get_pin() { return _pin; }
  DAPinType get_type() { return _type; }
  std::vector<int32_t>& get_net_idx_list() { return _net_idx_list; }
  // const getter
  const DAPin& get_pin() const { return _pin; }
  DAPinType get_type() const { return _type; }
  const std::vector<int32_t>& get_net_idx_list() const { return _net_idx_list; }
  // setter
  void set_pin(const DAPin& pin) { _pin = pin; }
  void set_type(DAPinType type) { _type = type; }
  void set_net_idx_list(const std::vector<int32_t>& net_idx_list) { _net_idx_list = net_idx_list; }
  // function
  bool isInferred() const { return _type != _pin.get_type(); }

 private:
  DAPin _pin;
  DAPinType _type = DAPinType::kNone;
  std::vector<int32_t> _net_idx_list;
};

}  // namespace imj
