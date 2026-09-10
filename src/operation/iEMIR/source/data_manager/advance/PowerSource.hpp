// ***************************************************************************************
// Copyright (c) 2023-2025 Peng Cheng Laboratory
// Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences
// Copyright (c) 2023-2025 Beijing Institute of Open Source Chip
//
// iEDA is licensed under Mulan PSL v2.
// ***************************************************************************************
#pragma once

#include "EMIRHeader.hpp"
#include "PowerNetType.hpp"

namespace iemir {

class PowerSource
{
 public:
  std::string& get_name() { return _name; }
  std::string& get_net_name() { return _net_name; }
  std::string& get_layer_name() { return _layer_name; }
  PowerNetType get_net_type() { return _net_type; }
  int32_t get_x() { return _x; }
  int32_t get_y() { return _y; }

  void set_name(const std::string& name) { _name = name; }
  void set_net_name(const std::string& net_name) { _net_name = net_name; }
  void set_layer_name(const std::string& layer_name) { _layer_name = layer_name; }
  void set_net_type(PowerNetType net_type) { _net_type = net_type; }
  void set_x(int32_t x) { _x = x; }
  void set_y(int32_t y) { _y = y; }

 private:
  std::string _name;
  std::string _net_name;
  std::string _layer_name;
  PowerNetType _net_type = PowerNetType::kNone;
  int32_t _x = 0;
  int32_t _y = 0;
};

}  // namespace iemir
