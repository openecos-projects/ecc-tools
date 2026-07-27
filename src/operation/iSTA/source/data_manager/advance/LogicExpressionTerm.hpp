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

#include "LogicOperationType.hpp"
#include "STAHeader.hpp"

namespace ista {

class LogicExpressionTerm
{
 public:
  LogicExpressionTerm() = default;
  ~LogicExpressionTerm() = default;
  // getter
  LogicOperationType get_operation_type() const { return _operation_type; }
  std::string& get_port_name() { return _port_name; }
  // setter
  void set_operation_type(const LogicOperationType& operation_type) { _operation_type = operation_type; }
  void set_port_name(const std::string& port_name) { _port_name = port_name; }
  // function

 private:
  LogicOperationType _operation_type = LogicOperationType::kNone;
  std::string _port_name;
};

}  // namespace ista
