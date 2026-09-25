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
// See the Mulan PSL v2 for more details.
// ***************************************************************************************
#pragma once

#include "PWHeader.hpp"

namespace ipw {

class ParasiticDmpLoadModel
{
 public:
  ParasiticDmpLoadModel() = default;
  ~ParasiticDmpLoadModel() = default;
  // getter
  bool get_is_valid() const { return _is_valid; }
  std::vector<double>& get_pole_list() { return _pole_list; }
  std::vector<double>& get_residue_list() { return _residue_list; }
  // setter
  void set_is_valid(const bool is_valid) { _is_valid = is_valid; }
  void set_pole_list(const std::vector<double>& pole_list) { _pole_list = pole_list; }
  void set_residue_list(const std::vector<double>& residue_list) { _residue_list = residue_list; }
  // function

 private:
  bool _is_valid = false;
  std::vector<double> _pole_list;
  std::vector<double> _residue_list;
};

}  // namespace ipw
