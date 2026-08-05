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

#include "MILayerRule.hpp"
#include "MIRect.hpp"

namespace izh {

class MIModel
{
 public:
  MIModel() = default;
  ~MIModel() = default;
  // getter
  std::string& get_rule_file_path() { return _rule_file_path; }
  MIRect& get_fill_area() { return _fill_area; }
  std::vector<MILayerRule>& get_layer_rule_list() { return _layer_rule_list; }
  bool get_reset_fill() const { return _reset_fill; }
  int32_t get_inserted_metal_num() const { return _inserted_metal_num; }
  // const getter
  const std::string& get_rule_file_path() const { return _rule_file_path; }
  const MIRect& get_fill_area() const { return _fill_area; }
  const std::vector<MILayerRule>& get_layer_rule_list() const { return _layer_rule_list; }
  // setter
  void set_rule_file_path(const std::string& rule_file_path) { _rule_file_path = rule_file_path; }
  void set_fill_area(const MIRect& fill_area) { _fill_area = fill_area; }
  void set_layer_rule_list(const std::vector<MILayerRule>& layer_rule_list) { _layer_rule_list = layer_rule_list; }
  void set_reset_fill(bool reset_fill) { _reset_fill = reset_fill; }
  void set_inserted_metal_num(int32_t inserted_metal_num) { _inserted_metal_num = inserted_metal_num; }
  // function
  void addInsertedMetalNum() { ++_inserted_metal_num; }
  void addInsertedMetalNum(int32_t inserted_metal_num) { _inserted_metal_num += inserted_metal_num; }

 private:
  std::string _rule_file_path;
  MIRect _fill_area;
  std::vector<MILayerRule> _layer_rule_list;
  bool _reset_fill = false;
  int32_t _inserted_metal_num = 0;
};

}  // namespace izh
