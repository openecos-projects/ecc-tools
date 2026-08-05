// ***************************************************************************************
// Copyright (c) 2023-2025 Peng Cheng Laboratory
// Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences
// Copyright (c) 2023-2025 Beijing Institute of Open Source Chip
//
// iEDA is licensed under Mulan PSL v2.
// You can use this software according to the terms and conditions of the Mulan PSL v2.
// You may obtain a copy of the Mulan PSL v2 at:
// http://license.coscl.org.cn/MulanPSL2
//
// THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
// EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
// MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE.
// See the Mulan PSL v2 for more details.
// ***************************************************************************************
#pragma once

#include "FPHeader.hpp"
#include "PGNetType.hpp"

namespace ifp {

class PGGlobalConnect
{
 public:
  PGGlobalConnect() = default;
  ~PGGlobalConnect() = default;
  // getter
  std::string& get_net_name() { return _net_name; }
  std::string& get_pin_name() { return _pin_name; }
  PGNetType get_net_type() const { return _net_type; }

  // const getter
  const std::string& get_net_name() const { return _net_name; }
  const std::string& get_pin_name() const { return _pin_name; }

  // setter
  void set_net_name(std::string net_name) { _net_name = net_name; }
  void set_pin_name(std::string pin_name) { _pin_name = pin_name; }
  void set_net_type(PGNetType net_type) { _net_type = net_type; }

  // function

 private:
  std::string _net_name;
  std::string _pin_name;
  PGNetType _net_type = PGNetType::kNone;
};

}  // namespace ifp
