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

#include "CellMaster.hpp"
#include "Core.hpp"
#include "Die.hpp"
#include "FPHeader.hpp"
#include "IOPin.hpp"
#include "Instance.hpp"
#include "Net.hpp"
#include "PGNet.hpp"
#include "PGSegment.hpp"
#include "RoutingLayer.hpp"
#include "Row.hpp"
#include "Site.hpp"
#include "Track.hpp"

namespace ifp {

class Database
{
 public:
  Database() = default;
  ~Database() = default;
  // getter
  std::string& get_design_name() { return _design_name; }
  int32_t get_micron_dbu() const { return _micron_dbu; }
  int32_t get_manufacture_grid() const { return _manufacture_grid; }
  double get_cell_area() const { return _cell_area; }
  Die& get_die() { return _die; }
  Core& get_core() { return _core; }
  std::map<std::string, Site>& get_site_map() { return _site_map; }
  std::vector<Track>& get_new_track_list() { return _new_track_list; }
  std::vector<Row>& get_new_row_list() { return _new_row_list; }
  std::vector<Instance>& get_instance_list() { return _instance_list; }
  std::map<std::string, int32_t>& get_instance_name_to_idx_map() { return _instance_name_to_idx_map; }
  std::map<std::string, CellMaster>& get_cell_master_map() { return _cell_master_map; }
  std::vector<RoutingLayer>& get_routing_layer_list() { return _routing_layer_list; }
  std::map<std::string, int32_t>& get_routing_layer_name_to_idx_map() { return _routing_layer_name_to_idx_map; }
  std::vector<Net>& get_net_list() { return _net_list; }
  std::vector<IOPin>& get_io_pin_list() { return _io_pin_list; }
  std::map<std::string, int32_t>& get_io_pin_name_to_idx_map() { return _io_pin_name_to_idx_map; }
  std::vector<PGNet>& get_pg_net_list() { return _pg_net_list; }
  std::map<std::string, int32_t>& get_pg_net_name_to_idx_map() { return _pg_net_name_to_idx_map; }
  std::vector<PGSegment>& get_pg_segment_list() { return _pg_segment_list; }
  std::vector<Row>& get_row_list() { return _row_list; }
  bool is_die_updated() const { return _die_updated; }
  bool is_core_updated() const { return _core_updated; }
  bool is_track_updated() const { return _track_updated; }

  // const getter
  const std::string& get_design_name() const { return _design_name; }
  const Die& get_die() const { return _die; }
  const Core& get_core() const { return _core; }
  const std::map<std::string, Site>& get_site_map() const { return _site_map; }
  const std::vector<Track>& get_new_track_list() const { return _new_track_list; }
  const std::vector<Row>& get_new_row_list() const { return _new_row_list; }
  const std::vector<Instance>& get_instance_list() const { return _instance_list; }
  const std::map<std::string, int32_t>& get_instance_name_to_idx_map() const { return _instance_name_to_idx_map; }
  const std::map<std::string, CellMaster>& get_cell_master_map() const { return _cell_master_map; }
  const std::vector<RoutingLayer>& get_routing_layer_list() const { return _routing_layer_list; }
  const std::map<std::string, int32_t>& get_routing_layer_name_to_idx_map() const { return _routing_layer_name_to_idx_map; }
  const std::vector<Net>& get_net_list() const { return _net_list; }
  const std::vector<IOPin>& get_io_pin_list() const { return _io_pin_list; }
  const std::map<std::string, int32_t>& get_io_pin_name_to_idx_map() const { return _io_pin_name_to_idx_map; }
  const std::vector<PGNet>& get_pg_net_list() const { return _pg_net_list; }
  const std::map<std::string, int32_t>& get_pg_net_name_to_idx_map() const { return _pg_net_name_to_idx_map; }
  const std::vector<PGSegment>& get_pg_segment_list() const { return _pg_segment_list; }
  const std::vector<Row>& get_row_list() const { return _row_list; }
  // setter
  void set_design_name(std::string design_name) { _design_name = design_name; }
  void set_micron_dbu(int32_t micron_dbu) { _micron_dbu = micron_dbu; }
  void set_manufacture_grid(int32_t manufacture_grid) { _manufacture_grid = manufacture_grid; }
  void set_cell_area(double cell_area) { _cell_area = cell_area; }
  void set_die(const Die& die) { _die = die; }
  void set_core(const Core& core) { _core = core; }
  void set_site_map(const std::map<std::string, Site>& site_map) { _site_map = site_map; }
  void set_new_track_list(const std::vector<Track>& new_track_list) { _new_track_list = new_track_list; }
  void set_new_row_list(const std::vector<Row>& new_row_list) { _new_row_list = new_row_list; }
  void set_instance_list(const std::vector<Instance>& instance_list) { _instance_list = instance_list; }
  void set_instance_name_to_idx_map(const std::map<std::string, int32_t>& instance_name_to_idx_map)
  {
    _instance_name_to_idx_map = instance_name_to_idx_map;
  }
  void set_cell_master_map(const std::map<std::string, CellMaster>& cell_master_map) { _cell_master_map = cell_master_map; }
  void set_routing_layer_list(const std::vector<RoutingLayer>& routing_layer_list) { _routing_layer_list = routing_layer_list; }
  void set_routing_layer_name_to_idx_map(const std::map<std::string, int32_t>& routing_layer_name_to_idx_map)
  {
    _routing_layer_name_to_idx_map = routing_layer_name_to_idx_map;
  }
  void set_net_list(const std::vector<Net>& net_list) { _net_list = net_list; }
  void set_io_pin_list(const std::vector<IOPin>& io_pin_list) { _io_pin_list = io_pin_list; }
  void set_io_pin_name_to_idx_map(const std::map<std::string, int32_t>& io_pin_name_to_idx_map)
  {
    _io_pin_name_to_idx_map = io_pin_name_to_idx_map;
  }
  void set_pg_net_list(const std::vector<PGNet>& pg_net_list) { _pg_net_list = pg_net_list; }
  void set_pg_net_name_to_idx_map(const std::map<std::string, int32_t>& pg_net_name_to_idx_map)
  {
    _pg_net_name_to_idx_map = pg_net_name_to_idx_map;
  }
  void set_pg_segment_list(const std::vector<PGSegment>& pg_segment_list) { _pg_segment_list = pg_segment_list; }
  void set_row_list(const std::vector<Row>& row_list) { _row_list = row_list; }
  void set_die_updated(bool die_updated) { _die_updated = die_updated; }
  void set_core_updated(bool core_updated) { _core_updated = core_updated; }
  void set_track_updated(bool track_updated) { _track_updated = track_updated; }

  // function

 private:
  std::string _design_name;
  int32_t _micron_dbu = -1;
  int32_t _manufacture_grid = -1;
  double _cell_area = -1.0;
  Die _die;
  Core _core;
  std::map<std::string, Site> _site_map;
  std::vector<Track> _new_track_list;
  std::vector<Row> _new_row_list;
  std::vector<Instance> _instance_list;
  std::map<std::string, int32_t> _instance_name_to_idx_map;
  std::map<std::string, CellMaster> _cell_master_map;
  std::vector<RoutingLayer> _routing_layer_list;
  std::map<std::string, int32_t> _routing_layer_name_to_idx_map;
  std::vector<Net> _net_list;
  std::vector<IOPin> _io_pin_list;
  std::map<std::string, int32_t> _io_pin_name_to_idx_map;
  std::vector<PGNet> _pg_net_list;
  std::map<std::string, int32_t> _pg_net_name_to_idx_map;
  std::vector<PGSegment> _pg_segment_list;
  std::vector<Row> _row_list;
  bool _die_updated = false;
  bool _core_updated = false;
  bool _track_updated = false;
};

}  // namespace ifp
