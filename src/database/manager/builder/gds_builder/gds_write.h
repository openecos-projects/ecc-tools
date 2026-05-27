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

#include <time.h>

#include <map>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "../def_service/def_service.h"

namespace gdstk {
struct Cell;
struct Library;
}  // namespace gdstk

namespace idb {

using std::string;
using std::vector;

#define kDbSuccess 0
#define kDbFail 1

#define CLOCKS_PER_MS 1000

class Def2GdsWrite
{
 public:
  explicit Def2GdsWrite(IdbDefService* def_service);
  ~Def2GdsWrite();

  IdbDefService* get_service() { return _def_service; }

  int32_t set_units();

  int32_t write_version();
  int32_t write_design();
  int32_t write_die();
  int32_t write_track_grid();
  int32_t write_row();
  int32_t write_component();
  int32_t write_net();
  int32_t write_special_net();
  int32_t write_pin();
  int32_t write_via();
  int32_t write_blockage();
  int32_t write_gcell_grid();
  int32_t write_region();
  int32_t write_slot();
  int32_t write_group();
  int32_t write_fill();

  void set_start_time(clock_t time) { _start_time = time; }
  void set_end_time(clock_t time) { _end_time = time; }
  float time_eclips() { return (float(_end_time - _start_time)) / CLOCKS_PER_MS; }

  bool writeDb(const char* file);
  bool writeHardenedDb(const char* file);
  int32_t write_harden_macro_pins();
  int32_t write_harden_macro_obs();
  bool writeChip();

 private:
  IdbDefService* _def_service = nullptr;
  int32_t _index = 0;
  clock_t _start_time = 0;
  clock_t _end_time = 0;

  std::unique_ptr<gdstk::Library> _library;
  gdstk::Cell* _top_cell = nullptr;
  std::map<std::string, int> _cell_name_count;
  std::set<std::string> _used_cell_names;

  gdstk::Cell* createCell(const string& name);
  void addReferenceDefault(gdstk::Cell* child);
  void addLabel(gdstk::Cell* gds_cell, const string& text, int32_t x, int32_t y, int32_t layer = 0, int32_t datatype = 0);
  string sanitizeCellName(const string& name);
  bool finishWrite(const char* file);

  std::pair<int32_t, int32_t> get_pdn_layer_order_range();

  int32_t _unit_microns = -1;
  double transDB2Unit(int32_t value) const { return _unit_microns > 0 ? static_cast<double>(value) / _unit_microns : value; }

  int32_t write_net_wire(gdstk::Cell* gds_cell, IdbRegularWire* wire);
  int32_t write_net_wire_segment(gdstk::Cell* gds_cell, IdbRegularWireSegment* segment);
  int32_t write_net_wire_segment_points(gdstk::Cell* gds_cell, IdbRegularWireSegment* segment);
  int32_t write_net_wire_segment_via(gdstk::Cell* gds_cell, IdbRegularWireSegment* segment);
  int32_t write_net_wire_segment_rect(gdstk::Cell* gds_cell, IdbRegularWireSegment* segment);
  int32_t write_specialnet_wire(gdstk::Cell* gds_cell, IdbSpecialWire* wire);
  int32_t write_specialnet_wire_segment(gdstk::Cell* gds_cell, IdbSpecialWireSegment* segment);
  int32_t write_specialnet_wire_segment_points(gdstk::Cell* gds_cell, IdbSpecialWireSegment* segment);
  int32_t write_specialnet_wire_segment_via(gdstk::Cell* gds_cell, IdbSpecialWireSegment* segment);
  int32_t write_specialnet_wire_segment_rect(gdstk::Cell* gds_cell, IdbSpecialWireSegment* segment);

  void packVia(gdstk::Cell* gds_cell, IdbVia* via);
  void packPin(gdstk::Cell* gds_cell, IdbPin* pin);
  void packLayerShape(gdstk::Cell* gds_cell, IdbLayerShape* layer_shape);
  void packRect(gdstk::Cell* gds_cell, IdbRect* rect, IdbLayer* layer);
  void packRect(gdstk::Cell* gds_cell, IdbRect* rect, int32_t layer_id);
  void packRect(gdstk::Cell* gds_cell, int32_t ll_x, int32_t ll_y, int32_t ur_x, int32_t ur_y, IdbLayer* layer);
  void packRect(gdstk::Cell* gds_cell, int32_t ll_x, int32_t ll_y, int32_t ur_x, int32_t ur_y, int32_t layer_id,
                int32_t datatype = 0);
  void packSegment(gdstk::Cell* gds_cell, IdbLayerRouting* routing_layer, IdbCoordinate<int32_t>* point_1,
                   IdbCoordinate<int32_t>* point_2, int32_t width = -1);
};
}  // namespace idb
