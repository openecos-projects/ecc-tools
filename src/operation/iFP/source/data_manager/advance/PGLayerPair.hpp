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

namespace ifp {

class PGLayerPair
{
 public:
  PGLayerPair() = default;
  ~PGLayerPair() = default;
  // getter
  std::string& get_first_layer_name() { return _first_layer_name; }
  std::string& get_second_layer_name() { return _second_layer_name; }

  // const getter
  const std::string& get_first_layer_name() const { return _first_layer_name; }
  const std::string& get_second_layer_name() const { return _second_layer_name; }

  // setter
  void set_first_layer_name(std::string first_layer_name) { _first_layer_name = first_layer_name; }
  void set_second_layer_name(std::string second_layer_name) { _second_layer_name = second_layer_name; }

  // function

 private:
  std::string _first_layer_name;
  std::string _second_layer_name;
};

}  // namespace ifp
