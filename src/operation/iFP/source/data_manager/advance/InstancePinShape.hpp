// ***************************************************************************************
// Copyright (c) 2023-2025 Peng Cheng Laboratory
// Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences
// Copyright (c) 2023-2025 Beijing Institute of Open Source Chip
//
// iEDA is licensed under Mulan PSL v2.
// You can use this software according to the terms and conditions of the Mulan PSL v2.
// You may obtain a copy of Mulan PSL v2 at:
//
// http://license.coscl.org.cn/MulanPSL2
//
// THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
// EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
// MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE.
// See the Mulan PSL v2 for more details.
// ***************************************************************************************
#pragma once

#include "PlanarRect.hpp"

namespace ifp {

class InstancePinShape : public PlanarRect
{
 public:
  InstancePinShape() = default;
  ~InstancePinShape() = default;
  // getter
  std::string& get_pin_name() { return _pin_name; }
  std::string& get_layer_name() { return _layer_name; }

  // const getter
  const std::string& get_pin_name() const { return _pin_name; }
  const std::string& get_layer_name() const { return _layer_name; }

  // setter
  void set_pin_name(std::string pin_name) { _pin_name = pin_name; }
  void set_layer_name(std::string layer_name) { _layer_name = layer_name; }

  // function

 private:
  std::string _pin_name;
  std::string _layer_name;
};

}  // namespace ifp
