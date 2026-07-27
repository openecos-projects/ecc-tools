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
// MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE.
//
// See the Mulan PSL v2 for more details.
// ***************************************************************************************
#pragma once

#include "ConnectType.hpp"
#include "LVSHeader.hpp"

namespace ilvs {

class SupplyPoint
{
 public:
  SupplyPoint() = default;
  ~SupplyPoint() = default;
  // getter
  int32_t get_component_id() const { return _component_id; }
  ConnectType get_connect_type() const { return _connect_type; }
  // setter
  void set_component_id(const int32_t component_id) { _component_id = component_id; }
  void set_connect_type(const ConnectType connect_type) { _connect_type = connect_type; }

 private:
  int32_t _component_id = -1;
  ConnectType _connect_type = ConnectType::kNone;
};

}  // namespace ilvs
