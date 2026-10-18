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

#include "DCRect.hpp"

namespace imj {

class DCObject
{
 public:
  DCObject() = default;
  ~DCObject() = default;
  // getter
  int32_t get_source_idx() { return _source_idx; }
  int32_t get_part_idx() { return _part_idx; }
  std::string& get_name() { return _name; }
  std::vector<DCRect>& get_rect_list() { return _rect_list; }
  bool get_retained() { return _retained; }
  // const getter
  int32_t get_source_idx() const { return _source_idx; }
  int32_t get_part_idx() const { return _part_idx; }
  const std::string& get_name() const { return _name; }
  const std::vector<DCRect>& get_rect_list() const { return _rect_list; }
  bool get_retained() const { return _retained; }
  // setter
  void set_source_idx(int32_t source_idx) { _source_idx = source_idx; }
  void set_part_idx(int32_t part_idx) { _part_idx = part_idx; }
  void set_name(const std::string& name) { _name = name; }
  void set_rect_list(const std::vector<DCRect>& rect_list) { _rect_list = rect_list; }
  void set_retained(bool retained) { _retained = retained; }
  // function

 private:
  int32_t _source_idx = -1;
  int32_t _part_idx = -1;
  std::string _name;
  std::vector<DCRect> _rect_list;
  bool _retained = false;
};

}  // namespace imj
