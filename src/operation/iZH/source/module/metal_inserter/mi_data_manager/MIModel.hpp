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

#include "MIComParam.hpp"
#include "MILayer.hpp"
#include "MIRect.hpp"

namespace izh {

class MIModel
{
 public:
  MIModel() = default;
  ~MIModel() = default;
  // getter
  MIComParam& get_mi_com_param() { return _mi_com_param; }
  MIRect& get_die() { return _die; }
  int32_t get_micron_dbu() const { return _micron_dbu; }
  int32_t get_manufacture_grid() const { return _manufacture_grid; }
  std::vector<MILayer>& get_mi_layer_list() { return _mi_layer_list; }
  int32_t get_inserted_metal_num() const { return _inserted_metal_num; }
  // const getter
  const MIComParam& get_mi_com_param() const { return _mi_com_param; }
  const MIRect& get_die() const { return _die; }
  const std::vector<MILayer>& get_mi_layer_list() const { return _mi_layer_list; }
  // setter
  void set_mi_com_param(const MIComParam& mi_com_param) { _mi_com_param = mi_com_param; }
  void set_die(const MIRect& die) { _die = die; }
  void set_micron_dbu(int32_t micron_dbu) { _micron_dbu = micron_dbu; }
  void set_manufacture_grid(int32_t manufacture_grid) { _manufacture_grid = manufacture_grid; }
  void set_mi_layer_list(const std::vector<MILayer>& mi_layer_list) { _mi_layer_list = mi_layer_list; }
  void set_inserted_metal_num(int32_t inserted_metal_num) { _inserted_metal_num = inserted_metal_num; }
  // function
  void add_inserted_metal_num() { ++_inserted_metal_num; }
  void add_inserted_metal_num(int32_t inserted_metal_num) { _inserted_metal_num += inserted_metal_num; }

 private:
  MIComParam _mi_com_param;
  MIRect _die;
  int32_t _micron_dbu = 0;
  int32_t _manufacture_grid = 0;
  std::vector<MILayer> _mi_layer_list;
  int32_t _inserted_metal_num = 0;
};

}  // namespace izh
