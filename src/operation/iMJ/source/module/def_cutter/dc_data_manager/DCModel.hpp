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

#include "DCGrid.hpp"
#include "DCIOPin.hpp"
#include "DCNet.hpp"
#include "DCObject.hpp"

namespace imj {

class DCModel
{
 public:
  DCModel() = default;
  ~DCModel() = default;
  // getter
  std::string& get_design_name() { return _design_name; }
  int32_t get_micron_dbu() { return _micron_dbu; }
  DCRect& get_input_die() { return _input_die; }
  DCRect& get_die() { return _die; }
  std::vector<DCGrid>& get_row_list() { return _row_list; }
  std::vector<DCGrid>& get_track_list() { return _track_list; }
  std::vector<DCGrid>& get_gcell_list() { return _gcell_list; }
  std::vector<DCObject>& get_instance_list() { return _instance_list; }
  std::vector<DCObject>& get_io_pin_list() { return _io_pin_list; }
  std::vector<DCIOPin>& get_cut_pin_list() { return _cut_pin_list; }
  std::vector<DCNet>& get_net_list() { return _net_list; }
  std::vector<DCObject>& get_blockage_list() { return _blockage_list; }
  std::vector<DCObject>& get_fill_list() { return _fill_list; }
  std::vector<DCObject>& get_region_list() { return _region_list; }
  std::vector<DCObject>& get_slot_list() { return _slot_list; }
  // const getter
  const std::string& get_design_name() const { return _design_name; }
  int32_t get_micron_dbu() const { return _micron_dbu; }
  const DCRect& get_input_die() const { return _input_die; }
  const DCRect& get_die() const { return _die; }
  const std::vector<DCGrid>& get_row_list() const { return _row_list; }
  const std::vector<DCGrid>& get_track_list() const { return _track_list; }
  const std::vector<DCGrid>& get_gcell_list() const { return _gcell_list; }
  const std::vector<DCObject>& get_instance_list() const { return _instance_list; }
  const std::vector<DCObject>& get_io_pin_list() const { return _io_pin_list; }
  const std::vector<DCIOPin>& get_cut_pin_list() const { return _cut_pin_list; }
  const std::vector<DCNet>& get_net_list() const { return _net_list; }
  const std::vector<DCObject>& get_blockage_list() const { return _blockage_list; }
  const std::vector<DCObject>& get_fill_list() const { return _fill_list; }
  const std::vector<DCObject>& get_region_list() const { return _region_list; }
  const std::vector<DCObject>& get_slot_list() const { return _slot_list; }
  // setter
  void set_design_name(const std::string& design_name) { _design_name = design_name; }
  void set_micron_dbu(int32_t micron_dbu) { _micron_dbu = micron_dbu; }
  void set_input_die(const DCRect& input_die) { _input_die = input_die; }
  void set_die(const DCRect& die) { _die = die; }
  void set_row_list(const std::vector<DCGrid>& row_list) { _row_list = row_list; }
  void set_track_list(const std::vector<DCGrid>& track_list) { _track_list = track_list; }
  void set_gcell_list(const std::vector<DCGrid>& gcell_list) { _gcell_list = gcell_list; }
  void set_instance_list(const std::vector<DCObject>& instance_list) { _instance_list = instance_list; }
  void set_io_pin_list(const std::vector<DCObject>& io_pin_list) { _io_pin_list = io_pin_list; }
  void set_cut_pin_list(const std::vector<DCIOPin>& cut_pin_list) { _cut_pin_list = cut_pin_list; }
  void set_net_list(const std::vector<DCNet>& net_list) { _net_list = net_list; }
  void set_blockage_list(const std::vector<DCObject>& blockage_list) { _blockage_list = blockage_list; }
  void set_fill_list(const std::vector<DCObject>& fill_list) { _fill_list = fill_list; }
  void set_region_list(const std::vector<DCObject>& region_list) { _region_list = region_list; }
  void set_slot_list(const std::vector<DCObject>& slot_list) { _slot_list = slot_list; }
  // function

 private:
  std::string _design_name;
  int32_t _micron_dbu = -1;
  DCRect _input_die;
  DCRect _die;
  std::vector<DCGrid> _row_list;
  std::vector<DCGrid> _track_list;
  std::vector<DCGrid> _gcell_list;
  std::vector<DCObject> _instance_list;
  std::vector<DCObject> _io_pin_list;
  std::vector<DCIOPin> _cut_pin_list;
  std::vector<DCNet> _net_list;
  std::vector<DCObject> _blockage_list;
  std::vector<DCObject> _fill_list;
  std::vector<DCObject> _region_list;
  std::vector<DCObject> _slot_list;
};

}  // namespace imj
