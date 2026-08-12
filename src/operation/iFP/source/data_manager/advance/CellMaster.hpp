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
// See the Mulan PSL v2 for more details.
// ***************************************************************************************
#pragma once

#include "FPHeader.hpp"

namespace ifp {

class CellMaster
{
 public:
  CellMaster() = default;
  ~CellMaster() = default;
  // getter
  std::string& get_name() { return _name; }
  int32_t get_width() const { return _width; }
  int32_t get_height() const { return _height; }
  bool get_pad() const { return _pad; }
  bool get_pad_filler() const { return _pad_filler; }
  bool get_spacer() const { return _spacer; }
  bool get_corner() const { return _corner; }

  // const getter
  const std::string& get_name() const { return _name; }

  // setter
  void set_name(std::string name) { _name = name; }
  void set_width(int32_t width) { _width = width; }
  void set_height(int32_t height) { _height = height; }
  void set_pad(bool pad) { _pad = pad; }
  void set_pad_filler(bool pad_filler) { _pad_filler = pad_filler; }
  void set_spacer(bool spacer) { _spacer = spacer; }
  void set_corner(bool corner) { _corner = corner; }

  // function

 private:
  std::string _name;
  int32_t _width = -1;
  int32_t _height = -1;
  bool _pad = false;
  bool _pad_filler = false;
  bool _spacer = false;
  bool _corner = false;
};

}  // namespace ifp
