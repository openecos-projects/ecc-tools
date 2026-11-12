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

#include "DAPinType.hpp"
#include "MJHeader.hpp"

namespace imj {

class DAPin
{
 public:
  DAPin() = default;
  ~DAPin() = default;
  // getter
  std::string& get_instance_name() { return _instance_name; }
  std::string& get_pin_name() { return _pin_name; }
  DAPinType get_type() { return _type; }
  bool get_is_io() { return _is_io; }
  bool get_is_pad() { return _is_pad; }
  // const getter
  const std::string& get_instance_name() const { return _instance_name; }
  const std::string& get_pin_name() const { return _pin_name; }
  DAPinType get_type() const { return _type; }
  bool get_is_io() const { return _is_io; }
  bool get_is_pad() const { return _is_pad; }
  // setter
  void set_instance_name(const std::string& instance_name) { _instance_name = instance_name; }
  void set_pin_name(const std::string& pin_name) { _pin_name = pin_name; }
  void set_type(DAPinType type) { _type = type; }
  void set_is_io(bool is_io) { _is_io = is_io; }
  void set_is_pad(bool is_pad) { _is_pad = is_pad; }
  // function

 private:
  std::string _instance_name;
  std::string _pin_name;
  DAPinType _type = DAPinType::kNone;
  bool _is_io = false;
  bool _is_pad = false;
};

}  // namespace imj
